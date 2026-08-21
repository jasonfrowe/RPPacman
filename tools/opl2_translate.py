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
# melodic NES/N163 lanes. One physical channel per lane (5 lanes, 5
# channels) -- measured on track 0: all 5 lanes are simultaneously active in
# 21.5% of rows and 4+ in 49.8%, so a smaller shared pool would mean
# frequent audible voice-stealing, not a rare edge case. acquire_channel's
# stealing logic stays in place as a safety net (it should now never
# actually need to fire for this track set, since demand never exceeds
# pool size), not as the primary allocation strategy.
LOGICAL_MELODIC_SOURCES = [0, 1, 2, 5, 6]  # sq1, sq2, tri, n163_0, n163_1

# source id -> (NSFConverter.build_frame_history() key, instrument patch id)
MELODIC_SOURCE_INFO = {
    0: ('sq1', 80),
    1: ('sq2', 81),
    2: ('tri', 33),
    5: ('n163_0', 38),
    6: ('n163_1', 39),
}
MELODIC_POOL = [0, 1, 2, 3, 4]             # physical channels shared by the above

# Physical channel 5 is intentionally left unused here: it's the budget
# reserved for future interactive SFX (eating pellets, ghosts, dying).
SFX_RESERVED_CHANNELS = [5]

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

# Per-logical-source mix trim, added to the computed TL (positive = quieter,
# negative = louder). Ear-tuned balance, not derived from anything: sq1 and
# n163_0 (the first two voices) were dominating the mix, triangle (the
# voice that follows) was getting buried under them, so sq1/n163_0 come
# down and triangle's earlier heavy attenuation is mostly backed off.
MIX_TRIM_BY_SOURCE = {0: 4, 2: 10, 5: 4, 6: 6}

# Per-logical-source fine-tune in cents, added on top of the exact f-number
# computation in midi_to_fnum (positive = sharper). Empty by default; this
# is an experimental knob for chasing a specific voice's "out of tune"
# complaint, not a standing correction -- the f-number math itself is exact
# now (see midi_to_fnum), so a nonzero entry here is a deliberate detune,
# not a bug fix.
FINE_TUNE_CENTS_BY_SOURCE: Dict[int, float] = {}


class OPL2Translator:
    def __init__(self):
        self.voice_state: Dict[int, VoiceState] = {i: VoiceState() for i in range(9)}
        self.channel_ksl: Dict[int, int] = {i: 0 for i in range(9)}
        self.logical_channel: Dict[int, int] = {s: -1 for s in LOGICAL_MELODIC_SOURCES}
        self.rhythm_reg = 0x00
        self.prev_noise_bit = 0
        self.prev_dmc_note = 0
        self.tick = 0
        # Pac-Man CE leans toward a bright, punchy arcade synth: tight lead,
        # clipped bass, and dry support voices with little extra resonance.
        self.patch_bank: Dict[int, Dict[str, int]] = {
              # Inst 80 (sq1): user-directed swap to RPTracker's gm_bank[0x3B]
              # (Muted Trumpet) after isolated review. Decay rate (low
              # nibble of *_atdec) raised from 1/4 to 9/9 -- same issue and
              # same fix as inst 38: with glide (no retrigger) the envelope
              # only attacks once, so RPTracker's original slow decay was
              # audible as a many-second fade instead of a quick settle to
              # the sustain level.
              # Carrier sustain level (high nibble of c_susrel) also dropped
              # to 0: at SL=6 the envelope's own attenuation was stacking
              # with our per-note TL/mix-trim attenuation, landing well
              # below audible even after the decay-rate fix. Our own
              # per-note volume system is the intended sole loudness
              # authority for a held note; the patch's sustain level should
              # just be "as loud as the envelope gets", not a second
              # independent attenuation on top.
              # Now that the triangle investigation confirmed timing is
              # correct, revisited sq1's patch for the same class of
              # perceptual-pitch-instability issue: gm_bank[0x3B]'s
              # modulator has VIB (vibrato) enabled -- wobbles the
              # modulator's own frequency over time, which in FM mode
              # continuously varies the effective modulation index (and so
              # the carrier's timbre) rather than shifting the carrier's
              # actual pitch. That reads as an unstable/uncertain pitch,
              # not a consistent sharp or flat -- matches "off, can't tell
              # which way" better than a real tuning error would. Feedback
              # was also at 7/7, the maximum: heavy modulator self-feedback
              # pushes toward dense, less cleanly-tonal harmonic content,
              # compounding the same effect. VIB cleared on the modulator;
              # feedback eased from 7 to 3.
              # Follow-up: modulator also had KSR (key-scale rate) set
              # while the carrier didn't -- that scales the modulator's own
              # envelope timing by pitch but leaves the carrier's alone, so
              # the same instrument's attack/decay character subtly shifts
              # across the octave range instead of staying consistent. Real
              # NES pulse channels have no such pitch-dependent envelope
              # scaling at all. Cleared to match the carrier.
              80: {"m_ave": 0x21, "m_ksl": 0x1C, "m_atdec": 0x59, "m_susrel": 0x03, "m_wave": 0x00,
                  "c_ave": 0x21, "c_ksl": 0x00, "c_atdec": 0x59, "c_susrel": 0x07, "c_wave": 0x00,
                  "feedback": 0x06},
              81: {"m_ave": 0x41, "m_ksl": 0x0C, "m_atdec": 0xF2, "m_susrel": 0xFF, "m_wave": 0x00,
                  "c_ave": 0x11, "c_ksl": 0x00, "c_atdec": 0xF2, "c_susrel": 0xFF, "c_wave": 0x00,
                  "feedback": 0x02},
              # Inst 33 (triangle): abandoning the "Square Lead"/GM-patch
              # lineage entirely. Timing is now confirmed correct (verified
              # against real hardware and the user's own reference
              # recording -- the ~7s entrance is real, not a decode bug),
              # so the remaining "flat"/off complaint is a genuine timbre
              # problem, not a hidden pitch or timing bug. Root cause:
              # gm_bank[0x50]'s modulator ran at TL=29/63 (fairly strong FM
              # depth) through a quarter-sine wave (m_wave=3) -- a bright,
              # asymmetric, harmonically busy tone. A real NES triangle is
              # the opposite: a clean, quickly-rolling-off wave with almost
              # all its energy in the fundamental, which is *why* it's so
              # easy for the ear to pitch-track -- any patch with prominent
              # non-fundamental energy will read as "off" even at an exact
              # fundamental frequency, purely from psychoacoustic pitch
              # ambiguity. Redesigned as a near-pure sine: modulator TL
              # raised to 58/63 (very quiet -- light FM coloration, not
              # silence) and switched from quarter-sine to plain sine to
              # remove the asymmetric buzz; carrier stays sine. Same
              # fast-attack/decay=9/carrier-SL=0 envelope shape already
              # established as necessary for glide-without-fade.
              # "Out of tune / not arcade-like": the near-pure-sine FM
              # redesign kept pitch exact but made the carrier the *only*
              # source of any energy -- against a busy mix, a single
              # unreinforced sine partial is easy for the ear to lose track
              # of, which reads as pitch ambiguity even though nothing is
              # mistuned. Switched to additive connection (bit0 of
              # feedback) so the carrier stays a completely clean,
              # unmodulated sine (pitch never gets touched by the
              # modulator) while the modulator becomes an independent,
              # quiet overtone instead of an FM modulator -- MULT raised to
              # 2 (an octave-up partial) at a moderate-quiet TL for a
              # little arcade sparkle without destabilizing the
              # fundamental.
              # More fuzz/harmonics to blend it in rather than stand out
              # as a lone pure tone: a real NES triangle wave's spectrum is
              # odd harmonics only (1st, 3rd, 5th...) -- MULT=2 on the
              # overtone was actually an *even* harmonic a real triangle
              # never has, which may have been working against the "arcade"
              # blend rather than helping. Switched to MULT=3 (a real odd
              # partial), raised its level a bit (TL 40->32) for more
              # presence, and added a touch of self-feedback (0->2) for
              # extra grit in that overtone specifically. Carrier stays
              # exactly as before: clean, unmodulated, additive connection
              # -- its own pitch is still never touched.
              33: {"m_ave": 0x23, "m_ksl": 0x20, "m_atdec": 0xF9, "m_susrel": 0x0F, "m_wave": 0x00,
                  "c_ave": 0x21, "c_ksl": 0x00, "c_atdec": 0xF9, "c_susrel": 0x08, "c_wave": 0x00,
                 "feedback": 0x05},
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
            # First real listen confirmed it: too loud, lacks bass. Both
            # traced to values rather than the rhythm-mode approach itself
            # -- BD's own fixed pitch (see rhythm_setup) was MIDI 48
            # (~131Hz, tom range, not sub-bass), and both operators' TL
            # were near max loud (0/8) with no headroom against the rest of
            # the mix. Carrier TL raised 0->12 and modulator TL raised
            # 8->16 for more mixing headroom; pitch itself is fixed in
            # rhythm_setup(), not here.
            # Kick down a notch: TL 16/12 -> 22/18.
            # Not actually missing (patch and trigger logic untouched,
            # firing correctly) -- relatively buried instead. Its own TL
            # was tuned before the melodic volume-rescale fix (see
            # volume_set) raised sq1/tri/n163_0/n163_1 by ~6dB; rhythm
            # voices don't go through that rescale, so kick's absolute
            # level never moved and it fell behind. TL eased ~6dB (8 steps)
            # to restore its prior relative presence.
            # Still too quiet -- another ~6dB (8 steps).
            # "Shallow": volume alone wasn't the issue -- lacked real boom.
            # Feedback (self-modulation on the carrier) raised 3->6 for a
            # fuller, more harmonically rich low end; carrier release
            # slowed (rate 8->3, lower rate = slower/longer decay) and
            # given a modest sustain level (0->3) for an actual "thump...
            # boom" tail instead of a short click.
            253: {  # bass drum (channel 6, both operators)
                  "m_ave": 0x01, "m_ksl": 0x06, "m_atdec": 0xF8, "m_susrel": 0x57, "m_wave": 0x00,
                  "c_ave": 0x01, "c_ksl": 0x02, "c_atdec": 0xFA, "c_susrel": 0x33, "c_wave": 0x00,
                  "feedback": 0x0C},
            # Same story as kick: TL=25 was tuned before the melodic
            # volume-rescale fix raised sq1/tri/n163_0/n163_1 by ~6dB, and
            # rhythm voices don't go through that rescale -- eased ~6dB
            # (8 steps) to match.
            # Snare (carrier) reverted back to unused -- cymbal routing
            # restored (see process_rhythm) -- so its TL is moot again;
            # left at its original pre-routing-change value.
            # Still too quiet -- another ~6dB (8 steps).
            254: {  # channel 7: modulator = hi-hat, carrier = snare
                  # In rhythm mode HH/SD are not FM-chained -- each operator
                  # is independently audible -- so *_ksl's TL directly sets
                  # each voice's own loudness.
                  "m_ave": 0x02, "m_ksl": 0x09, "m_atdec": 0xFF, "m_susrel": 0x0F, "m_wave": 0x00,
                  "c_ave": 0x01, "c_ksl": 0x00, "c_atdec": 0xFA, "c_susrel": 0x39, "c_wave": 0x00,
                  "feedback": 0x04},
            # Tom's envelope shape reverted back to its own original
            # character (attack/decay/sustain/release/waveform) -- reusing
            # kick's exact envelope didn't read well in context.
            255: {  # channel 8: modulator = tom-tom, carrier = top cymbal
                  "m_ave": 0x01, "m_ksl": 0x09, "m_atdec": 0xF9, "m_susrel": 0x48, "m_wave": 0x00,
                  "c_ave": 0x02, "c_ksl": 0x09, "c_atdec": 0xF6, "c_susrel": 0x23, "c_wave": 0x00,
                  "feedback": 0x02},
            # Inst 38 (N163 ch0, the low voice): user-directed swap to
            # RPTracker's gm_bank[0x18] (Nylon Guitar) after further review
            # of the Harpsichord attempt. EGT (bit5 of *_ave) flipped from
            # RPTracker's original 0 (percussive: decays through to silence
            # even while held, correct for a plucked guitar played as
            # discrete notes) to 1 (sustaining: holds at the sustain level
            # while key-on stays high). Needed once note changes within a
            # phrase started gliding instead of retriggering (see
            # note_glide, added to fix clicking): a percussive envelope
            # never gets re-attacked mid-phrase, so it just decays to
            # silence over the run instead of holding. Attack/decay/sustain/
            # release/waveform/feedback otherwise unchanged from the chosen
            # patch.
            # Decay rate (low nibble of *_atdec) raised from RPTracker's
            # original 1 (near-instant Attack, then an extremely slow crawl
            # down to the sustain level -- fine for a retriggered note, but
            # with glide only attacking once, that slow decay was the whole
            # audible "fade": it was still crawling toward sustain many
            # seconds into the phrase) to 9, so it settles at the sustain
            # level within a fraction of a second and then actually holds
            # there for the rest of the run.
            # Carrier sustain level also dropped to 0 -- same reasoning as
            # inst 80: it was stacking with our own per-note attenuation
            # instead of just being "as loud as the envelope gets".
            # Feedback bumped from 0x06 (FB=3) to sq1's 0x0E (FB=7) -- sq1
            # ("now quite good") is otherwise structurally the same patch
            # (MULT=1 on both operators, sustain-type envelope, series
            # connection); feedback amount is the main OPL2 lever for how
            # much a sine operator buzzes/grits up via self-modulation, and
            # is the most direct thing to borrow for the grit this lost.
            # "No upper end buzz": feedback was already at max (7) and the
            # modulator wasn't especially quiet (TL=13), so pushed further
            # -- modulator MULT raised 1->2 (an actual higher-harmonic
            # generator, not just louder FM depth at the same rate) and TL
            # dropped 13->6 for a deeper FM index. Carrier MULT left at 1
            # so the fundamental pitch doesn't move.
            38: {"m_ave": 0x22, "m_ksl": 0x46, "m_atdec": 0xF9, "m_susrel": 0x55, "m_wave": 0x00,
                 "c_ave": 0x31, "c_ksl": 0x02, "c_atdec": 0xF9, "c_susrel": 0x04, "c_wave": 0x00,
                 "feedback": 0x0E},
            # Inst 39 (n163_1): never got the same fix applied to
            # 38/80/33 earlier, since it's not audible until ~49s in and
            # so hadn't been reviewed yet. Same bug: EGT=0 (percussive --
            # decays to silence regardless of key-on state) with a very
            # slow decay rate (1), so a held note under glide slowly fades/
            # warbles instead of holding steady -- reads as "weird". EGT
            # set to sustain (1) on both operators, decay raised 1->9,
            # carrier SL zeroed (our own volume system is the sole
            # loudness authority for a held note, same reasoning as the
            # other patches), modulator SL zeroed too for consistency.
            # This voice's actual note data (MIDI 24-37, real sub-bass/bass
            # territory) marks it as a bass line, not a lead -- and the
            # modulator was at TL=0, the absolute maximum FM depth. Heavy
            # FM modulation is much more perceptually unstable at a low
            # fundamental than at a higher pitch (the modulator's own
            # frequency, MULT=1, is equally low, and a deep, slow-beating
            # modulation at that register reads as "warble" rather than
            # brightness). Same fix as triangle's redesign: quiet the
            # modulator drastically (0 -> 50) for a cleaner, closer-to-sine
            # bass tone instead of a dense, unstable FM voice.
            # Still a warble at TL=50 -- pushed further toward the
            # near-silent-modulator approach that worked for triangle
            # (TL=58 there); this is a low bass register so the residual
            # modulation index at TL=50 was apparently still enough to
            # read as unstable. TL 50->60, close to fully silent.
            39: {"m_ave": 0x21, "m_ksl": 0x3C, "m_atdec": 0xF9, "m_susrel": 0x04, "m_wave": 0x00,
                 "c_ave": 0x21, "c_ksl": 0x00, "c_atdec": 0xF9, "c_susrel": 0x04, "c_wave": 0x00,
                 "feedback": 0x00},
        }

    OPL_CLOCK_HZ = 3579545.0

    @classmethod
    def midi_to_fnum(cls, midi_note: int, cents: float = 0.0) -> tuple[int, int]:
        """Convert a MIDI note (optionally detuned by `cents`) to an exact
        OPL2 block/f-number pair, computed directly from the target
        frequency rather than a fixed 12-entry table.

        The fixed table (one f-num per pitch class, reused every octave)
        this replaced was only accurate to within about -3/+4 cents of true
        equal temperament -- small, usually inaudible on a single tone, but
        it meant there was no way to deliberately nudge a specific voice's
        tuning without picking a different, still-imprecise table entry.
        Computing the f-number directly from the target frequency removes
        that rounding and makes `cents` a real, exact fine-tune knob.
        """
        note = max(12, min(127, midi_note))
        freq = 440.0 * (2.0 ** ((note - 69) / 12.0)) * (2.0 ** (cents / 1200.0))

        # Pick the lowest block where the f-number still fits in 10 bits --
        # that maximizes f-num (and so cents-per-step resolution) for this
        # frequency. block=(note-10)//12 mirrors the old table's block
        # choice for a normal note, but a large `cents` offset near an
        # octave boundary could shift which block actually fits, so search
        # explicitly rather than assume it.
        best = None
        for block in range(8):
            fnum = round(freq * 72.0 * (2 ** (20 - block)) / cls.OPL_CLOCK_HZ)
            if 0 <= fnum <= 1023:
                best = (fnum, block)
                break
        if best is None:
            # Outside the representable range entirely (only possible at
            # the extreme ends of the MIDI range with a large detune) --
            # clamp to the nearest edge rather than producing garbage.
            fnum = round(freq * 72.0 * (2 ** 20) / cls.OPL_CLOCK_HZ)
            best = (0, 0) if fnum < 0 else (1023, 7)
        f_num, block = best

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

    def note_on(self, channel: int, midi_note: int, cents: float = 0.0) -> List[OPL2Event]:
        lo, hi = self.midi_to_fnum(midi_note, cents)
        hi_keyon = hi | 0x20
        events = [
            OPL2Event(0xA0 + channel, lo),
            OPL2Event(0xB0 + channel, hi_keyon),
        ]
        self.voice_state[channel].note = midi_note
        self.voice_state[channel].fnum_hi = hi_keyon & 0x1F
        self.voice_state[channel].active = True
        return events

    def note_glide(self, channel: int, midi_note: int, cents: float = 0.0) -> List[OPL2Event]:
        """Change pitch on an already-sounding voice without retriggering it.

        Writing new f-num/block bytes while the key-on bit (0xB0 bit 5)
        stays set changes the OPL2 channel's pitch in place; the envelope
        keeps running rather than restarting. Used for same-instrument
        pitch changes so a fast arpeggio doesn't force a fresh attack (and
        the click/pluck that comes with it) on every note.
        """
        lo, hi = self.midi_to_fnum(midi_note, cents)
        hi_keyon = hi | 0x20
        events = [
            OPL2Event(0xA0 + channel, lo),
            OPL2Event(0xB0 + channel, hi_keyon),
        ]
        self.voice_state[channel].note = midi_note
        self.voice_state[channel].fnum_hi = hi_keyon & 0x1F
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
        # The caller's `vol` is always the NES chip's native 0-15 volume/
        # envelope range (4 bits), never actually 0-63 despite the
        # parameter name -- velocity_to_opl_tl(15) only reaches TL~21, so
        # without rescaling here every melodic note was permanently capped
        # ~11-12dB below OPL2's actual loudest output, regardless of any
        # per-source trim. Rescale 0-15 -> 0-63 before the log curve; keep
        # state.volume in the original 0-15 units since process_melodic
        # compares it against the raw incoming `vol` to detect changes.
        rescaled = min(63, volume_63 * 63 // 15) if volume_63 > 0 else 0
        tl = self.velocity_to_opl_tl(rescaled)
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
        # BD dropped further, MIDI 36 (~65Hz) -> 30 (~46Hz) -- "shallow"
        # feedback: needed more real sub-bass weight, not just volume.
        for phys, note in ((RHYTHM_BD_CH, 30), (RHYTHM_HHSD_CH, 84), (RHYTHM_TOMCYM_CH, 65)):
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

        Only fires on a genuine attack -- silence-to-active, or a change of
        target voice mid-streak -- not on every row of a held streak. Noise
        hits in Pac-Man CE run in multi-row streaks; retriggering every row
        never lets the (deliberately slower-decaying) cymbal/tom voices
        actually ring, which is most of why the drums were inaudible.
        """
        bits = 0
        if dmc_note > 0 and self.prev_dmc_note <= 0:
            bits |= RYT_BD
        self.prev_dmc_note = dmc_note

        if noise_note > 0:
            if noise_note >= 74:
                target = RYT_HH
            elif noise_note >= 55:
                target = RYT_TOM
            else:
                # Reverted: routing this bucket to snare (in place of
                # cymbal) made kick+snare read as "frog"-like together and
                # lost cymbal made the kit feel thin/missing. Back to
                # cymbal.
                target = RYT_CYM
            if target != self.prev_noise_bit:
                bits |= target
            self.prev_noise_bit = target
        else:
            self.prev_noise_bit = 0

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
        inaudible). Only as a last resort -- all 4 channels actively
        sounding at once -- does it steal, and purely by least-recently
        triggered (not by TL/loudness: TL reflects intentional per-source
        mix trims like the triangle's, so using it here would make any
        deliberately-quieter voice the perpetual first victim, cutting it
        off mid-note instead of just sitting back in the mix).
        """
        phys = self.logical_channel.get(source, -1)
        if phys != -1:
            return phys, []

        idle = [p for p in MELODIC_POOL if not self.voice_state[p].active]
        pool = idle if idle else MELODIC_POOL
        phys = min(pool, key=lambda p: self.voice_state[p].last_tick)

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

        cents = FINE_TUNE_CENTS_BY_SOURCE.get(source, 0.0)
        if patch_changed or not state.active:
            events.extend(self.note_on(phys, note, cents))
            state.last_tick = self.tick
        elif note_changed:
            # Same instrument, already sounding: glide instead of
            # retriggering, so a fast arpeggio doesn't click on every note.
            events.extend(self.note_glide(phys, note, cents))
            state.last_tick = self.tick

        if patch_changed or note_changed or state.volume != vol or state.tl < 0:
            events.extend(self.volume_set(phys, vol, note, inst, source))

        return events

    def translate_frame_history(self, history) -> List[OPL2Event]:
        """Build the OPL2 event stream directly from real 60Hz NSF frames.

        No row/pattern quantization: 1 history frame is 1 tick in the
        runtime's delay units (both are the real 60Hz vsync rate the game
        actually runs at), so there's no frames_per_row/bpm conversion to
        get wrong, and nothing that changes faster than a fixed row width
        -- like the triangle's linear-counter gate -- gets lost to a
        majority vote. process_melodic/process_rhythm already only emit
        register writes when something actually changes; calling them once
        per real frame instead of once per artificially-widened row is all
        that's needed.
        """
        # Fully self-contained: reset all per-translation state here so a
        # translator instance can safely be reused across tracks or not.
        self.voice_state = {i: VoiceState() for i in range(9)}
        self.channel_ksl = {i: 0 for i in range(9)}
        self.logical_channel = {s: -1 for s in LOGICAL_MELODIC_SOURCES}
        self.prev_noise_bit = 0
        self.prev_dmc_note = 0
        self.tick = 0

        out: List[OPL2Event] = []
        out.extend(self.rhythm_setup())
        last_index = 0  # frame index the most recent emitted batch corresponds to

        for i, frame in enumerate(history):
            self.tick += 1
            frame_events: List[OPL2Event] = []

            for source, (hist_key, inst) in MELODIC_SOURCE_INFO.items():
                note, vol = frame[hist_key]
                frame_events.extend(self.process_melodic(source, note, inst, vol))

            frame_events.extend(self.process_rhythm(frame['noise'][0], frame['dmc'][0]))

            if frame_events:
                # The wait since the previous batch belongs on the
                # *previous* batch's trailing event (it's how long the
                # player idles before reaching this one), not on this
                # batch's own trailing event -- attaching it here instead
                # shifted every event's timing by one gap and, worse, made
                # the very first batch play instantly at t=0 whenever
                # nothing preceded it (e.g. a solo render with no rhythm
                # setup), since there was nothing yet to attach a leading
                # wait to.
                gap = i - last_index
                if gap > 0:
                    if out:
                        out[-1].delay = gap
                    else:
                        out.append(OPL2Event(0x01, 0x20, gap))
                out.extend(frame_events)
                last_index = i

        return out


def translate_nsf_track(nsf_path: str, track_idx: int = 0, loops: int = 1) -> List[OPL2Event]:
    converter = NSFConverter(nsf_path)
    history = converter.build_subframe_history(track_idx)
    translator = OPL2Translator()
    events: List[OPL2Event] = []
    for _ in range(max(1, loops)):
        events.extend(translator.translate_frame_history(history))
    return events


def export_all_tracks(nsf_path: str, output_dir: str) -> List[str]:
    converter = NSFConverter(nsf_path)
    translator = OPL2Translator()
    os.makedirs(output_dir, exist_ok=True)
    exported: List[str] = []

    for track_idx in range(converter.total_songs):
        history = converter.build_subframe_history(track_idx)
        events = translator.translate_frame_history(history)
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
