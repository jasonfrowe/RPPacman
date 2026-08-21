#!/usr/bin/env python3
"""
Direct NSF -> OPL2 translation layer.

This is intentionally scoped to the importer output and the runtime register
contract in src/opl.c. It does not attempt to tune patch values by guesswork;
it converts semantic note / instrument / volume rows into OPL2 register events
using the actual channel mapping and delay semantics the player expects.

Key rules implemented in this first pass:
- volume is translated in log space for the OPL2 TL register
- volume updates never force a voice-off, to avoid click artifacts
- all tracks are processed from the NSF header
- the drums are routed to dedicated percussion voices
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Dict, Tuple

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from import_nsf import NSFConverter


# ---------------------------------------------------------------------------
# OPL2 register layout helpers
# ---------------------------------------------------------------------------

MOD_OFFSETS = [0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12]
CAR_OFFSETS = [0x03, 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x13, 0x14, 0x15]

# The RP6502 runtime uses registers in the same family represented in src/opl.c.
# This mapping is intentionally the same as the runtime contract.


@dataclass
class OPL2Event:
    reg: int
    value: int
    delay: int = 0

    def to_tuple(self):
        return (self.reg, self.value, self.delay)

    def to_bin(self) -> bytes:
        delay = max(0, min(0xFFFF, int(self.delay)))
        return bytes([
            self.reg & 0xFF,
            self.value & 0xFF,
            delay & 0xFF,
            (delay >> 8) & 0xFF,
        ])

    @staticmethod
    def end_marker() -> bytes:
        return b"\xFF\xFF\x00\x00"


@dataclass
class VoiceState:
    note: int = 0
    volume: int = -1
    patch: int = 0
    active: bool = False
    fnum_hi: int = 0
    tl: int = -1


class OPL2Translator:
    def __init__(self):
        self.voice_state: Dict[int, VoiceState] = {i: VoiceState() for i in range(9)}
        self.channel_ksl: Dict[int, int] = {i: 0 for i in range(9)}
        # Pac-Man CE leans toward a bright, punchy arcade synth: tight lead,
        # clipped bass, and dry support voices with little extra resonance.
        self.patch_bank: Dict[int, Dict[str, int]] = {
              80: {"m_ave": 0x21, "m_ksl": 0x15, "m_atdec": 0xF4, "m_susrel": 0x18, "m_wave": 0x03,
                  "c_ave": 0x31, "c_ksl": 0x00, "c_atdec": 0xF4, "c_susrel": 0x18, "c_wave": 0x00,
                  "feedback": 0x02},
              81: {"m_ave": 0x41, "m_ksl": 0x0C, "m_atdec": 0xF2, "m_susrel": 0xFF, "m_wave": 0x00,
                  "c_ave": 0x11, "c_ksl": 0x00, "c_atdec": 0xF2, "c_susrel": 0xFF, "c_wave": 0x00,
                  "feedback": 0x02},
              33: {"m_ave": 0x01, "m_ksl": 0x10, "m_atdec": 0xD6, "m_susrel": 0xF2, "m_wave": 0x00,
                  "c_ave": 0x10, "c_ksl": 0x80, "c_atdec": 0xC6, "c_susrel": 0x8A, "c_wave": 0x00,
                 "feedback": 0x02},
            115: {"m_ave": 0x32, "m_ksl": 0x44, "m_atdec": 0xF8, "m_susrel": 0xFF, "m_wave": 0x00,
                  "c_ave": 0x11, "c_ksl": 0x00, "c_atdec": 0xF5, "c_susrel": 0x7F, "c_wave": 0x00,
                  "feedback": 0x04},
            118: {"m_ave": 0x00, "m_ksl": 0x0D, "m_atdec": 0xE8, "m_susrel": 0xEF, "m_wave": 0x00,
                  "c_ave": 0x00, "c_ksl": 0x00, "c_atdec": 0xA5, "c_susrel": 0xFF, "c_wave": 0x00,
                  "feedback": 0x02},
            253: {"m_ave": 0x00, "m_ksl": 0x0D, "m_atdec": 0xE8, "m_susrel": 0xEF, "m_wave": 0x00,
                  "c_ave": 0x00, "c_ksl": 0x00, "c_atdec": 0xA5, "c_susrel": 0xFF, "c_wave": 0x00,
                  "feedback": 0x06},
            254: {"m_ave": 0x06, "m_ksl": 0x00, "m_atdec": 0xF0, "m_susrel": 0xF0, "m_wave": 0x00,
                  "c_ave": 0x00, "c_ksl": 0x00, "c_atdec": 0xF7, "c_susrel": 0xF7, "c_wave": 0x00,
                  "feedback": 0x0E},
            255: {"m_ave": 0x05, "m_ksl": 0x00, "m_atdec": 0xF0, "m_susrel": 0x77, "m_wave": 0x00,
                  "c_ave": 0x00, "c_ksl": 0x00, "c_atdec": 0xFA, "c_susrel": 0xEA, "c_wave": 0x00,
                  "feedback": 0x0E},
            38: {"m_ave": 0x21, "m_ksl": 0x00, "m_atdec": 0xF1, "m_susrel": 0x38, "m_wave": 0x00,
                 "c_ave": 0x21, "c_ksl": 0x00, "c_atdec": 0xF1, "c_susrel": 0x38, "c_wave": 0x00,
                 "feedback": 0x01},
            39: {"m_ave": 0x01, "m_ksl": 0x00, "m_atdec": 0xF1, "m_susrel": 0x54, "m_wave": 0x00,
                 "c_ave": 0x01, "c_ksl": 0x00, "c_atdec": 0xF1, "c_susrel": 0x54, "c_wave": 0x00,
                 "feedback": 0x00},
        }

    @staticmethod
    def midi_to_fnum(midi_note: int) -> tuple[int, int]:
        """Match the runtime's OPL2 note conversion in src/opl.c."""
        note = max(12, min(127, midi_note))
        block = (note - 12) // 12
        note_idx = (note - 12) % 12
        if block > 7:
            block = 7

        fnum_table = [308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579]
        f_num = fnum_table[note_idx]
        hi = 0x20 | (block << 2) | ((f_num >> 8) & 0x03)
        lo = f_num & 0xFF
        return lo, hi

    @staticmethod
    def velocity_to_opl_tl(velocity: int) -> int:
        """Map the importer's linear volume into the OPL2 TL logarithmic range."""
        v = max(0, min(63, int(velocity)))
        if v <= 0:
            return 63
        if v >= 63:
            return 0

        # Use a perceptual log curve centered on the 0..63 OPL2 TL range.
        ratio = math.log2(v + 1) / math.log2(64)
        return int(round(63.0 * (1.0 - ratio)))

    @staticmethod
    def _reg_for_channel(channel: int, base: int) -> int:
        return base + channel

    def patch_set(self, channel: int, patch_id: int) -> List[OPL2Event]:
        patch = self.patch_bank.get(patch_id)
        if patch is None:
            return []

        events: List[OPL2Event] = []
        mod_reg = MOD_OFFSETS[channel]
        car_reg = CAR_OFFSETS[channel]

        events.append(OPL2Event(0x20 + mod_reg, patch["m_ave"]))
        events.append(OPL2Event(0x20 + car_reg, patch["c_ave"]))
        events.append(OPL2Event(0x40 + mod_reg, patch["m_ksl"]))
        events.append(OPL2Event(0x40 + car_reg, patch["c_ksl"]))
        events.append(OPL2Event(0x60 + mod_reg, patch["m_atdec"]))
        events.append(OPL2Event(0x60 + car_reg, patch["c_atdec"]))
        events.append(OPL2Event(0x80 + mod_reg, patch["m_susrel"]))
        events.append(OPL2Event(0x80 + car_reg, patch["c_susrel"]))
        events.append(OPL2Event(0xE0 + mod_reg, patch["m_wave"]))
        events.append(OPL2Event(0xE0 + car_reg, patch["c_wave"]))
        events.append(OPL2Event(0xC0 + channel, patch["feedback"]))
        return events

    def note_on(self, channel: int, midi_note: int) -> List[OPL2Event]:
        lo, hi = self.midi_to_fnum(midi_note)
        hi_keyon = hi | 0x20
        events = [
            OPL2Event(0xA0 + channel, lo),
            OPL2Event(0xB0 + channel, hi_keyon),
        ]
        self.voice_state[channel].note = midi_note
        self.voice_state[channel].fnum_hi = hi_keyon & 0x1F
        self.voice_state[channel].active = True
        return events

    def note_off(self, channel: int) -> List[OPL2Event]:
        state = self.voice_state[channel]
        if not state.active:
            return []

        # Preserve the octave/fnum high bits while clearing the key-on bit.
        events = [OPL2Event(0xB0 + channel, state.fnum_hi & 0x1F)]
        state.active = False
        state.note = 0
        return events

    def volume_set(self, channel: int, volume_63: int, note: int, inst: int) -> List[OPL2Event]:
        """Set TL without dropping the note; this avoids click artifacts."""
        state = self.voice_state[channel]
        tl = self.velocity_to_opl_tl(volume_63)
        reg = 0x40 + CAR_OFFSETS[channel]
        state.volume = volume_63
        state.tl = tl
        ksl_bits = self.channel_ksl.get(channel, 0) & 0xC0
        return [OPL2Event(reg, ksl_bits | tl)]

    def route_drum(self, channel: int, note: int, volume: int) -> List[OPL2Event]:
        """Use the RPTracker custom drum patches and route by note range."""
        events: List[OPL2Event] = []

        if note <= 0:
            return events

        if note < 50:
            drum_patch = 253
        elif note < 75:
            drum_patch = 254
        else:
            drum_patch = 255

        events.extend(self.patch_set(channel, drum_patch))
        if not self.voice_state[channel].active or self.voice_state[channel].note != note:
            events.extend(self.note_on(channel, note))
        events.extend(self.volume_set(channel, volume, note, drum_patch))
        return events

    def translate_track(self, rpt) -> List[OPL2Event]:
        """Convert a current RPT pattern into a proper voice-state OPL2 stream."""
        out: List[OPL2Event] = []
        ticks_per_row = max(1, int(round(900.0 / max(1, getattr(rpt, "bpm", 150)))))

        for pattern_index in range(rpt.song_length):
            for row_index in range(32):
                cell_index = pattern_index * 32 + row_index
                if cell_index >= len(rpt.patterns):
                    continue

                row = rpt.patterns[cell_index]
                row_events: List[OPL2Event] = []
                for ch, cell in enumerate(row):
                    if ch >= 9:
                        continue

                    note = getattr(cell, "note", 0)
                    inst = getattr(cell, "inst", 0)
                    vol = getattr(cell, "vol", 0)

                    if ch in (3, 4):
                        row_events.extend(self.route_drum(ch, note, vol))
                        continue

                    state = self.voice_state[ch]

                    if note <= 0:
                        if state.active:
                            row_events.extend(self.note_off(ch))
                        continue

                    patch_changed = state.patch != inst
                    note_changed = state.note != note

                    if patch_changed:
                        if state.active:
                            row_events.extend(self.note_off(ch))
                        row_events.extend(self.patch_set(ch, inst))
                        state.patch = inst
                        self.channel_ksl[ch] = self.patch_bank.get(inst, {}).get("c_ksl", 0)
                        state.volume = -1
                        state.tl = -1

                    if state.active and note_changed:
                        row_events.extend(self.note_off(ch))

                    if patch_changed or not state.active or note_changed:
                        row_events.extend(self.note_on(ch, note))

                    if patch_changed or note_changed or state.volume != vol or state.tl < 0:
                        row_events.extend(self.volume_set(ch, vol, note, inst))

                if row_events:
                    row_events[-1].delay = ticks_per_row
                    out.extend(row_events)
                else:
                    out.append(OPL2Event(0x01, 0x20, ticks_per_row))

        return out


def translate_nsf_track(nsf_path: str, track_idx: int = 0, loops: int = 1) -> List[OPL2Event]:
    converter = NSFConverter(nsf_path)
    rpt = converter.convert_track(track_idx)
    translator = OPL2Translator()
    events: List[OPL2Event] = []
    for _ in range(max(1, loops)):
        translator.voice_state = {i: VoiceState() for i in range(9)}
        translator.channel_ksl = {i: 0 for i in range(9)}
        events.extend(translator.translate_track(rpt))
    return events


def export_all_tracks(nsf_path: str, output_dir: str) -> List[str]:
    converter = NSFConverter(nsf_path)
    translator = OPL2Translator()
    os.makedirs(output_dir, exist_ok=True)
    exported: List[str] = []

    for track_idx in range(converter.total_songs):
        rpt = converter.convert_track(track_idx)
        events = translator.translate_track(rpt)
        out_path = os.path.join(output_dir, f"track_{track_idx:02d}.json")
        with open(out_path, "w", encoding="utf-8") as fh:
            json.dump([e.to_tuple() for e in events], fh, separators=(",", ":"))
        exported.append(out_path)

    return exported


def serialize_events_to_bin(events: List[OPL2Event], out_path: str) -> int:
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    raw = b"".join(event.to_bin() for event in events)
    raw += OPL2Event.end_marker()
    raw += b"\x00" * ((-len(raw)) % 512)
    with open(out_path, "wb") as fh:
        fh.write(raw)
    return len(raw)


def main() -> None:
    parser = argparse.ArgumentParser(description="Translate NSF tracks into an OPL2 event stream.")
    parser.add_argument("--nsf", default="NSF/Pac-Man_CE.nsf", help="Path to the NSF file")
    parser.add_argument("--track", type=int, default=0, help="Track index to translate")
    parser.add_argument("--all", action="store_true", help="Export all tracks to JSON event files")
    parser.add_argument("--out-dir", default="music/generated", help="Output directory for translated event files")
    parser.add_argument("--bin-out", default=None, help="Optional path for a generated RP6502 music binary (.BIN) file")
    parser.add_argument("--loops", type=int, default=1, help="How many times to render the imported track before the loop marker")
    parser.add_argument("--limit", type=int, default=20, help="How many events to print for the selected track")
    args = parser.parse_args()

    if not os.path.exists(args.nsf):
        raise FileNotFoundError(f"NSF file not found: {args.nsf}")

    if args.all:
        exported = export_all_tracks(args.nsf, args.out_dir)
        print(f"Generated {len(exported)} translated track files in {args.out_dir}")
        return

    converter = NSFConverter(args.nsf)
    print(f"Loaded NSF: {args.nsf}")
    print(f"Title: {converter.title}")
    print(f"Tracks: {converter.total_songs}")

    events = translate_nsf_track(args.nsf, args.track, loops=args.loops)
    print(f"Track {args.track} event count: {len(events)}")
    if args.bin_out:
        size = serialize_events_to_bin(events, args.bin_out)
        print(f"Wrote {size} bytes to {args.bin_out}")
    for event in events[: args.limit]:
        print(event)


if __name__ == "__main__":
    main()
