#pragma once

#include <QMutex>
#include <QThread>
#include <QStringList>
#include <atomic>

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
    void devicesChanged(const QStringList &devices);
    void midiError(const QString &message);

protected:
    void run() override;

private:
    std::atomic_bool m_running{false};
    mutable QMutex m_mutex;
    QStringList m_devices;
};

