#pragma once

#include "musicdocument.h"

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QWidget>

class ScoreWidget : public QWidget {
    Q_OBJECT
public:
    explicit ScoreWidget(QWidget *parent = nullptr);
    void setNotes(const QVector<NoteEvent> &notes);
    void setExpected(const QSet<int> &notes, double groupStart);
    void setPlayedBeat(double beat);
    void setPlaybackCursorVisible(bool visible);
    void setPlaybackWaiting(double beat);
    void setFollowBeat(double beat);
    void setLiveWritingMode(bool live);
    void setEditEnabled(bool enabled);
    void setSeekEnabled(bool enabled);
    void setEditCursorBeat(double beat);
    void setTimeSignatures(const QVector<TimeSignatureEvent> &signatures);
    void setKeySignatures(const QVector<KeySignatureEvent> &signatures);
    void setRests(const QVector<StaffRestEvent> &rests);
    void setRepeatEnds(const QVector<double> &beats);
    void setPedalEvents(const QVector<PedalEvent> &events);
    void setExpectedPedal(int state, double beat);
    void setShowNames(bool show);
    void setShowFingering(bool show);
    void setLessonOpeningInset(bool enabled);
    void setHandFilter(int filter);
    void setDrillRange(double startBeat, double endBeat);
    void setDrillRanges(const QVector<QPair<double,double>> &ranges);
    QPair<double,double> measureRangeAt(double beat) const;
    double measureEndAt(double beat) const;
    int measureNumberAt(double beat) const;
    double viewStartBeat() const;
    double viewWindowBeats() const;
    void holdViewStart(double beat);
    void paintScore(QPainter &p, const QRectF &target) const;
    int recommendedFrameHeight() const;
    const QVector<NoteEvent> &notes() const { return m_notes; }
    QString noteNameForBeat(int midi, double beat,
                            AccidentalMark accidental = AccidentalMark::Automatic) const;

signals:
    void editCursorChanged(double beat);
    void noteDeleteRequested(int noteIndex);
    void noteContextRequested(int noteIndex, const QPoint &globalPosition);
    void restDeleteRequested(int restIndex);
    void staffContextRequested(double beat, const QPoint &globalPosition);
    void seekPreviewChanged(double beat);
    void seekRequested(double beat);
    void historyStepRequested(int steps);
    void measureDrillCancelRequested();
    void frameHeightHintChanged(int height);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QPair<double,double> viewRange() const;
    QRectF canvasViewport(const QRectF &target) const;
    QPointF widgetToCanvas(const QPointF &point) const;
    QVector<NoteEvent> m_notes;
    QVector<TimeSignatureEvent> m_timeSignatures{{0.0,4,4}};
    QVector<KeySignatureEvent> m_keySignatures{{0.0,0}};
    QVector<StaffRestEvent> m_rests;
    QVector<double> m_repeatEnds;
    QVector<PedalEvent> m_pedalEvents;
    int m_expectedPedal = -1;
    double m_expectedPedalBeat = -1;
    QSet<int> m_expected;
    double m_groupStart = 0;
    double m_playedBeat = -1;
    bool m_playbackCursorVisible = true;
    double m_playbackWaitingBeat = -1.0;
    double m_followBeat = 0;
    double m_viewStartOverride = -1;
    bool m_showNames = false;
    bool m_showFingering = false;
    bool m_lessonOpeningInset = false;
    bool m_liveWriting = false;
    bool m_editEnabled = false;
    bool m_seekEnabled = true;
    bool m_seekDragging = false;
    double m_seekPreviewBeat = -1;
    double m_seekRangeStart = 0;
    double m_seekRangeWindow = 8;
    double m_editCursorBeat = -1;
    QVector<QPair<double,double>> m_drillRanges;
    int m_handFilter = -1;
};

class PianoWidget : public QWidget {
    Q_OBJECT
public:
    explicit PianoWidget(QWidget *parent = nullptr);
    void setActive(int note, bool active);
    void setFingerHints(const QHash<int,int> &hints);
    void setKeyRange(int low, int high);
    QSize sizeHint() const override { return {900, 168}; }

signals:
    void notePressed(int note, int velocity);
    void noteReleased(int note);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;

private:
    int noteAt(const QPointF &point) const;
    QRectF noteRect(int note) const;
    QSet<int> m_active;
    QHash<int,int> m_fingerHints;
    int m_mouseNote = -1;
    int m_low = 36, m_high = 96;
};

class BubbleWidget : public QWidget {
    Q_OBJECT
public:
    explicit BubbleWidget(QWidget *parent = nullptr);
    void setNotes(const QVector<NoteEvent> &notes);
    void setPlayhead(double beat);
    void setExpected(const QSet<int> &notes, double groupStart);
    void setBubblesVisible(bool visible);
    void setTimelineVisible(bool visible);
    void setWaitMode(bool waiting);
    void setHandFilter(int filter);
    void pulseNote(int note);
    void resetState();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    struct Pulse { int note; qint64 born; };
    QVector<NoteEvent> m_notes;
    QVector<Pulse> m_pulses;
    QSet<int> m_expected;
    double m_playhead = 0, m_groupStart = 0;
    bool m_showBubbles = true;
    bool m_timelineVisible = true;
    bool m_waitMode = true;
    int m_handFilter = -1;
    QElapsedTimer m_clock;
    QTimer m_timer;
};
