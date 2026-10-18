#!/usr/bin/env python3
"""Create properly measured MusicXML and an engraved PDF from VArpeggio notes."""

from __future__ import annotations

import argparse
import html
import json
import math
import subprocess
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

# 24 divisions preserves ordinary binary values and exact thirds. The former
# eight-tick grid rounded every Moonlight triplet into the wrong rhythm.
DIVISIONS = 24
TICKS_PER_MEASURE = 96
NOTE_VALUES = (96, 72, 48, 36, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2, 1)
PITCH_CLASS_FLAT = {
    0: ("C", 0), 1: ("D", -1), 2: ("D", 0), 3: ("E", -1),
    4: ("E", 0), 5: ("F", 0), 6: ("G", -1), 7: ("G", 0),
    8: ("A", -1), 9: ("A", 0), 10: ("B", -1), 11: ("B", 0),
}
PITCH_CLASS_SHARP = {
    0: ("C", 0), 1: ("C", 1), 2: ("D", 0), 3: ("D", 1),
    4: ("E", 0), 5: ("F", 0), 6: ("F", 1), 7: ("G", 0),
    8: ("G", 1), 9: ("A", 0), 10: ("A", 1), 11: ("B", 0),
}


@dataclass
class Group:
    start: int
    duration: int
    pitches: tuple[int, ...]
    accidentals: tuple[int, ...]
    spellings: tuple[tuple[int, int], ...]
    staff: int
    voice: int = 1

    @property
    def end(self) -> int:
        return self.start + self.duration


@dataclass
class Item:
    start: int
    duration: int
    pitches: tuple[int, ...]
    accidentals: tuple[int, ...]
    spellings: tuple[tuple[int, int], ...]
    rest: bool
    visible: bool = True
    tie_start: bool = False
    tie_stop: bool = False
    beams: tuple[tuple[int, str], ...] = ()


def split_value(duration: int) -> list[int]:
    pieces: list[int] = []
    while duration:
        value = next(candidate for candidate in NOTE_VALUES if candidate <= duration)
        pieces.append(value)
        duration -= value
    return pieces


def allocate_voices(source: list[dict]) -> dict[int, list[list[Group]]]:
    result: dict[int, list[list[Group]]] = {}
    for staff in (1, 2):
        grouped: dict[tuple[int, int], list[tuple[int, int, int, int]]] = defaultdict(list)
        for note in source:
            note_staff = 2 if note.get("hand") == "left" else 1
            if note_staff != staff:
                continue
            start = max(0, round(float(note["start"]) * DIVISIONS))
            duration = max(1, round(float(note["duration"]) * DIVISIONS))
            grouped[(start, duration)].append((int(note["note"]), int(note.get("accidental", 0)), int(note.get("writtenStep", -1)), int(note.get("writtenAlter", 99))))
        groups = [
            Group(start, duration, tuple(pitch for pitch, _, _, _ in sorted(set(pitches))),
                  tuple(accidental for _, accidental, _, _ in sorted(set(pitches))),
                  tuple((step, alter) for _, _, step, alter in sorted(set(pitches))), staff)
            for (start, duration), pitches in grouped.items()
        ]
        groups.sort(key=lambda group: (group.start, -group.duration, group.pitches))
        voices: list[list[Group]] = []
        ends: list[int] = []
        for group in groups:
            chosen = next((index for index, end in enumerate(ends) if end <= group.start), None)
            if chosen is None:
                chosen = len(voices)
                voices.append([])
                ends.append(0)
            group.voice = chosen + 1
            voices[chosen].append(group)
            ends[chosen] = group.end
        result[staff] = voices or [[]]
    return result


def add_beams(items: list[Item]) -> list[Item]:
    index = 0
    while index < len(items):
        first = items[index]
        if first.rest or first.duration not in (3, 4, 6, 8, 12):
            index += 1
            continue
        run = [index]
        beat = first.start // DIVISIONS
        cursor = first.start + first.duration
        look = index + 1
        while look < len(items):
            current = items[look]
            if current.rest or current.duration != first.duration or current.start != cursor or current.start // DIVISIONS != beat:
                break
            run.append(look)
            cursor += current.duration
            look += 1
        if len(run) >= 2:
            levels = {12: 1, 8: 1, 6: 2, 4: 2, 3: 3}[first.duration]
            for position, item_index in enumerate(run):
                value = "begin" if position == 0 else "end" if position == len(run) - 1 else "continue"
                items[item_index].beams = tuple((level, value) for level in range(1, levels + 1))
        index = max(look, index + 1)
    return items


def fragments(groups: list[Group], measure_start: int, measure_end: int, visible_rests: bool) -> list[Item]:
    notes: list[Item] = []
    for group in groups:
        if group.end <= measure_start or group.start >= measure_end:
            continue
        clipped_start = max(group.start, measure_start)
        clipped_end = min(group.end, measure_end)
        cursor = clipped_start - measure_start
        pieces = split_value(clipped_end - clipped_start)
        for index, duration in enumerate(pieces):
            notes.append(Item(
                cursor, duration, group.pitches, group.accidentals, group.spellings, False,
                tie_start=(group.end > measure_end or index < len(pieces) - 1),
                tie_stop=(group.start < measure_start or index > 0),
            ))
            cursor += duration
    notes.sort(key=lambda item: item.start)
    timeline: list[Item] = []
    cursor = 0
    for note in notes:
        if note.start > cursor:
            rest_cursor = cursor
            for duration in split_value(note.start - cursor):
                timeline.append(Item(rest_cursor, duration, (), (), (), True, visible_rests))
                rest_cursor += duration
        timeline.append(note)
        cursor = max(cursor, note.start + note.duration)
    measure_duration = measure_end - measure_start
    if cursor < measure_duration:
        for duration in split_value(measure_duration - cursor):
            timeline.append(Item(cursor, duration, (), (), (), True, visible_rests))
            cursor += duration
    return add_beams(timeline)


def duration_notation(duration: int) -> tuple[str, int, bool]:
    return {
        96: ("whole", 0, False), 72: ("half", 1, False), 48: ("half", 0, False),
        36: ("quarter", 1, False), 24: ("quarter", 0, False),
        18: ("eighth", 1, False), 16: ("quarter", 0, True), 12: ("eighth", 0, False),
        9: ("16th", 1, False), 8: ("eighth", 0, True), 6: ("16th", 0, False),
        4: ("16th", 0, True), 3: ("32nd", 0, False),
        2: ("32nd", 0, True), 1: ("64th", 0, False),
    }[duration]


def pitch_xml(midi: int, accidental_mark: int = 0, fifths: int = 0,
              written_step: int = -1, written_alter: int = 99) -> str:
    if 0 <= written_step <= 6 and -2 <= written_alter <= 2:
        natural = (0, 2, 4, 5, 7, 9, 11)
        step = "CDEFGAB"[written_step]
        alter = written_alter
        octave = math.floor((midi - natural[written_step] - alter) / 12) - 1
        accidental = f"<alter>{alter}</alter>" if alter else ""
        return f"<pitch><step>{step}</step>{accidental}<octave>{octave}</octave></pitch>"
    spelling = PITCH_CLASS_SHARP if accidental_mark == 2 or (accidental_mark != 3 and fifths > 0) else PITCH_CLASS_FLAT
    step, alter = spelling[midi % 12]
    accidental = f"<alter>{alter}</alter>" if alter else ""
    return f"<pitch><step>{step}</step>{accidental}<octave>{midi // 12 - 1}</octave></pitch>"


def note_xml(item: Item, staff: int, voice: int, fifths: int = 0) -> str:
    note_type, dots, triplet = duration_notation(item.duration)
    blocks: list[str] = []
    for pitch_index, pitch in enumerate(item.pitches if not item.rest else (None,)):
        hidden = ' print-object="no" print-spacing="yes"' if item.rest and not item.visible else ""
        pieces = [f"<note{hidden}>"]
        if pitch_index:
            pieces.append("<chord/>")
        accidental_mark = item.accidentals[pitch_index] if pitch_index < len(item.accidentals) else 0
        written_step, written_alter = item.spellings[pitch_index] if pitch_index < len(item.spellings) else (-1, 99)
        if not accidental_mark and pitch is not None:
            natural_order = (5, 0, 7, 2, 9, 4, 11) if fifths > 0 else (11, 4, 9, 2, 7, 0, 5)
            if int(pitch) % 12 in natural_order[:abs(fifths)]:
                accidental_mark = 1
        pieces.append("<rest/>" if item.rest else pitch_xml(int(pitch), accidental_mark, fifths, written_step, written_alter))
        pieces += [f"<duration>{item.duration}</duration>", f"<voice>{voice}</voice>", f"<type>{note_type}</type>"]
        pieces += ["<dot/>"] * dots
        if triplet:
            pieces.append("<time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>")
        if item.tie_stop:
            pieces.append('<tie type="stop"/>')
        if item.tie_start:
            pieces.append('<tie type="start"/>')
        pieces.append(f"<staff>{staff}</staff>")
        if not item.rest:
            if accidental_mark:
                pieces.append(f"<accidental>{ {1: 'natural', 2: 'sharp', 3: 'flat', 4: 'double-sharp', 5: 'flat-flat'}.get(accidental_mark, 'natural') }</accidental>")
            # A chord shares one stem/beam. Repeating beam elements on every
            # secondary <chord/> note makes Verovio treat the chord as a broken
            # beam sequence and can corrupt the following rests.
            if pitch_index == 0:
                pieces += [f'<beam number="{level}">{value}</beam>' for level, value in item.beams]
            if item.tie_start or item.tie_stop:
                pieces.append("<notations>")
                if item.tie_stop:
                    pieces.append('<tied type="stop"/>')
                if item.tie_start:
                    pieces.append('<tied type="start"/>')
                pieces.append("</notations>")
        pieces.append("</note>")
        blocks.append("".join(pieces))
    return "".join(blocks)


@dataclass
class MeasurePlan:
    start: int
    end: int
    beats: int
    beat_type: int
    fifths: int
    show_time: bool
    show_key: bool
    repeat_end: bool


def measure_plan(project: dict, last_tick: int) -> list[MeasurePlan]:
    meters = {0: (4, 4)}
    for event in project.get("timeSignatures", []):
        tick = max(0, round(float(event.get("beat", 0)) * DIVISIONS))
        meters[tick] = (max(1, int(event.get("beats", 4))), max(1, int(event.get("beatType", 4))))
    keys = {0: 0}
    for event in project.get("keySignatures", []):
        tick = max(0, round(float(event.get("beat", 0)) * DIVISIONS))
        keys[tick] = max(-7, min(7, int(event.get("fifths", 0))))
    repeats = {max(0, round(float(beat) * DIVISIONS)) for beat in project.get("repeatEnds", [])}
    forced = sorted((set(meters) | set(keys) | repeats) - {0})
    plans: list[MeasurePlan] = []
    cursor = 0
    while cursor < max(1, last_tick):
        beats, beat_type = next(value for tick, value in sorted(meters.items(), reverse=True) if tick <= cursor)
        fifths = next(value for tick, value in sorted(keys.items(), reverse=True) if tick <= cursor)
        natural_end = cursor + max(1, round(beats * 4 * DIVISIONS / beat_type))
        next_forced = next((tick for tick in forced if tick > cursor), None)
        end = min(natural_end, next_forced) if next_forced is not None else natural_end
        plans.append(MeasurePlan(cursor, end, beats, beat_type, fifths,
                                 not plans or cursor in meters, not plans or cursor in keys,
                                 end in repeats))
        cursor = end
    return plans


def build_musicxml(project: dict) -> str:
    title = html.escape(str(project.get("title") or "VArpeggio transcription"))
    tempo = max(30, min(260, int(project.get("tempo") or 96)))
    voices = allocate_voices(project["notes"])
    endpoints = [round((float(note["start"]) + float(note["duration"])) * DIVISIONS) for note in project["notes"]]
    endpoints += [round((float(rest.get("beat", 0)) + float(rest.get("duration", 1))) * DIVISIONS) for rest in project.get("rests", [])]
    endpoints += [round(float(beat) * DIVISIONS) for beat in project.get("repeatEnds", [])]
    endpoints += [round(float(event.get("beat", 0)) * DIVISIONS) + DIVISIONS for event in project.get("timeSignatures", [])]
    endpoints += [round(float(event.get("beat", 0)) * DIVISIONS) + DIVISIONS for event in project.get("keySignatures", [])]
    endpoints += [round(float(event.get("beat", 0)) * DIVISIONS) for event in project.get("pedalEvents", [])]
    last_tick = max(endpoints, default=TICKS_PER_MEASURE)
    plans = measure_plan(project, last_tick)
    measures: list[str] = []
    for measure_index, plan in enumerate(plans):
        number = measure_index + 1
        layout = ""
        if number == 1:
            layout = '<print><system-layout><system-distance>135</system-distance></system-layout></print>'
        elif (number - 1) % 12 == 0:
            layout = '<print new-page="yes"><system-layout><system-distance>135</system-distance></system-layout></print>'
        elif (number - 1) % 3 == 0:
            layout = '<print new-system="yes"><system-layout><system-distance>135</system-distance></system-layout></print>'
        attributes = ""
        if number == 1 or plan.show_time or plan.show_key:
            parts = ["<attributes>"]
            if number == 1:
                parts.append(f"<divisions>{DIVISIONS}</divisions>")
            if number == 1 or plan.show_key:
                parts.append(f"<key><fifths>{plan.fifths}</fifths></key>")
            if number == 1 or plan.show_time:
                parts.append(f"<time><beats>{plan.beats}</beats><beat-type>{plan.beat_type}</beat-type></time>")
            if number == 1:
                parts += ['<staves>2</staves>', '<part-symbol>brace</part-symbol>',
                          '<clef number="1"><sign>G</sign><line>2</line></clef>',
                          '<clef number="2"><sign>F</sign><line>4</line></clef>']
            parts.append("</attributes>")
            attributes = "".join(parts)
            if number == 1:
                attributes += f'<direction placement="above"><direction-type><words font-weight="bold">Tempo: {tempo} BPM</words></direction-type><sound tempo="{tempo}"/></direction>'
        # MusicXML's encoded system breaks need explicit measure widths in the
        # bundled engraver.  Share the available system width between however
        # many measures this system actually has so a short final system does
        # not collapse into a cramped block at the left edge of the page.
        system_start = (measure_index // 3) * 3
        system_measure_count = min(3, len(plans) - system_start)
        measure_width = round(960 / system_measure_count)
        content = [f'<measure number="{number}" width="{measure_width}">', layout, attributes]
        for pedal in project.get("pedalEvents", []):
            tick = max(0, round(float(pedal.get("beat", 0)) * DIVISIONS))
            if plan.start <= tick < plan.end or (tick == plan.end and measure_index == len(plans) - 1):
                offset = tick - plan.start
                pedal_type = "start" if bool(pedal.get("down")) else "stop"
                value = "yes" if bool(pedal.get("down")) else "no"
                content.append(
                    '<direction placement="below">'
                    f'<direction-type><pedal type="{pedal_type}" line="yes" sign="yes"/></direction-type>'
                    f'<offset>{offset}</offset><staff>2</staff><sound damper-pedal="{value}"/>'
                    '</direction>'
                )
        first_voice = True
        for staff in (1, 2):
            for voice_index, groups in enumerate(voices[staff], start=1):
                if not first_voice:
                    content.append(f"<backup><duration>{plan.end-plan.start}</duration></backup>")
                first_voice = False
                for item in fragments(groups, plan.start, plan.end, visible_rests=(voice_index == 1)):
                    content.append(note_xml(item, staff, voice_index, plan.fifths))
        if plan.repeat_end:
            content.append('<barline location="right"><bar-style>light-heavy</bar-style><repeat direction="backward"/></barline>')
        content.append("</measure>")
        measures.append("".join(content))
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n'
        '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" '
        '"http://www.musicxml.org/dtds/partwise.dtd">\n'
        f'<score-partwise version="4.0"><work><work-title>{title}</work-title></work>'
        '<identification><encoding><software>VArpeggio detailed video transcription</software></encoding></identification>'
        '<part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>'
        f'<part id="P1">{"".join(measures)}</part></score-partwise>\n'
    )


def render_pdf(musicxml: Path, project: Path, output: Path) -> None:
    local_python = Path(__file__).resolve().parent / "python"
    if local_python.exists():
        sys.path.insert(0, str(local_python))
    try:
        import verovio  # type: ignore
    except ImportError as exc:
        raise RuntimeError("The bundled Verovio score engraver is missing.") from exc
    with tempfile.TemporaryDirectory(prefix="varpeggio-score-") as directory:
        temp = Path(directory)
        toolkit = verovio.toolkit()
        toolkit.setOptions({
            "breaks": "encoded", "pageWidth": 2159, "pageHeight": 2794,
            "pageMarginTop": 180, "pageMarginBottom": 80,
            "pageMarginLeft": 85, "pageMarginRight": 85,
            "scale": 36, "header": "auto", "footer": "none",
            "spacingLinear": 0.28, "spacingNonLinear": 0.55,
            # Short Free Play scores should still use the printable width;
            # otherwise a one-measure score is cramped into the left third.
            "minLastJustification": 0.0, "systemMaxPerPage": 4,
            "svgViewBox": True,
        })
        if not toolkit.loadFile(str(musicxml)):
            raise RuntimeError("Verovio could not load the generated MusicXML.")
        pdf_pages: list[Path] = []
        for page in range(1, toolkit.getPageCount() + 1):
            svg = temp / f"page-{page:03d}.svg"
            page_pdf = temp / f"page-{page:03d}.pdf"
            svg.write_text(toolkit.renderToSVG(page), encoding="utf-8")
            subprocess.run(["rsvg-convert", "-f", "pdf", "-o", str(page_pdf), str(svg)], check=True)
            pdf_pages.append(page_pdf)
        merged = temp / "engraved-score.pdf"
        subprocess.run(["pdfunite", *map(str, pdf_pages), str(merged)], check=True)
        # Preserve VArpeggio's playable-PDF behavior while using Verovio's exact
        # notation engraving for the visible pages.
        output.unlink(missing_ok=True)
        subprocess.run(["pdfattach", str(merged), str(project), str(output)], check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    parser.add_argument("--musicxml", required=True)
    parser.add_argument("--pdf")
    args = parser.parse_args()
    project = json.loads(Path(args.project).read_text(encoding="utf-8"))
    xml_path = Path(args.musicxml)
    xml_path.parent.mkdir(parents=True, exist_ok=True)
    xml_path.write_text(build_musicxml(project), encoding="utf-8")
    if args.pdf:
        output = Path(args.pdf)
        output.parent.mkdir(parents=True, exist_ok=True)
        render_pdf(xml_path, Path(args.project), output)


if __name__ == "__main__":
    main()
