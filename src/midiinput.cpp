#include "midiinput.h"

#include <alsa/asoundlib.h>
#include <QSet>

MidiInput::MidiInput(QObject *parent) : QThread(parent) {}
MidiInput::~MidiInput(){stop();wait(1500);}
void MidiInput::stop(){m_running=false;}
QStringList MidiInput::connectedDevices() const {QMutexLocker lock(&m_mutex);return m_devices;}

void MidiInput::run(){
    snd_seq_t *seq=nullptr;if(snd_seq_open(&seq,"default",SND_SEQ_OPEN_INPUT,SND_SEQ_NONBLOCK)<0){emit midiError("ALSA MIDI is unavailable.");return;}
    snd_seq_set_client_name(seq,"Keyflow MIDI Studio");int port=snd_seq_create_simple_port(seq,"Keyboard input",SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,SND_SEQ_PORT_TYPE_APPLICATION);if(port<0){snd_seq_close(seq);emit midiError("Could not create the MIDI input port.");return;}
    m_running=true;QSet<QString>connected;auto scan=[&](){QStringList names;snd_seq_client_info_t*cinfo;snd_seq_port_info_t*pinfo;snd_seq_client_info_alloca(&cinfo);snd_seq_port_info_alloca(&pinfo);snd_seq_client_info_set_client(cinfo,-1);while(snd_seq_query_next_client(seq,cinfo)>=0){int client=snd_seq_client_info_get_client(cinfo);if(client==snd_seq_client_id(seq))continue;snd_seq_port_info_set_client(pinfo,client);snd_seq_port_info_set_port(pinfo,-1);while(snd_seq_query_next_port(seq,pinfo)>=0){unsigned cap=snd_seq_port_info_get_capability(pinfo);if((cap&SND_SEQ_PORT_CAP_READ)&&(cap&SND_SEQ_PORT_CAP_SUBS_READ)){int source=snd_seq_port_info_get_port(pinfo);QString key=QString::number(client)+":"+QString::number(source);QString name=QString::fromLocal8Bit(snd_seq_client_info_get_name(cinfo))+" — "+QString::fromLocal8Bit(snd_seq_port_info_get_name(pinfo));names<<name;if(!connected.contains(key)&&snd_seq_connect_from(seq,port,client,source)>=0)connected.insert(key);}}}names.removeDuplicates();names.sort();{QMutexLocker lock(&m_mutex);if(names!=m_devices){m_devices=names;emit devicesChanged(names);}}};
    int counter=0;scan();while(m_running){snd_seq_event_t*ev=nullptr;while(snd_seq_event_input(seq,&ev)>=0&&ev){switch(ev->type){case SND_SEQ_EVENT_NOTEON:if(ev->data.note.velocity)emit noteOn(ev->data.note.note,ev->data.note.velocity);else emit noteOff(ev->data.note.note);break;case SND_SEQ_EVENT_NOTEOFF:emit noteOff(ev->data.note.note);break;case SND_SEQ_EVENT_CONTROLLER:if(ev->data.control.param==64)emit sustain(ev->data.control.value>=64);break;default:break;}snd_seq_free_event(ev);ev=nullptr;}if(++counter>=1000){scan();counter=0;}msleep(2);}snd_seq_close(seq);
}
