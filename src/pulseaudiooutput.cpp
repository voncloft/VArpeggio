#include "pulseaudiooutput.h"

#include "synthesizer.h"

#include <pulse/pulseaudio.h>
#include <pulse/rtclock.h>
#include <pulse/thread-mainloop.h>
#include <algorithm>
#include <limits>
#include <utility>

struct PulseAudioOutput::Impl {
    static constexpr qsizetype frameBytes = 2 * qsizetype(sizeof(float));
    SynthStream &source;
    const int sampleRate;
    const QByteArray deviceId;
    qsizetype requestedBufferBytes;
    qsizetype actualBufferBytes;
    pa_threaded_mainloop *loop = nullptr;
    pa_context *context = nullptr;
    pa_stream *stream = nullptr;
    bool loopRunning = false;
    bool contextFailed = false;
    bool streamFailed = false;
    bool underflow = false;
    bool waitTimedOut = false;
    quint64 streamGeneration = 0;
    std::optional<qint64> lastPresentedFrames;
    qint64 submittedFrames = 0;
    qint64 lastUnderflowFrame = -1;
    QAudio::State outputState = QAudio::StoppedState;
    QAudio::Error outputError = QAudio::NoError;

    struct TimingReply {
        Impl *owner;
        quint64 generation;
        bool done = false;
        std::optional<qint64> frames;
    };

    struct Lock {
        explicit Lock(pa_threaded_mainloop *value) : loop(value) {
            if(loop)pa_threaded_mainloop_lock(loop);
        }
        ~Lock(){if(loop)pa_threaded_mainloop_unlock(loop);}
        pa_threaded_mainloop *loop;
    };

    Impl(SynthStream &input,int rate,qsizetype bufferBytes,QByteArray sinkId)
        : source(input),sampleRate(rate),deviceId(std::move(sinkId)),
          requestedBufferBytes(std::clamp<qsizetype>(bufferBytes,frameBytes,
              qsizetype(std::numeric_limits<uint32_t>::max())-frameBytes)),
          actualBufferBytes(requestedBufferBytes) {
        requestedBufferBytes-=requestedBufferBytes%frameBytes;
        actualBufferBytes=requestedBufferBytes;
        pa_sample_spec spec{PA_SAMPLE_FLOAT32NE,uint32_t(sampleRate),2};
        if(sampleRate<=0||!pa_sample_spec_valid(&spec)){fail(QAudio::OpenError);return;}
        loop=pa_threaded_mainloop_new();
        if(!loop){fail(QAudio::OpenError);return;}
        pa_threaded_mainloop_set_name(loop,"VArpeggio PCM");
        pa_proplist *properties=pa_proplist_new();
        if(properties){
            pa_proplist_sets(properties,PA_PROP_APPLICATION_NAME,"VArpeggio");
            pa_proplist_sets(properties,PA_PROP_APPLICATION_ID,"com.voncloft.VArpeggio");
            pa_proplist_sets(properties,PA_PROP_MEDIA_ROLE,"music");
        }
        context=pa_context_new_with_proplist(pa_threaded_mainloop_get_api(loop),
                                           "VArpeggio",properties);
        if(properties)pa_proplist_free(properties);
        if(!context){fail(QAudio::OpenError);return;}
        pa_context_set_state_callback(context,&Impl::contextStateChanged,this);
        if(pa_threaded_mainloop_start(loop)<0){fail(QAudio::OpenError);return;}
        loopRunning=true;
        Lock lock(loop);
        if(pa_context_connect(context,nullptr,PA_CONTEXT_NOAUTOSPAWN,nullptr)<0){
            contextFailed=true;fail(QAudio::OpenError);return;
        }
        pa_time_event *timeout=makeTimeout(3000);
        while(!waitTimedOut&&PA_CONTEXT_IS_GOOD(pa_context_get_state(context))
              &&pa_context_get_state(context)!=PA_CONTEXT_READY)
            pa_threaded_mainloop_wait(loop);
        freeTimeout(timeout);
        if(waitTimedOut||pa_context_get_state(context)!=PA_CONTEXT_READY){
            contextFailed=true;fail(QAudio::OpenError);
        }
    }

    ~Impl(){
        if(!loop)return;
        {
            Lock lock(loopRunning?loop:nullptr);
            resetStream();
            if(context){
                pa_context_set_state_callback(context,nullptr,nullptr);
                pa_context_disconnect(context);
                pa_context_unref(context);
                context=nullptr;
            }
        }
        // PulseAudio explicitly requires stop() outside its mainloop lock.
        // It joins the native callback thread before the source can die.
        if(loopRunning)pa_threaded_mainloop_stop(loop);
        pa_threaded_mainloop_free(loop);
    }

    void fail(QAudio::Error error){
        streamFailed=true;outputState=QAudio::StoppedState;outputError=error;
    }

    void signal(){if(loop)pa_threaded_mainloop_signal(loop,0);}

    static void contextStateChanged(pa_context *value,void *userdata){
        auto &self=*static_cast<Impl*>(userdata);
        const auto state=pa_context_get_state(value);
        if(state==PA_CONTEXT_FAILED||state==PA_CONTEXT_TERMINATED){
            self.contextFailed=true;self.fail(QAudio::IOError);
        }
        self.signal();
    }

    static void streamStateChanged(pa_stream *value,void *userdata){
        auto &self=*static_cast<Impl*>(userdata);
        switch(pa_stream_get_state(value)){
        case PA_STREAM_READY:
            self.updateBufferSize();
            if(!self.streamFailed)self.outputState=QAudio::IdleState;
            break;
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:self.fail(QAudio::IOError);break;
        default:break;
        }
        self.signal();
    }

    void updateBufferSize(){
        if(stream)if(const auto *attributes=pa_stream_get_buffer_attr(stream)){
            // With zero prebuffer, an underflow may run the read index beyond
            // future writes and silently drop notes. A continuous score must
            // rebuffer losslessly instead; fail rather than accept that mode.
            if(attributes->prebuf==0){fail(QAudio::IOError);return;}
            if(attributes->tlength!=uint32_t(-1)&&attributes->tlength>=frameBytes)
                actualBufferBytes=qsizetype(attributes->tlength)
                    -qsizetype(attributes->tlength)%frameBytes;
        }
    }

    static void bufferChanged(pa_stream*,void *userdata){
        auto &self=*static_cast<Impl*>(userdata);
        self.updateBufferSize();self.signal();
    }

    static void streamUnderflow(pa_stream *value,void *userdata){
        auto &self=*static_cast<Impl*>(userdata);
        if(self.streamFailed)return;
        self.underflow=true;
        const qint64 index=pa_stream_get_underflow_index(value);
        self.lastUnderflowFrame=index<0?-1:index/frameBytes;
        self.outputState=QAudio::IdleState;
        self.outputError=QAudio::UnderrunError;
        self.signal();
    }

    static void streamStarted(pa_stream*,void *userdata){
        auto &self=*static_cast<Impl*>(userdata);
        if(self.streamFailed)return;
        self.underflow=false;
        self.outputState=QAudio::ActiveState;
        self.outputError=QAudio::NoError;
        self.signal();
    }

    static void streamSuspended(pa_stream *value,void *userdata){
        auto &self=*static_cast<Impl*>(userdata);
        if(self.streamFailed)return;
        self.outputState=pa_stream_is_suspended(value)>0?QAudio::SuspendedState
            :self.underflow?QAudio::IdleState:QAudio::ActiveState;
        self.signal();
    }

    static void writeRequested(pa_stream *value,size_t requested,void *userdata){
        auto &self=*static_cast<Impl*>(userdata);
        if(self.streamFailed||value!=self.stream)return;
        if(requested%size_t(frameBytes)!=0){self.fail(QAudio::IOError);self.signal();return;}
        // The native callback already holds the mainloop lock. It renders only
        // the memory actually granted by begin_write, then submits ALL of it.
        // Never render a large block and silently drop its unwritten tail.
        const size_t maximumChunk=size_t(std::max(1,self.sampleRate/50))*size_t(frameBytes);
        while(requested>0){
            size_t bytes=std::min(requested,maximumChunk);
            void *data=nullptr;
            if(pa_stream_begin_write(value,&data,&bytes)<0||!data){
                self.fail(QAudio::IOError);self.signal();return;
            }
            if(bytes==0||bytes%size_t(frameBytes)!=0){
                pa_stream_cancel_write(value);
                self.fail(QAudio::IOError);self.signal();return;
            }
            const qint64 rendered=self.source.readData(static_cast<char*>(data),qint64(bytes));
            if(rendered!=qint64(bytes)){
                pa_stream_cancel_write(value);
                self.fail(QAudio::IOError);self.signal();return;
            }
            if(pa_stream_write(value,data,bytes,nullptr,0,PA_SEEK_RELATIVE)<0){
                self.fail(QAudio::IOError);self.signal();return;
            }
            self.submittedFrames+=qint64(bytes/size_t(frameBytes));
            requested-=bytes;
        }
    }

    static void timedOut(pa_mainloop_api*,pa_time_event*,const timeval*,void *userdata){
        auto &self=*static_cast<Impl*>(userdata);
        self.waitTimedOut=true;self.signal();
    }

    pa_time_event *makeTimeout(int milliseconds){
        waitTimedOut=false;
        auto *timer=pa_context_rttime_new(context,
            pa_rtclock_now()+pa_usec_t(milliseconds)*1000,&Impl::timedOut,this);
        if(!timer)waitTimedOut=true;
        return timer;
    }

    void freeTimeout(pa_time_event *timer){
        if(timer)pa_threaded_mainloop_get_api(loop)->time_free(timer);
    }

    bool contextReady() const{
        return context&&!contextFailed&&pa_context_get_state(context)==PA_CONTEXT_READY;
    }

    void resetStream(){
        // Called with the native lock held. Disabling every callback and
        // disconnecting synchronously excludes any further source reads.
        if(stream){
            pa_stream_set_write_callback(stream,nullptr,nullptr);
            pa_stream_set_state_callback(stream,nullptr,nullptr);
            pa_stream_set_underflow_callback(stream,nullptr,nullptr);
            pa_stream_set_started_callback(stream,nullptr,nullptr);
            pa_stream_set_suspended_callback(stream,nullptr,nullptr);
            pa_stream_set_buffer_attr_callback(stream,nullptr,nullptr);
            pa_stream_disconnect(stream);pa_stream_unref(stream);stream=nullptr;
        }
        ++streamGeneration;
        submittedFrames=0;lastUnderflowFrame=-1;
        lastPresentedFrames.reset();
        underflow=false;outputState=QAudio::StoppedState;
        if(!contextFailed){streamFailed=false;outputError=QAudio::NoError;}
    }

    bool start(){
        if(!contextReady())return false;
        resetStream();
        const pa_sample_spec spec{PA_SAMPLE_FLOAT32NE,uint32_t(sampleRate),2};
        stream=pa_stream_new(context,"VArpeggio piano",&spec,nullptr);
        if(!stream){fail(QAudio::OpenError);return false;}
        pa_stream_set_state_callback(stream,&Impl::streamStateChanged,this);
        pa_stream_set_write_callback(stream,&Impl::writeRequested,this);
        pa_stream_set_underflow_callback(stream,&Impl::streamUnderflow,this);
        pa_stream_set_started_callback(stream,&Impl::streamStarted,this);
        pa_stream_set_suspended_callback(stream,&Impl::streamSuspended,this);
        pa_stream_set_buffer_attr_callback(stream,&Impl::bufferChanged,this);
        const pa_buffer_attr attributes{uint32_t(-1),uint32_t(requestedBufferBytes),
                                       uint32_t(-1),uint32_t(-1),uint32_t(-1)};
        // No interpolator or monotonic smoother: timing queries explicitly
        // request a fresh server/device snapshot before reading get_time().
        const auto flags=pa_stream_flags_t(PA_STREAM_AUTO_TIMING_UPDATE
                         |PA_STREAM_ADJUST_LATENCY|PA_STREAM_NOT_MONOTONIC);
        const char *sink=deviceId.isEmpty()?nullptr:deviceId.constData();
        if(pa_stream_connect_playback(stream,sink,&attributes,flags,nullptr,nullptr)<0){
            fail(QAudio::OpenError);return false;
        }
        pa_time_event *timeout=makeTimeout(3000);
        while(!waitTimedOut&&!contextFailed&&!streamFailed
              &&PA_STREAM_IS_GOOD(pa_stream_get_state(stream))
              &&pa_stream_get_state(stream)!=PA_STREAM_READY)
            pa_threaded_mainloop_wait(loop);
        freeTimeout(timeout);
        if(waitTimedOut||contextFailed||streamFailed
            ||pa_stream_get_state(stream)!=PA_STREAM_READY){
            fail(QAudio::OpenError);return false;
        }
        updateBufferSize();
        return !streamFailed;
    }

    static void timingUpdated(pa_stream *value,int success,void *userdata){
        auto &reply=*static_cast<TimingReply*>(userdata);
        auto &self=*reply.owner;
        reply.done=true;reply.frames.reset();
        if(success&&reply.generation==self.streamGeneration&&value==self.stream
            &&!self.contextFailed&&!self.streamFailed){
            // Suspension can arrive while the fresh timing request is in
            // flight. Do not read its corked/suspended raw-index clock.
            if(pa_stream_is_suspended(value)>0||pa_stream_is_corked(value)>0){
                reply.frames=self.lastPresentedFrames;self.signal();return;
            }
            const auto *timing=pa_stream_get_timing_info(value);
            pa_usec_t usecs=0;
            if(timing&&!timing->read_index_corrupt&&!timing->write_index_corrupt
                &&pa_stream_get_time(value,&usecs)>=0){
                const long double frames=static_cast<long double>(usecs)*self.sampleRate/1000000.0L;
                reply.frames=std::clamp<qint64>(
                    frames>=self.submittedFrames?self.submittedFrames:qint64(frames),
                    0,self.submittedFrames);
            }
        }
        self.signal();
    }

    std::optional<qint64> processedFrames(){
        if(!contextReady()||!stream||streamFailed
            ||pa_stream_get_state(stream)!=PA_STREAM_READY)return {};
        // get_time omits sink latency when suspended/corked; querying it in
        // that state would jump to queued, not presented, samples instead.
        if(pa_stream_is_suspended(stream)>0||pa_stream_is_corked(stream)>0)
            return lastPresentedFrames;
        auto reply=std::make_unique<TimingReply>();
        reply->owner=this;reply->generation=streamGeneration;
        pa_operation *operation=pa_stream_update_timing_info(stream,&Impl::timingUpdated,reply.get());
        if(!operation)return {};
        pa_time_event *timeout=makeTimeout(100);
        while(!reply->done&&!waitTimedOut&&!contextFailed&&!streamFailed
              &&reply->generation==streamGeneration
              &&pa_operation_get_state(operation)==PA_OPERATION_RUNNING)
            pa_threaded_mainloop_wait(loop);
        freeTimeout(timeout);
        if(pa_operation_get_state(operation)==PA_OPERATION_RUNNING)
            pa_operation_cancel(operation);
        pa_operation_unref(operation);
        // cancel() suppresses the operation's user callback. The heap-owned
        // reply remains alive until cancellation/unref has completed under
        // the native lock; no delayed callback retains a stack address.
        if(waitTimedOut||!reply->done||reply->generation!=streamGeneration
            ||contextFailed||streamFailed)return {};
        if(reply->frames)lastPresentedFrames=reply->frames;
        return reply->frames;
    }
};

PulseAudioOutput::PulseAudioOutput(SynthStream &source,int sampleRate,
                                 qsizetype requestedBufferBytes,QByteArray deviceId)
    :m_impl(std::make_unique<Impl>(source,sampleRate,requestedBufferBytes,std::move(deviceId))){}
PulseAudioOutput::~PulseAudioOutput()=default;

bool PulseAudioOutput::available() const{
    Impl::Lock lock(m_impl->loopRunning?m_impl->loop:nullptr);
    return m_impl->contextReady();
}
bool PulseAudioOutput::start(){
    Impl::Lock lock(m_impl->loopRunning?m_impl->loop:nullptr);
    return m_impl->start();
}
void PulseAudioOutput::reset(){
    Impl::Lock lock(m_impl->loopRunning?m_impl->loop:nullptr);
    m_impl->resetStream();
}
qsizetype PulseAudioOutput::bufferSize() const{
    Impl::Lock lock(m_impl->loopRunning?m_impl->loop:nullptr);
    return m_impl->actualBufferBytes;
}
std::optional<qint64> PulseAudioOutput::processedFrames() const{
    Impl::Lock lock(m_impl->loopRunning?m_impl->loop:nullptr);
    return m_impl->processedFrames();
}
bool PulseAudioOutput::failed() const{
    Impl::Lock lock(m_impl->loopRunning?m_impl->loop:nullptr);
    return m_impl->contextFailed||m_impl->streamFailed;
}
QAudio::State PulseAudioOutput::state() const{
    Impl::Lock lock(m_impl->loopRunning?m_impl->loop:nullptr);
    return m_impl->outputState;
}
QAudio::Error PulseAudioOutput::error() const{
    Impl::Lock lock(m_impl->loopRunning?m_impl->loop:nullptr);
    return m_impl->outputError;
}
