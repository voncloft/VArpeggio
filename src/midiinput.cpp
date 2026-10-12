#include "midiinput.h"

#include <alsa/asoundlib.h>
#include <QSet>
#include <QVector>

namespace {
struct RawDevice {
    QByteArray address;
    QString displayName;
};

QVector<RawDevice> rawMidiDevices(snd_rawmidi_stream_t stream){
    QVector<RawDevice> devices;
    int card=-1;
    if(snd_card_next(&card)<0)return devices;
    while(card>=0){
        snd_ctl_t *control=nullptr;
        const QByteArray cardAddress="hw:"+QByteArray::number(card);
        if(snd_ctl_open(&control,cardAddress.constData(),0)>=0){
            int device=-1;
            while(snd_ctl_rawmidi_next_device(control,&device)>=0&&device>=0){
                snd_rawmidi_info_t *info;
                snd_rawmidi_info_alloca(&info);
                snd_rawmidi_info_set_device(info,unsigned(device));
                snd_rawmidi_info_set_subdevice(info,0);
                snd_rawmidi_info_set_stream(info,stream);
                if(snd_ctl_rawmidi_info(control,info)<0)continue;
                const unsigned subdevices=qMax(1u,snd_rawmidi_info_get_subdevices_count(info));
                for(unsigned subdevice=0;subdevice<subdevices;++subdevice){
                    snd_rawmidi_info_set_subdevice(info,subdevice);
                    if(snd_ctl_rawmidi_info(control,info)<0)continue;
                    const QByteArray address="hw:"+QByteArray::number(card)+','+QByteArray::number(device)+','+QByteArray::number(subdevice);
                    QString name=QString::fromLocal8Bit(snd_rawmidi_info_get_name(info));
                    const QString port=QString::fromLocal8Bit(snd_rawmidi_info_get_subdevice_name(info));
                    if(!port.isEmpty())name+=" — "+port;
                    if(name.isEmpty())name=QString("MIDI %1:%2").arg(card).arg(device);
                    devices.append({address,name});
                }
            }
            snd_ctl_close(control);
        }
        if(snd_card_next(&card)<0)break;
    }
    return devices;
}

int midiDataBytes(quint8 status){
    switch(status&0xf0){
    case 0x80:case 0x90:case 0xa0:case 0xb0:case 0xe0:return 2;
    case 0xc0:case 0xd0:return 1;
    default:return 0;
    }
}

QVector<RawDevice> rawMidiInputs(){return rawMidiDevices(SND_RAWMIDI_STREAM_INPUT);}
}

MidiInput::MidiInput(QObject *parent) : QThread(parent) {}
MidiInput::~MidiInput(){stop();wait(1500);}
void MidiInput::stop(){m_running=false;}
QStringList MidiInput::connectedDevices() const {QMutexLocker lock(&m_mutex);return m_devices;}

void MidiInput::run(){
    snd_seq_t *seq=nullptr;
    if(snd_seq_open(&seq,"default",SND_SEQ_OPEN_INPUT,SND_SEQ_NONBLOCK)<0){
        // Minimal systems do not always load snd-seq. USB adapters still
        // expose a standard ALSA raw-MIDI input, so use it directly instead
        // of requiring the user to run a privileged module command.
        struct OpenRawDevice {snd_rawmidi_t *handle=nullptr;QString name;quint8 status=0;quint8 data[2]{};int have=0;};
        QVector<OpenRawDevice> inputs;
        QStringList names;
        for(const auto &device:rawMidiInputs()){
            snd_rawmidi_t *input=nullptr;
            if(snd_rawmidi_open(&input,nullptr,device.address.constData(),SND_RAWMIDI_NONBLOCK)<0)continue;
            inputs.append({input,device.displayName});names.append(device.displayName);
        }
        {QMutexLocker lock(&m_mutex);m_devices=names;}
        emit devicesChanged(names);
        if(inputs.isEmpty()){emit midiError("No readable ALSA MIDI input was found.");return;}
        m_running=true;
        while(m_running){
            for(auto &input:inputs){
                quint8 bytes[64];
                const int count=snd_rawmidi_read(input.handle,bytes,sizeof(bytes));
                if(count<=0)continue;
                for(int index=0;index<count;++index){
                    const quint8 byte=bytes[index];
                    if(byte>=0xf8)continue; // real-time messages do not disturb running status
                    if(byte&0x80){
                        if(byte>=0xf0){input.status=0;input.have=0;continue;}
                        input.status=byte;input.have=0;continue;
                    }
                    const int needed=midiDataBytes(input.status);
                    if(!needed)continue;
                    input.data[input.have++]=byte;
                    if(input.have<needed)continue;
                    const quint8 command=input.status&0xf0;
                    if(command==0x90){if(input.data[1])emit noteOn(input.data[0],input.data[1]);else emit noteOff(input.data[0]);}
                    else if(command==0x80)emit noteOff(input.data[0]);
                    else if(command==0xb0&&input.data[0]==64)emit sustain(input.data[1]>=64);
                    else if(command==0xc0)emit programChanged(input.data[0]);
                    input.have=0;
                }
            }
            msleep(2);
        }
        for(auto &input:inputs)snd_rawmidi_close(input.handle);
        return;
    }
    snd_seq_set_client_name(seq,"Keyflow MIDI Studio");int port=snd_seq_create_simple_port(seq,"Keyboard input",SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,SND_SEQ_PORT_TYPE_APPLICATION);if(port<0){snd_seq_close(seq);emit midiError("Could not create the MIDI input port.");return;}
    m_running=true;QSet<QString>connected;auto scan=[&](){QStringList names;snd_seq_client_info_t*cinfo;snd_seq_port_info_t*pinfo;snd_seq_client_info_alloca(&cinfo);snd_seq_port_info_alloca(&pinfo);snd_seq_client_info_set_client(cinfo,-1);while(snd_seq_query_next_client(seq,cinfo)>=0){int client=snd_seq_client_info_get_client(cinfo);if(client==snd_seq_client_id(seq))continue;snd_seq_port_info_set_client(pinfo,client);snd_seq_port_info_set_port(pinfo,-1);while(snd_seq_query_next_port(seq,pinfo)>=0){unsigned cap=snd_seq_port_info_get_capability(pinfo);if((cap&SND_SEQ_PORT_CAP_READ)&&(cap&SND_SEQ_PORT_CAP_SUBS_READ)){int source=snd_seq_port_info_get_port(pinfo);QString key=QString::number(client)+":"+QString::number(source);QString name=QString::fromLocal8Bit(snd_seq_client_info_get_name(cinfo))+" — "+QString::fromLocal8Bit(snd_seq_port_info_get_name(pinfo));names<<name;if(!connected.contains(key)&&snd_seq_connect_from(seq,port,client,source)>=0)connected.insert(key);}}}names.removeDuplicates();names.sort();{QMutexLocker lock(&m_mutex);if(names!=m_devices){m_devices=names;emit devicesChanged(names);}}};
    int counter=0;scan();while(m_running){snd_seq_event_t*ev=nullptr;while(snd_seq_event_input(seq,&ev)>=0&&ev){switch(ev->type){case SND_SEQ_EVENT_NOTEON:if(ev->data.note.velocity)emit noteOn(ev->data.note.note,ev->data.note.velocity);else emit noteOff(ev->data.note.note);break;case SND_SEQ_EVENT_NOTEOFF:emit noteOff(ev->data.note.note);break;case SND_SEQ_EVENT_CONTROLLER:if(ev->data.control.param==64)emit sustain(ev->data.control.value>=64);break;case SND_SEQ_EVENT_PGMCHANGE:emit programChanged(ev->data.control.value);break;default:break;}snd_seq_free_event(ev);ev=nullptr;}if(++counter>=1000){scan();counter=0;}msleep(2);}snd_seq_close(seq);
}

MidiOutput::MidiOutput(QObject *parent):QObject(parent){}
MidiOutput::~MidiOutput(){close();}

QStringList MidiOutput::availableDevices() const {
    QStringList names;
    for(const auto &device:rawMidiDevices(SND_RAWMIDI_STREAM_OUTPUT))names.append(device.displayName);
    return names;
}

QString MidiOutput::activeDevice() const {QMutexLocker lock(&m_mutex);return m_device;}
bool MidiOutput::isOpen() const {QMutexLocker lock(&m_mutex);return m_handle!=nullptr;}

bool MidiOutput::openFirst(QString *error){
    close();
    const auto devices=rawMidiDevices(SND_RAWMIDI_STREAM_OUTPUT);
    if(devices.isEmpty()){if(error)*error="No writable MIDI OUT port was found.";return false;}
    snd_rawmidi_t *output=nullptr;
    const int result=snd_rawmidi_open(nullptr,&output,devices.first().address.constData(),SND_RAWMIDI_NONBLOCK);
    if(result<0||!output){if(error)*error=QString("Could not open %1 for MIDI output: %2").arg(devices.first().displayName,QString::fromLocal8Bit(snd_strerror(result)));return false;}
    {
        QMutexLocker lock(&m_mutex);m_handle=output;m_device=devices.first().displayName;
    }
    // General MIDI program 0 is acoustic grand piano.  The CTK may ignore the
    // program message when its current tone is locked, which is harmless.
    sendMessage(0xc0,0,0,2);
    return true;
}

void MidiOutput::close(){
    QMutexLocker lock(&m_mutex);
    if(!m_handle)return;
    const quint8 sustainOff[]{0xb0,64,0};const quint8 allOff[]{0xb0,123,0};
    snd_rawmidi_write(m_handle,sustainOff,3);snd_rawmidi_write(m_handle,allOff,3);
    snd_rawmidi_drop(m_handle);snd_rawmidi_close(m_handle);m_handle=nullptr;m_device.clear();
}

void MidiOutput::sendMessage(quint8 status,quint8 data1,quint8 data2,int length){
    QMutexLocker lock(&m_mutex);if(!m_handle)return;
    const quint8 message[]{status,data1,data2};
    // Three-byte channel messages fit atomically in the ALSA raw-MIDI buffer.
    // In nonblocking mode EAGAIN simply drops this one message rather than
    // ever freezing the UI/audio clock while a cable is being unplugged.
    snd_rawmidi_write(m_handle,message,length);
}
void MidiOutput::noteOn(int note,int velocity){sendMessage(0x90,quint8(qBound(0,note,127)),quint8(qBound(1,velocity,127)));}
void MidiOutput::noteOff(int note){sendMessage(0x80,quint8(qBound(0,note,127)),0);}
void MidiOutput::sustain(bool down){sendMessage(0xb0,64,down?127:0);}
void MidiOutput::programChange(int program){sendMessage(0xc0,quint8(qBound(0,program,127)),0,2);}
void MidiOutput::channelVolume(int value){sendMessage(0xb0,7,quint8(qBound(0,value,127)));}
void MidiOutput::allNotesOff(){sendMessage(0xb0,64,0);sendMessage(0xb0,123,0);}
