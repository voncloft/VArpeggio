#include "helpmanual.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

#ifndef VARPEGGIO_VERSION
#define VARPEGGIO_VERSION "0.2.45"
#endif

namespace {
struct ManualSection { QString title; QString keywords; QString body; };

QString page(const QString &title,const QString &body){
    return QString(R"HTML(
<html><head><style>
body{color:#18312d;background:#fffefa;margin:22px;line-height:1.45}
h1{color:#173f39;font-size:22pt;margin:0 0 8px 0}
h2{color:#2d7d70;font-size:15pt;margin:20px 0 7px 0}
h3{color:#31534d;font-size:12pt;margin:15px 0 5px 0}
p{margin:6px 0 10px 0}
li{margin:5px 0}
code{color:#7b3f32;background:#f5eee8;padding:2px 4px}
.note{background:#eef7f4;border-left:4px solid #2d7d70;padding:10px 12px;margin:12px 0}
.warning{background:#fff4df;border-left:4px solid #e18a57;padding:10px 12px;margin:12px 0}
table{border-collapse:collapse;width:100%;margin:10px 0}
th{color:#173f39;background:#e7f4ef;text-align:left}
th,td{border:1px solid #cad7d2;padding:7px 9px;vertical-align:top}
</style></head><body><h1>%1</h1>%2</body></html>)HTML").arg(title,body);
}

QVector<ManualSection> manualSections(){
    return {
      {"Welcome and quick start","start first launch overview",
       page("Welcome to VArpeggio",R"HTML(
<p>VArpeggio is a piano practice, notation, recording, and score-conversion studio. You can use the computer keys, click the virtual piano, or connect a MIDI keyboard.</p>
<h2>Fastest way to begin</h2><ol>
<li>Choose <b>Morning Light</b>, <b>Ode to Joy</b>, or <b>Import score</b> in the left sidebar.</li>
<li>Choose <b>Wait mode</b> to learn at your own pace, <b>Listen</b> to hear the notation, <b>Play along</b> to follow the tempo, or <b>Free play</b> to create your own score.</li>
<li>Turn on <b>Note names</b>, <b>Finger numbers</b>, or <b>Falling notes</b> above the Grand Staff when you want extra help.</li>
<li>Use the Right, Left, and Together controls to isolate a hand.</li>
</ol>
<div class="note"><b>Physical MIDI is optional.</b> Every playable function also accepts the on-screen keyboard. The computer keys A through K cover C4 through C5.</div>
)HTML")},
      {"Main window tour","interface header sidebar workspace cards panels",
       page("Main window tour",R"HTML(
<h2>Top bar</h2><ul>
<li><b>Connect MIDI keyboard</b> opens or reconnects the ALSA MIDI input device. Its label shows the connected device.</li>
<li>The <b>?</b> button opens this complete manual. The Help menu also opens it with F1.</li>
</ul>
<h2>Left sidebar</h2><ul>
<li>Select a practice mode, built-in lesson, imported score, Chord Practice, or the Daily Practice Plan.</li>
<li>The pushpin switches between a permanently open sidebar and an icon-only rail. An unpinned rail expands temporarily while the pointer is over it.</li>
</ul>
<h2>Song and transport row</h2><ul>
<li>The title is editable. Undo/restart, Play/Restart, Stop, tempo minus/plus, exact BPM, export type, and Save are grouped here.</li>
<li>If imported media exists, <b>Hear original recording (reference only)</b> compares the source with generated score playback.</li>
</ul>
<h2>Workspace sections</h2><p>The Grand Staff, Bubbles + Live Coach, and Keyboard can each be collapsed. The Earlier–Latest bar reviews score history without stopping playback.</p>
)HTML")},
      {"MIDI connection and routing","midi alsa adapter input output ctk keyboard sound reconnect",
       page("MIDI connection and routing",R"HTML(
<h2>Playing into VArpeggio</h2><p>Connect the adapter's <b>MIDI IN</b> plug to the keyboard's <b>MIDI OUT</b>, then connect USB to the computer. Press <b>Connect MIDI keyboard</b>. Incoming note, velocity, sustain, program, and supported volume messages are used by the app.</p>
<h2>Using the keyboard's own sound</h2><p>Enable <b>Use keyboard MIDI sound</b> to send generated score notes through the adapter's MIDI OUT to the keyboard's MIDI IN. VArpeggio silences its internal piano during routed score playback to avoid doubled sound.</p>
<ul><li>The choice is remembered on close and reconnects when the saved adapter becomes available.</li><li>The tone selector sends General MIDI program changes and follows compatible incoming program changes.</li><li>Stop sends note-off, sustain-off, all-notes-off, all-sound-off, and controller reset messages.</li></ul>
<div class="warning"><b>MIDI is not recorded audio.</b> MIDI carries key and pedal instructions. PC-speaker sound comes from VArpeggio; keyboard-speaker sound requires MIDI OUT routing and a compatible keyboard.</div>
)HTML")},
      {"Practice modes","wait listen play along free play mode",
       page("The four practice modes",R"HTML(
<table><tr><th>Mode</th><th>What it does</th></tr>
<tr><td><b>Wait mode</b></td><td>The notation advances only while the complete highlighted note or chord is held for its written duration. Use this for deliberate practice.</td></tr>
<tr><td><b>Listen</b></td><td>Plays the generated notes represented on the staff. If a source recording is available, the reference switch can play that recording instead.</td></tr>
<tr><td><b>Play along</b></td><td>The score advances at the chosen tempo while your correct and incorrect attacks update coaching and statistics.</td></tr>
<tr><td><b>Free play</b></td><td>Turns your playing into editable notation immediately. Recording is optional; notes played outside recording still enter the staff.</td></tr></table>
<p>Changing modes does not erase the current Free Play project. Only <b>Clear staff</b> removes it.</p>
)HTML")},
      {"Wait mode, penalties, and measure drills","wrong note penalty restart measure drill select highlight clean retry sustain pedal ped",
       page("Wait mode, penalties, and measure drills",R"HTML(
<h2>Duration-aware practice</h2><ul><li>Hold every highlighted chord tone for the written length.</li><li>A chord must match the complete written key set. A brief 500 ms entry window lets human fingers land, but missing a required tone, releasing any chord key early, or adding an extra tone fails the attempt and restarts the measure.</li><li>A repeated pitch requires a real release and new attack unless the notation is a tie.</li><li>Releasing a single note early pauses the score and tells you which key must be played again.</li></ul>
<h2>Wrong-note penalty</h2><p>A wrong note immediately kills that attempt: the streak resets, accuracy records the miss, partial chord credit is cleared, and the playhead returns to the first playable attack of the <b>current measure only</b>. Release the wrong key, then replay that measure cleanly.</p>
<h2>Exact sustain-pedal checks</h2><p>Wait mode and Daily Practice treat the sustain pedal as an exact lesson input. Pressing sustain where the score has no <b>Ped.</b> mark, failing to press it when <b>Ped.</b> is required, releasing it before <b>*</b>, or holding it beyond <b>*</b> fails the attempt and restarts the current measure. Imported MIDI CC64 and MusicXML pedal marks provide these instructions. A piece with no pedal marks—such as the built-in Ode to Joy lesson—expects no pedal at all.</p>
<h2>Focused measure drills</h2><ul>
<li><b>Single-click a measure</b> in Wait mode or daily score practice to toggle its soft coral highlight.</li>
<li>Select one, several adjacent measures, or several separate measures. Unselected gaps are skipped.</li>
<li>Single-click a selected measure to remove only that measure. Double-click the score or choose <b>End drill</b> to clear the entire selection.</li>
<li>A mistake in the second selected measure restarts that second measure—not the first selection and not the whole song.</li>
<li>Pass and mistake totals are shown above the score. Finishing a drill pass loops the selected material without falsely completing a Daily Plan step.</li>
</ul>
<div class="note">Measure selection exists only in Wait mode and Daily Practice score steps. It does not change Free Play, Listen, Play Along, or Ear Training.</div>
)HTML")},
      {"Daily Practice Plan","daily routine date previous next warm up scale chord ear song steps",
       page("Daily Practice Plan",R"HTML(
<p>Open <b>Practice → Daily practice plan</b> or the sidebar card. Choose a date, optionally change the cadence pattern, then start the plan. The pattern button immediately cycles among block chords, rising broken chords, and falling broken chords; the selected name appears in the plan before you start.</p>
<h2>Seven chapters, twenty-one guided steps</h2><p>Every chapter is taught in the same physical order: <b>right hand → left hand → together</b>.</p><ol><li>Scale and contrary motion</li><li>Arpeggios</li><li>Chord inversions: root, first, second, and octave positions</li><li>I–IV–V–I cadence with the selected block or broken-chord pattern</li><li>Pedal changes as their own chapter, with exact <b>Ped.</b> and <b>*</b> timing</li><li>Ear training: right-hand range, left-hand range, then a sound split across both hands</li><li>Repertoire</li></ol>
<h2>Major and minor days stay separate</h2><p>A plan date is either entirely major or entirely minor. On a minor day, all 21 steps use the minor scale, minor arpeggio and inversions, minor-key cadence and pedal harmony, and minor-mode repertoire. Nothing switches back to major halfway through the session.</p>
<p>The session strip above the score has Previous, Next step, Finish plan, and Close plan controls. The plan changes key, mode, fingering, cadence pattern, ear focus, and repertoire by date. Score steps use Wait mode and support focused measure drills and the wrong-note measure restart. All three pedal-change steps enforce each highlighted <b>Ped.</b> and <b>*</b>.</p>
)HTML")},
      {"Chord Practice","chords roots fifth triad seventh ninth inversion block broken",
       page("Chord Practice",R"HTML(
<p>Open <b>Practice → Chord practice</b> or its sidebar entry. Choose:</p><ul>
<li>Any of the 12 chromatic roots.</li><li>Perfect fifth/power chord; major, minor, diminished, augmented, suspended, sixth, seventh, add-nine, ninth, eleventh, or thirteenth families.</li><li>Root position or any available inversion.</li><li>Right hand, left hand, or both hands.</li><li>Block, broken rising, or broken up-and-down pattern.</li><li>A tempo from 30 to 200 BPM.</li></ul>
<p>The generated exercise opens in Wait mode and repeats. Chord fingering uses the same score-wide physical-hand optimizer as imports, lessons, Daily Practice, and Free Play; authored source fingering remains authoritative.</p>
)HTML")},
      {"Ear Training","ear hidden sound replay identify statistics",
       page("Ear Training",R"HTML(
<p>Choose <b>Practice → Ear training — Find the sound</b>, or reach it through the Daily Practice Plan. VArpeggio plays a hidden note or chord and hides the written answer.</p>
<ul><li>Play guesses on MIDI, computer keys, or the on-screen piano.</li><li>Space or <b>Hear again</b> repeats the prompt.</li><li>A correct complete answer appears on the grand staff before the next round.</li><li>Accuracy, streak, and progress update through ten-round sets.</li></ul>
<p>Measure drills and the Wait-mode measure-restart penalty do not apply to Ear Training.</p>
)HTML")},
      {"Grand Staff and learning aids","notation note names fingers fingering falling notes hands clef ledger octave",
       page("Grand Staff and learning aids",R"HTML(
<h2>Display switches</h2><ul><li><b>Metronome</b> enables the audible beat.</li><li><b>Note names</b> labels pitches with octave numbers.</li><li><b>Finger numbers</b> shows piano fingering above treble and below bass, and synchronizes the same numbers to the virtual keyboard.</li><li><b>Falling notes</b> shows the animated approach to the keyboard in Bubbles.</li></ul>
<h2>Notation behavior</h2><p>The live grand staff supports chords, beams, accidentals, rests, key and time signatures, repeats, ties, pedal marks, and the complete A0–C8 piano range. Extreme pitches use conventional 8va/8vb or 15ma/15mb notation. The viewport scale remains static instead of zooming when register changes.</p>
<p>Right, Left, and Together filter both practice requirements and visible fingering. Imported authored fingering is preserved; otherwise VArpeggio proposes phrase-aware fingering for both hands.</p>
)HTML")},
      {"Transport, tempo, and score history","play pause resume stop restart bpm seek orange line earlier latest wheel",
       page("Transport, tempo, and score history",R"HTML(
<ul><li><b>Restart</b> returns the active practice or playback to its beginning. In a drill it restarts the drill.</li><li><b>Play / Pause / Resume</b> changes meaning with the current mode.</li><li><b>Stop</b> releases app audio, MIDI notes, and pedals.</li><li>The minus/plus buttons and exact BPM field accept 30–260 BPM. Tempo changes retime generated playback, animation, keyboard highlights, and metronome from the same musical position.</li></ul>
<h2>Moving through a score</h2><p>Drag the orange line where seeking is enabled. Use the Earlier–Latest scrollbar or the mouse wheel over the staff to inspect completed measures while playback continues. Return to Latest to resume automatic follow. Live follow pages through complete measures and fits as many readable measures as the panel width allows.</p>
)HTML")},
      {"Virtual keyboard and piano sound","keyboard 61 76 88 inactive gray click instrument volume sound tone",
       page("Virtual keyboard and piano sound",R"HTML(
<ul><li>Select <b>61 keys (C2–C7)</b>, <b>76 keys (E1–G7)</b>, or <b>88 keys (A0–C8)</b> to match the physical instrument.</li><li>Key widths and octave positions never stretch. Keys outside the chosen range are grey while idle but remain clickable and respond normally.</li><li>Finger badges follow the current or upcoming score attack when Finger numbers are enabled.</li><li>Choose bright, natural, or soft recorded Kawai upright sound. The <b>Sound</b> switch mutes internal sound without disabling input.</li><li>The volume control reaches 150% and is remembered.</li><li><b>Use keyboard MIDI sound</b> routes generated playback to compatible hardware instead of the internal piano.</li></ul>
)HTML")},
      {"Free Play and recording","free play record timing sustain preview clear cache",
       page("Free Play and recording",R"HTML(
<h2>Immediate notation</h2><p>In Free Play, every played note is placed on the grand staff. A held key grows from a short value into longer notation. The staff remains cached when you visit another mode.</p>
<h2>Recording</h2><ul><li>Choose <b>Record free play</b> or press R to start and stop timed recording.</li><li>Real pauses and note lengths are preserved and quantized for readable notation.</li><li>Sustain-pedal down and release are stored separately and drawn as <b>Ped.</b> and <b>*</b> at the positions where you used them. Free Play records pedal choices without applying the Wait-mode penalty.</li></ul>
<h2>Preview and clearing</h2><p><b>Preview score</b> plays the editable staff from the orange insertion line. Pause, seek, and resume work during preview. <b>Clear staff</b> permanently empties the working score after confirmation.</p>
)HTML")},
      {"Editing the Free Play staff","edit insertion add line right click duration accidental rest meter key repeat group delete",
       page("Editing the Free Play staff",R"HTML(
<ul><li>Click the staff to place the orange <b>ADD</b> insertion line, then play keys to insert notes.</li><li>Group mode lets separately played keys share one time position as a chord. Step mode advances after each completed note or chord.</li><li>Right-click a note to change its length, force an accidental spelling, delete it, or switch grouping.</li><li>Right-click empty staff space to insert rests, change meter, change key signature, add/remove a repeat end, or remove a local meter/key change.</li><li><b>Edit staff</b> opens a table for exact pitch, start beat, duration, hand, addition, and deletion.</li><li>Choose <b>Done editing — hide ADD line</b>, Apply changes, or press Escape to leave insertion mode.</li></ul>
<p>Projects preserve notes, rests, key and meter changes, repeats, fingering, and pedal events.</p>
)HTML")},
      {"Importing scores and PDFs","import midi musicxml mxl abc varpeggio pdf audiveris omr exact live",
       page("Importing scores and PDFs",R"HTML(
<h2>Supported score files</h2><p>MIDI (.mid/.midi), MusicXML (.musicxml/.mxl/.xml), ABC (.abc), VArpeggio projects (.varpeggio), and PDF (.pdf).</p>
<ul><li>VArpeggio projects reopen as editable Free Play scores.</li><li>A MusicXML file beside a same-named PDF uses XML for playback and the PDF for Exact PDF viewing.</li><li>VArpeggio-generated PDFs contain attached editable score data.</li><li>An ordinary PDF first looks for attached data or a same-name sidecar, then uses Audiveris or the built-in reader to produce a recognition draft.</li></ul>
<h2>Exact PDF and Live follow</h2><p><b>Exact PDF</b> preserves the original page. <b>Live follow</b> shows the playable interpretation used by Wait, Listen, Play Along, falling notes, and export.</p>
<div class="warning">Optical recognition is a draft. Dense voices, scans, handwriting, unusual engraving, accidentals, and rhythms may require correction. The original PDF is never overwritten.</div>
)HTML")},
      {"Saving and exporting","save export midi musicxml abc wav mp4 svg png pdf project csv",
       page("Saving and exporting",R"HTML(
<p>Select a format in the top-right list and choose <b>Save</b>, or use <b>File → Export</b>.</p>
<table><tr><th>Format</th><th>Purpose</th></tr>
<tr><td>MIDI</td><td>Playable note, velocity, tempo, and pedal instructions.</td></tr><tr><td>MusicXML</td><td>Detailed notation interchange.</td></tr><tr><td>ABC</td><td>Text notation interchange.</td></tr><tr><td>WAV</td><td>Rendered audio using the selected piano profile.</td></tr><tr><td>MP4</td><td>Rendered falling-note piano visualization with audio.</td></tr><tr><td>SVG / PNG</td><td>Live-score image exports.</td></tr><tr><td>PDF</td><td>Engraved score with VArpeggio data attached when available.</td></tr><tr><td>VArpeggio project</td><td>Editable notes and notation metadata.</td></tr><tr><td>CSV</td><td>Plain note-event data.</td></tr></table>
)HTML")},
      {"Original media and Director","source audio video director hear original overlay detector review",
       page("Original media and Director",R"HTML(
<p>When a score came from audio or video, <b>Hear original recording (reference only)</b> performs an A/B comparison. It does not replace the generated notes used by practice and export.</p>
<h2>Director</h2><p>Open Director from Practice or the source-media control to review detected notes over the original video. Director provides Play, five-second back/forward controls, source sound, playback speed, and detector-video export. It is a verification view for accepted source bars and timing.</p>
<p>If generated notes and the source differ, treat the source as the reference and correct or re-transcribe the notation before relying on it.</p>
)HTML")},
      {"Experimental Video Notes","video audio transcription automatic tempo falling bars experimental",
       page("Experimental Video Notes",R"HTML(
<p>Open <b>Experimental → Video Notes</b> to convert local audio or video into practice material.</p>
<ul><li>Automatic falling-note analysis detects the visible keyboard, key range, strike line, bar crossings, duration, dynamics, and hand colors.</li><li>Sound analysis estimates tempo and polyphonic pitches when the visual tutorial is unavailable.</li><li>Results can create a VArpeggio project, MIDI, MusicXML, playable PDF, and source comparison.</li><li>Tempo can be supplied manually or estimated automatically.</li></ul>
<div class="warning">This feature is experimental. Clean solo piano and clear falling-note tutorials work best. Vocals, drums, effects, camera motion, occlusion, or unusual colors can produce missing or extra notes.</div>
)HTML")},
      {"Layout, settings, and remembered state","settings bubbles coach collapse sidebar window maximize remember close",
       page("Layout, settings, and remembered state",R"HTML(
<ul><li>Collapse the Grand Staff, Bubbles + Live Coach, or Keyboard with the control at the far-right of each header.</li><li><b>Settings → Show Bubbles + Live Coach</b> hides or restores that complete section, including its minimized header, to reclaim screen space.</li><li>Pin or unpin the sidebar. The unpinned icon rail expands on hover.</li><li>VArpeggio remembers window size, position, maximized state, section expansion, sidebar mode, volume, virtual-keyboard range, note-name and fingering aids, piano profile, MIDI tone, and keyboard MIDI sound routing.</li><li>Large KDE system fonts are used rather than replaced; dialogs and controls reflow where practical.</li></ul>
)HTML")},
      {"Keyboard shortcuts","keys shortcut hotkey space escape r ctrl open save f1",
       page("Keyboard shortcuts",R"HTML(
<table><tr><th>Key</th><th>Action</th></tr>
<tr><td>A W S E D F T G Y H U J K</td><td>Play C4 through C5, including the black keys.</td></tr>
<tr><td>Space</td><td>Start, pause, resume, restart Wait mode, or replay the Ear Training prompt according to context.</td></tr>
<tr><td>R</td><td>Start or stop Free Play recording.</td></tr>
<tr><td>Escape</td><td>Stop playback and recording; in Free Play it also exits insertion editing.</td></tr>
<tr><td>Ctrl+O</td><td>Import score.</td></tr><tr><td>Ctrl+Shift+S</td><td>Open Export.</td></tr><tr><td>Ctrl+Q</td><td>Quit.</td></tr><tr><td>F1</td><td>Open this Help Manual.</td></tr></table>
<p>Letter-key piano shortcuts are disabled while typing into a text field.</p>
)HTML")},
      {"Troubleshooting and limits","problem stuck audio static underrun pdf recognition midi no sound",
       page("Troubleshooting and limits",R"HTML(
<h2>The score will not advance</h2><p>In Wait mode, hold every highlighted note for its written duration. Release and replay repeated notes. After a wrong note, release the wrong key and replay the current measure from its beginning.</p>
<h2>No sound</h2><p>Check the Sound switch and volume. If Use keyboard MIDI sound is enabled, generated sound is expected from the connected keyboard instead of the PC. Reconnect MIDI if the adapter changed ports.</p>
<h2>Audio crackle or static</h2><p>A system scheduling or storage stall can starve the audio buffer (an underrun). VArpeggio restarts from silence and fades active voices back in, but closing heavy disk activity or using the hardware keyboard's own sound can help on an overloaded system.</p>
<h2>PDF or media mistakes</h2><p>Optical and audio/video transcription are recognition tools, not proof of the original notation. Review difficult measures in Live follow, compare the original, edit when possible, and save a project copy.</p>
)HTML")},
      {"About, files, and licensing","about version license gpl third party files privacy",
       page("About, files, and licensing",QString(R"HTML(
<p><b>VArpeggio %1</b><br>Piano Practice &amp; Notation Studio<br>Built by Voncloft. Copyright © 2026 Voncloft.</p>
<p>VArpeggio's original source code is licensed under GNU GPL-3.0-only. Bundled data and third-party components retain their own licenses. The installed README, LICENSE, THIRD_PARTY_NOTICES, and LICENSES directory provide the complete inventory and license texts.</p>
<p>User preferences are stored with Qt settings. Imported scores, recordings, and exports remain in locations chosen by the user. An Audiveris runtime may be installed in the per-user application-data directory when ordinary-PDF recognition requires it.</p>
<p>The software is provided without warranty. The VArpeggio and Voncloft names and artwork are not automatically licensed as trademarks by the GPL.</p>
)HTML").arg(VARPEGGIO_VERSION))}
    };
}
}

HelpManualDialog::HelpManualDialog(QWidget *parent):QDialog(parent){
    setObjectName("helpManualDialog");setWindowTitle("VArpeggio Help Manual");setModal(true);setMinimumSize(900,640);resize(1120,760);
    QPalette palette;palette.setColor(QPalette::Window,QColor("#f5f1e8"));palette.setColor(QPalette::WindowText,QColor("#18312d"));palette.setColor(QPalette::Base,QColor("#fffefa"));palette.setColor(QPalette::AlternateBase,QColor("#eef7f4"));palette.setColor(QPalette::Text,QColor("#18312d"));palette.setColor(QPalette::Button,QColor("#fffefa"));palette.setColor(QPalette::ButtonText,QColor("#18312d"));palette.setColor(QPalette::Highlight,QColor("#2d7d70"));palette.setColor(QPalette::HighlightedText,Qt::white);setPalette(palette);setAutoFillBackground(true);
    setStyleSheet(R"(
QDialog#helpManualDialog{background:#f5f1e8;color:#18312d}
QDialog#helpManualDialog QLabel{color:#18312d;background:transparent}
QDialog#helpManualDialog QLineEdit{color:#18312d;background:#fffefa;border:1px solid #9fb7b0;border-radius:8px;padding:9px 12px;selection-color:white;selection-background-color:#2d7d70}
QDialog#helpManualDialog QLineEdit:focus{border:2px solid #2d7d70;padding:8px 11px}
QDialog#helpManualDialog QListWidget{color:#18312d;background:#fffefa;border:1px solid #cad7d2;border-radius:9px;padding:6px;outline:0}
QDialog#helpManualDialog QListWidget::item{color:#31534d;background:transparent;border-radius:6px;padding:8px 9px;margin:1px}
QDialog#helpManualDialog QListWidget::item:selected{color:white;background:#2d7d70}
QDialog#helpManualDialog QListWidget::item:hover:!selected{color:#173f39;background:#e7f4ef}
QDialog#helpManualDialog QTextBrowser{color:#18312d;background:#fffefa;border:1px solid #cad7d2;border-radius:9px;selection-color:white;selection-background-color:#2d7d70}
QDialog#helpManualDialog QPushButton{color:#18312d;background:#fffefa;border:1px solid #9fb7b0;border-radius:8px;padding:8px 16px;min-width:96px;font-weight:700}
QDialog#helpManualDialog QPushButton:hover{color:white;background:#2d7d70;border-color:#2d7d70}
QDialog#helpManualDialog QPushButton:pressed{color:white;background:#205e54}
QDialog#helpManualDialog QScrollBar:vertical{width:11px;background:#eef1ed;border-radius:5px}
QDialog#helpManualDialog QScrollBar::handle:vertical{background:#9caea8;border-radius:5px;min-height:32px}
QDialog#helpManualDialog QSplitter::handle{background:#d8ddd9;width:5px}
)");

    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(24,20,24,18);layout->setSpacing(12);
    auto *title=new QLabel("VArpeggio Help Manual");QFont titleFont=QApplication::font();titleFont.setBold(true);titleFont.setPointSizeF(qMax<qreal>(16.0,titleFont.pointSizeF()+6.0));title->setFont(titleFont);layout->addWidget(title);
    auto *subtitle=new QLabel(QString("Complete guide to every practice, notation, MIDI, editing, import, export, and interface function · Version %1").arg(VARPEGGIO_VERSION));subtitle->setWordWrap(true);subtitle->setStyleSheet("color:#526862");layout->addWidget(subtitle);
    auto *search=new QLineEdit;search->setPlaceholderText("Search the manual (for example: wrong note, MIDI, fingering, PDF, record)");search->setClearButtonEnabled(true);search->setAccessibleName("Search the VArpeggio Help Manual");layout->addWidget(search);
    auto *splitter=new QSplitter(Qt::Horizontal);auto *topics=new QListWidget;topics->setMinimumWidth(300);topics->setMaximumWidth(390);topics->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);topics->setAccessibleName("Manual sections");auto *article=new QTextBrowser;article->setOpenExternalLinks(true);article->setFont(QApplication::font());article->setAccessibleName("Manual article");splitter->addWidget(topics);splitter->addWidget(article);splitter->setStretchFactor(0,0);splitter->setStretchFactor(1,1);splitter->setSizes({330,750});layout->addWidget(splitter,1);
    const auto sections=manualSections();for(const auto &section:sections)topics->addItem(section.title);
    auto *status=new QLabel(QString("%1 complete sections").arg(sections.size()));status->setStyleSheet("color:#62736e");
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close);if(auto *close=buttons->button(QDialogButtonBox::Close)){close->setText("Close manual");close->setDefault(true);}
    auto *footer=new QHBoxLayout;footer->addWidget(status,1);footer->addWidget(buttons);layout->addLayout(footer);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(topics,&QListWidget::currentRowChanged,this,[article,sections](int row){if(row>=0&&row<sections.size()){article->setHtml(sections[row].body);article->verticalScrollBar()->setValue(0);}});
    connect(search,&QLineEdit::textChanged,this,[topics,status,sections](const QString &text){
        const QString needle=text.trimmed();int visible=0,first=-1;
        for(int index=0;index<sections.size();++index){QString haystack=sections[index].title+' '+sections[index].keywords+' '+sections[index].body;haystack.remove(QRegularExpression("<[^>]*>"));const bool match=needle.isEmpty()||haystack.contains(needle,Qt::CaseInsensitive);topics->item(index)->setHidden(!match);if(match){if(first<0)first=index;++visible;}}
        status->setText(needle.isEmpty()?QString("%1 complete sections").arg(visible):QString("%1 matching section%2").arg(visible).arg(visible==1?"":"s"));
        if(first>=0&&(topics->currentRow()<0||topics->item(topics->currentRow())->isHidden()))topics->setCurrentRow(first);
    });
    topics->setCurrentRow(0);search->setFocus();
}
