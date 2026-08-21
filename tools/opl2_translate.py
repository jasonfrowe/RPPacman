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
    owner: int = -1       # logical melodic source currently assigned to this physical channel, -1 = free
    last_tick: int = 0    # row tick this channel was last (re)triggered, for voice-steal aging


# Pattern-channel indices (tools/import_nsf.py's RPT4 layout) for the five
# melodic NES/N163 lanes that share four physical OPL2 channels dynamically.
LOGICAL_MELODIC_SOURCES = [0, 1, 2, 5, 6]  # sq1, sq2, tri, n163_0, n163_1
MELODIC_POOL = [0, 1, 2, 3]                # physical channels shared by the above

# Physical channels 4-5 are intentionally left unused here: they're the
# budget reserved for future interactive SFX (eating pellets, ghosts, dying).
SFX_RESERVED_CHANNELS = [4, 5]

# Physical channels 6-8 are dedicated to real OPL2 rhythm mode (register
# 0xBD) instead of faking percussion with ordinary 2-op FM voices. This is
# the only way to get the chip's actual hardware noise generator, which is
# what a hi-hat/cymbal/snare needs to not sound like a pitched beep.
RHYTHM_BD_CH = 6      # bass drum: both operators
RHYTHM_HHSD_CH = 7    # modulator = hi-hat, carrier = snare
RHYTHM_TOMCYM_CH = 8  # modulator = tom-tom, carrier = top cymbal

RYT_ENABLE = 0x20  # register 0xBD bit 5
RYT_BD = 0x10
RYT_SD = 0x08
RYT_TOM = 0x04
RYT_CYM = 0x02
RYT_HH = 0x01

# Per-logical-source mix trim, added to the computed TL (positive = quieter).
# Triangle is always reported at max NES volume (the chip has no volume
# register), so without this it dominates the mix; n163_0/n163_1 trims
# preserve the balance already tuned by ear before this change.
MIX_TRIM_BY_SOURCE = {2: 10, 5: -6, 6: 6}


class OPL2Translator:
    def __init__(self):
        self.voice_state: Dict[int, VoiceState] = {i: VoiceState() for i in range(9)}
        self.channel_ksl: Dict[int, int] = {i: 0 for i in range(9)}
        self.logical_channel: Dict[int, int] = {s: -1 for s in LOGICAL_MELODIC_SOURCES}
        self.rhythm_reg = 0x00
        self.tick = 0
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
            # 253/254/255 are no longer melodic-FM "drum" patches faking
            # percussion; they're the real OPL2 rhythm-mode voices (register
            # 0xBD), applied to channels 6/7/8 by rhythm_setup(). EGT (bit 5
            # of *_ave) is deliberately 0 on every operator here: that's the
            # "percussive" envelope type, so each hit decays to silence on
            # its own release phase instead of holding at sustain like a
            # melodic note. These are a reasoned first pass (fast attacks,
            # short decay/release shaped per voice) rather than values
            # pulled from a published bank -- no authoritative byte-level
            # AdLib rhythm-kit reference turned up in research, so this is
            # the starting point to dial in by ear.
            253: {  # bass drum (channel 6, both operators)
                  "m_ave": 0x01, "m_ksl": 0x08, "m_atdec": 0xF8, "m_susrel": 0x57, "m_wave": 0x00,
                  "c_ave": 0x01, "c_ksl": 0x00, "c_atdec": 0xFA, "c_susrel": 0x48, "c_wave": 0x00,
                  "feedback": 0x06},
            254: {  # channel 7: modulator = hi-hat, carrier = snare
                  "m_ave": 0x02, "m_ksl": 0x10, "m_atdec": 0xFF, "m_susrel": 0x0F, "m_wave": 0x00,
                  "c_ave": 0x01, "c_ksl": 0x08, "c_atdec": 0xFA, "c_susrel": 0x39, "c_wave": 0x00,
                  "feedback": 0x04},
            255: {  # channel 8: modulator = tom-tom, carrier = top cymbal
                  "m_ave": 0x01, "m_ksl": 0x04, "m_atdec": 0xF9, "m_susrel": 0x48, "m_wave": 0x00,
                  "c_ave": 0x02, "c_ksl": 0x10, "c_atdec": 0xF6, "c_susrel": 0x23, "c_wave": 0x00,
                  "feedback": 0x02},
            # Inst 38 (N163 ch0, the low voice): switched from a parallel
            # (additive) connection with two plain sine operators -- which
            # is why it had no buzz, the modulator was just a second sine
            # tone summed in, never actually modulating anything -- to a
            # series/FM connection with a higher modulator multiple, a
            # harmonic-rich quarter-sine modulator waveform, and feedback,
            # so the modulator now actually injects overtones into the
            # carrier. Attack/decay/sustain/release left as before.
            38: {"m_ave": 0x22, "m_ksl": 0x02, "m_atdec": 0xF2, "m_susrel": 0x38, "m_wave": 0x03,
                 "c_ave": 0x21, "c_ksl": 0x00, "c_atdec": 0xF1, "c_susrel": 0x38, "c_wave": 0x00,
                 "feedback": 0x06},
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

    def volume_set(self, channel: int, volume_63: int, note: int, inst: int, source: int = -1) -> List[OPL2Event]:
        """Set TL without dropping the note; this avoids click artifacts."""
        state = self.voice_state[channel]
        tl = self.velocity_to_opl_tl(volume_63)
        tl = max(0, min(63, tl + MIX_TRIM_BY_SOURCE.get(source, 0)))
        reg = 0x40 + CAR_OFFSETS[channel]
        state.volume = volume_63
        state.tl = tl
        ksl_bits = self.channel_ksl.get(channel, 0) & 0xC0
        return [OPL2Event(reg, ksl_bits | tl)]

    def rhythm_setup(self) -> List[OPL2Event]:
        """Program channels 6-8 for real OPL2 rhythm mode and enable it."""
        events: List[OPL2Event] = []
        events.extend(self.patch_set(RHYTHM_BD_CH, 253))
        events.extend(self.patch_set(RHYTHM_HHSD_CH, 254))
        events.extend(self.patch_set(RHYTHM_TOMCYM_CH, 255))

        # Rhythm-mode channels are never key-on'd through 0xB0-0xB8 (that bit
        # is controlled by 0xBD instead); only their pitch is set here, once
        # -- see process_rhythm for why HH/TOM/CYM don't get a per-hit pitch.
        for phys, note in ((RHYTHM_BD_CH, 48), (RHYTHM_HHSD_CH, 84), (RHYTHM_TOMCYM_CH, 65)):
            lo, hi = self.midi_to_fnum(note)
            events.append(OPL2Event(0xA0 + phys, lo))
            events.append(OPL2Event(0xB0 + phys, hi & 0x1F))

        self.rhythm_reg = RYT_ENABLE
        events.append(OPL2Event(0xBD, self.rhythm_reg))
        return events

    def process_rhythm(self, noise_note: int, dmc_note: int) -> List[OPL2Event]:
        """Route the NES noise/DMC lanes into real rhythm-mode hits.

        DMC only ever reports a fixed "on" tone (see import_nsf.py), so it
        can only ever mean "kick hit". Noise carries a genuine pitch
        (NOISE_PERIOD_TO_MIDI), which becomes a choice of which rhythm voice
        fires rather than a continuously retuned pitch: OPL2's hi-hat/tom/
        cymbal derive their tone from a fixed hardware noise/phase
        combination on channels 7/8, not a freely retunable oscillator.
        """
        bits = 0
        if dmc_note > 0:
            bits |= RYT_BD
        if noise_note > 0:
            if noise_note >= 74:
                bits |= RYT_HH
            elif noise_note >= 55:
                bits |= RYT_TOM
            else:
                bits |= RYT_CYM

        if bits == 0:
            return []

        events: List[OPL2Event] = []
        # Force a fresh attack on every firing bit: clear then set, since a
        # bit already held high wouldn't re-trigger the envelope.
        off_reg = self.rhythm_reg & (~bits & 0xFF)
        if off_reg != self.rhythm_reg:
            events.append(OPL2Event(0xBD, off_reg))
        on_reg = self.rhythm_reg | bits
        events.append(OPL2Event(0xBD, on_reg))
        self.rhythm_reg = on_reg
        return events

    def acquire_channel(self, source: int) -> tuple[int, List[OPL2Event]]:
        """Return the physical channel for a melodic source, stealing if needed.

        Prefers a genuinely free channel, then an idle one (still "owned" by
        another source but not currently sounding a note -- reclaiming it is
        inaudible), and only as a last resort steals an actively-sounding
        voice, picking the quietest (highest TL) and then the least
        recently triggered.
        """
        phys = self.logical_channel.get(source, -1)
        if phys != -1:
            return phys, []

        idle = [p for p in MELODIC_POOL if not self.voice_state[p].active]
        pool = idle if idle else MELODIC_POOL
        phys = max(pool, key=lambda p: (self.voice_state[p].tl, -self.voice_state[p].last_tick))

        events: List[OPL2Event] = []
        old_owner = self.voice_state[phys].owner
        if old_owner not in (-1, source):
            if self.voice_state[phys].active:
                events.extend(self.note_off(phys))
            self.logical_channel[old_owner] = -1

        self.logical_channel[source] = phys
        self.voice_state[phys].owner = source
        return phys, events

    def process_melodic(self, source: int, note: int, inst: int, vol: int) -> List[OPL2Event]:
        events: List[OPL2Event] = []

        if note <= 0:
            phys = self.logical_channel.get(source, -1)
            if phys != -1 and self.voice_state[phys].active:
                events.extend(self.note_off(phys))
            return events

        phys, steal_events = self.acquire_channel(source)
        events.extend(steal_events)
        state = self.voice_state[phys]

        patch_changed = state.patch != inst
        note_changed = state.note != note

        if patch_changed:
            if state.active:
                events.extend(self.note_off(phys))
            events.extend(self.patch_set(phys, inst))
            state.patch = inst
            self.channel_ksl[phys] = self.patch_bank.get(inst, {}).get("c_ksl", 0)
            state.volume = -1
            state.tl = -1

        if state.active and note_changed:
            events.extend(self.note_off(phys))

        if patch_changed or not state.active or note_changed:
            events.extend(self.note_on(phys, note))
            state.last_tick = self.tick

        if patch_changed or note_changed or state.volume != vol or state.tl < 0:
            events.extend(self.volume_set(phys, vol, note, inst, source))

        return events

    def translate_track(self, rpt) -> List[OPL2Event]:
        """Convert a current RPT pattern into a proper voice-state OPL2 stream."""
        # Fully self-contained: reset all per-translation state here so a
        # translator instance can safely be reused across tracks or not.
        self.voice_state = {i: VoiceState() for i in range(9)}
        self.channel_ksl = {i: 0 for i in range(9)}
        self.logical_channel = {s: -1 for s in LOGICAL_MELODIC_SOURCES}
        self.tick = 0

        out: List[OPL2Event] = []
        ticks_per_row = max(1, int(round(900.0 / max(1, getattr(rpt, "bpm", 150)))))

        out.extend(self.rhythm_setup())

        for pattern_index in range(rpt.song_length):
            for row_index in range(32):
                cell_index = pattern_index * 32 + row_index
                if cell_index >= len(rpt.patterns):
                    continue

                self.tick += 1
                row = rpt.patterns[cell_index]
                row_events: List[OPL2Event] = []

                for source in LOGICAL_MELODIC_SOURCES:
                    cell = row[source]
                    row_events.extend(self.process_melodic(
                        source, getattr(cell, "note", 0), getattr(cell, "inst", 0), getattr(cell, "vol", 0)))

                noise_note = getattr(row[3], "note", 0)
                dmc_note = getattr(row[4], "note", 0)
                row_events.extend(self.process_rhythm(noise_note, dmc_note))

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
