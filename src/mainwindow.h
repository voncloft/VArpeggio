#pragma once

#include "midiinput.h"
#include "musicdocument.h"
#include "musicwidgets.h"
#include "synthesizer.h"

#include <QElapsedTimer>
#include <QDate>
#include <QHash>
#include <QMainWindow>
#include <QMap>
#include <QPdfDocument>
#include <QSet>
#include <QTimer>
#include <memory>

class QButtonGroup;
class QAudioOutput;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QMediaPlayer;
class QPushButton;
class QScrollBar;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QPdfView;
class QTemporaryDir;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void openPath(const QString &path);
    bool transcribeVideoPath(const QString &path, int tempo = 0);
    void setImportedMediaSource(const QString &path, double scoreStartSeconds = 0.0);
    void showLiveScore();
    void startListening();
    static bool saveFallingNoteVideoFile(const QString &path, const QVector<NoteEvent> &notes,
                                         int tempo, const QString &title,
                                         SynthInstrument instrument, QWidget *parent,
                                         QString *error);
    static bool saveDetectorVideoFile(const QString &path, const QString &source,
                                      const QVector<NoteEvent> &notes, int tempo,
                                      double sourceStart, const TranscriptionInfo &geometry,
                                      QWidget *parent, QString *error);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum class Mode { Wait, Listen, Follow, Free };
    enum class TranscriptionMode { Automatic, VisualOnly, AudioOnly };
    struct PracticeGroup { double start; QSet<int> notes; };

    QWidget *buildHeader();
    QWidget *buildSidebar();
    QWidget *buildWorkspace();
    QWidget *makeCard(const QString &objectName = {});
    void buildMenus();
    void applyStyle();
    void connectUi();

    void setMode(Mode mode);
    void setSong(const QString &title, const QVector<NoteEvent> &notes, int tempo = 96);
    void setHandFilter(int filter);
    void rebuildPractice();
    void updateExpected();
    void updateStats();
    void updateSongMeta();
    void changeTempo(int tempo);
    void updatePrimaryButton();
    void setExactScoreAvailable(const QString &label = QStringLiteral("Exact PDF"));
    QVector<NoteEvent> filteredNotes() const;

    void userNoteOn(int note, int velocity);
    void userNoteOff(int note);
    void playbackNoteOn(int index);
    void playbackNoteOff(int index);
    void togglePlayback();
    void pausePlayback();
    void seekPlayback(double beat);
    void stopPlayback(bool rewind = false);
    bool useImportedMediaAudio() const;
    void clearImportedMediaSource();
    void updateClocks();
    void toggleRecord();
    void finishRecording();
    void clearFreePlayStaff();
    void refreshFreePlayStaff();
    void editFreePlayStaff();
    void updateFreePlayHistory(bool followLatest);
    void syncFreePlayScore();
    void setFreePlayGrouping(bool grouped, double beat = -1.0);

    void importScore();
    void openScore(const QString &path);
    void exportScore();
    void exportTo(const QString &type, const QString &path);
    void openVideoLab();
    void openDetectorView();
    bool transcribeMediaPath(const QString &path, TranscriptionMode mode, int tempo);
    void openRoutineDialog();
    void loadRoutineStep(int step);
    void finishRoutinePlan();
    void startEarTrainingRound();
    void playEarTrainingPrompt();
    void showNotice(const QString &text, bool warning = false);
    void toast(const QString &text);

    Mode m_mode = Mode::Wait;
    QVector<NoteEvent> m_notes;
    QString m_title = "Morning Light";
    int m_tempo = 96;
    int m_handFilter = -1;
    bool m_playing = false;
    bool m_playbackPaused = false;
    bool m_usingImportedMediaAudio = false;
    bool m_recording = false;
    bool m_sustain = false;
    bool m_earTraining = false;
    bool m_freePlayApplied = false;
    bool m_freePlayAutoFollow = true;
    bool m_freePlayGroupEntry = false;
    double m_freePlayEditBeat = -1.0;
    double m_freePlayEditAdvance = 0.0;
    int m_routineVariation = 0;
    int m_routineStep = -1;
    QDate m_routinePlanDate;
    int m_earTrainingRound = 0;
    int m_earTrainingSolved = 0;

    QVector<PracticeGroup> m_groups;
    int m_groupIndex = 0;
    QSet<int> m_matched;
    int m_attempts = 0, m_correct = 0, m_streak = 0, m_bestStreak = 0;
    QSet<int> m_heldNotes;
    QSet<int> m_sustainedNotes;
    QSet<int> m_startedEvents;
    QSet<int> m_endedEvents;
    QSet<int> m_earExpected;
    QHash<int,int> m_midiOutputNotes;
    QSet<int> m_liveMidiOutputNotes;
    QHash<int,double> m_recordStarts;
    QHash<int,double> m_freePlayStarts;
    QHash<int,int> m_freePlayIndexes;
    QHash<int,qint64> m_freePlayPressMs;
    QHash<int,qint64> m_freePlayEditPressMs;
    QHash<int,quint64> m_userVoiceIds;
    QVector<NoteEvent> m_recorded;
    QVector<NoteEvent> m_freePlayNotes;
    QVector<TimeSignatureEvent> m_freePlayTimeSignatures{{0.0,4,4}};
    QVector<KeySignatureEvent> m_freePlayKeySignatures{{0.0,0}};
    QVector<StaffRestEvent> m_freePlayRests;
    QVector<double> m_freePlayRepeatEnds;
    QVector<NoteEvent> m_earPrompt;
    QMap<int,int> m_keyMap;

    Synthesizer m_synth;
    MidiInput m_midi;
    MidiOutput m_midiOutput;
    QTimer m_frameTimer;
    QElapsedTimer m_playClock, m_recordClock, m_freePlayClock, m_metroClock;
    double m_freePlayOffsetBeat = 0.0;
    double m_playbackOffsetSeconds = 0.0;
    int m_lastMetroBeat = -1;
    QString m_importedMediaPath;
    qint64 m_importedMediaStartMs = 0;
    int m_importedMediaTempo = 0;
    TranscriptionInfo m_videoDetectionInfo;

    QLabel *m_midiLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_noteCountLabel = nullptr;
    QLabel *m_durationLabel = nullptr;
    QSpinBox *m_tempoControl = nullptr;
    QLabel *m_nextNoteLabel = nullptr;
    QLabel *m_coachLabel = nullptr;
    QLabel *m_accuracyLabel = nullptr;
    QLabel *m_streakLabel = nullptr;
    QLabel *m_progressLabel = nullptr;
    QLabel *m_liveNoteLabel = nullptr;
    QLabel *m_barLabel = nullptr;
    QLabel *m_volumeLabel = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QPushButton *m_playButton = nullptr;
    QPushButton *m_recordButton = nullptr;
    QPushButton *m_freeEditButton = nullptr;
    QPushButton *m_freeClearButton = nullptr;
    QWidget *m_scoreHistoryRow = nullptr;
    QScrollBar *m_scoreHistoryBar = nullptr;
    QFrame *m_routineSession = nullptr;
    QLabel *m_routineStepLabel = nullptr;
    QLabel *m_routineDetailLabel = nullptr;
    QPushButton *m_routinePreviousButton = nullptr;
    QPushButton *m_routineNextButton = nullptr;
    QPushButton *m_midiButton = nullptr;
    QPushButton *m_noticeClose = nullptr;
    QPushButton *m_scoreViewButton = nullptr;
    QPushButton *m_liveScoreButton = nullptr;
    QWidget *m_notice = nullptr;
    QLabel *m_noticeText = nullptr;
    QComboBox *m_exportCombo = nullptr;
    QComboBox *m_instrumentCombo = nullptr;
    QComboBox *m_keyboardToneCombo = nullptr;
    QCheckBox *m_sourceAudio = nullptr;
    QPushButton *m_detectorButton = nullptr;
    QCheckBox *m_noteNames = nullptr;
    QCheckBox *m_bubbles = nullptr;
    QCheckBox *m_metronome = nullptr;
    QCheckBox *m_sound = nullptr;
    QCheckBox *m_midiOutputCheck = nullptr;
    QSlider *m_volume = nullptr;
    QMediaPlayer *m_sourcePlayer = nullptr;
    QAudioOutput *m_sourceAudioOutput = nullptr;
    QButtonGroup *m_modeButtons = nullptr;
    QButtonGroup *m_handButtons = nullptr;
    QButtonGroup *m_scoreDisplayButtons = nullptr;
    ScoreWidget *m_score = nullptr;
    PianoWidget *m_piano = nullptr;
    BubbleWidget *m_bubble = nullptr;
    QStackedWidget *m_scoreStack = nullptr;
    QPdfDocument *m_pdfDocument = nullptr;
    QPdfView *m_pdfView = nullptr;
    std::unique_ptr<QTemporaryDir> m_generatedScoreDir;
};
