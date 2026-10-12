#pragma once

#include <QMutex>
#include <QThread>
#include <QStringList>
#include <atomic>

struct _snd_rawmidi;
typedef struct _snd_rawmidi snd_rawmidi_t;

class MidiInput : public QThread {
    Q_OBJECT
public:
    explicit MidiInput(QObject *parent = nullptr);
    ~MidiInput() override;
    void stop();
    QStringList connectedDevices() const;

signals:
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void sustain(bool down);
    void programChanged(int program);
    void devicesChanged(const QStringList &devices);
    void midiError(const QString &message);

protected:
    void run() override;

private:
    std::atomic_bool m_running{false};
    mutable QMutex m_mutex;
    QStringList m_devices;
};

// Direct ALSA raw-MIDI output.  This intentionally does not depend on
// /dev/snd/seq: inexpensive USB MIDI cables such as the CH345 still expose a
// perfectly usable bidirectional raw port on systems where snd-seq is absent.
class MidiOutput : public QObject {
    Q_OBJECT
public:
    explicit MidiOutput(QObject *parent = nullptr);
    ~MidiOutput() override;
    QStringList availableDevices() const;
    QString activeDevice() const;
    bool openFirst(QString *error = nullptr);
    void close();
    bool isOpen() const;
    void noteOn(int note, int velocity = 96);
    void noteOff(int note);
    void sustain(bool down);
    void programChange(int program);
    void channelVolume(int value);
    void allNotesOff();

private:
    void sendMessage(quint8 status, quint8 data1, quint8 data2 = 0, int length = 3);
    mutable QMutex m_mutex;
    snd_rawmidi_t *m_handle = nullptr;
    QString m_device;
};
