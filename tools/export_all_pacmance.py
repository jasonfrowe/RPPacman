#!/usr/bin/env python3
"""
Exporter for Pac-Man Championship Edition NSF tracks to RP6502 OPL2 BIN files.
Converts all 22 tracks from NSF/Pac-Man_CE.nsf and saves them as:
  music/PacManCE_01.BIN .. music/PacManCE_22.BIN

OPL2 patch behavior:
- Starts with channel defaults from import_nsf.py instrument IDs
- Applies row instrument changes from each RPT cell dynamically
- Forces key-off/key-on retrigger for repeated notes (especially drums)
"""

import os
import sys
import argparse

# Import NSFConverter engine from RPTracker tools
sys.path.insert(0, '/Users/jasonrowe/Software/rp6502/RPTracker/tools')
from import_nsf import NSFConverter

# OPL2 F-Number table for Octave 4 @ 3.579545 MHz (matches src/opl.c)
FNUM_TABLE = [308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579]

# OPL2 register offset tables for channels 0..8
MOD_OFFSETS = [0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12]
CAR_OFFSETS = [0x03, 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x13, 0x14, 0x15]

def midi_to_opl_freq(midi_note):
    if midi_note < 12: midi_note = 12
    if midi_note > 107: midi_note = 107
    block = (midi_note - 12) // 12
    note_idx = (midi_note - 12) % 12
    if block > 7: block = 7
    f_num = FNUM_TABLE[note_idx]
    high_byte = 0x20 | (block << 2) | ((f_num >> 8) & 0x03)
    low_byte = f_num & 0xFF
    return high_byte, low_byte

def create_event(reg, val, delay=0):
    return bytes([reg & 0xFF, val & 0xFF, delay & 0xFF, (delay >> 8) & 0xFF])

def write_patch(events, ch, patch):
    m = MOD_OFFSETS[ch]
    c = CAR_OFFSETS[ch]
    events.extend(create_event(0x20 + m, patch.m_ave, 0))
    events.extend(create_event(0x20 + c, patch.c_ave, 0))
    events.extend(create_event(0x40 + m, patch.m_ksl, 0))
    events.extend(create_event(0x40 + c, patch.c_ksl, 0))
    events.extend(create_event(0x60 + m, patch.m_atdec, 0))
    events.extend(create_event(0x60 + c, patch.c_atdec, 0))
    events.extend(create_event(0x80 + m, patch.m_susrel, 0))
    events.extend(create_event(0x80 + c, patch.c_susrel, 0))
    events.extend(create_event(0xE0 + m, patch.m_wave, 0))
    events.extend(create_event(0xE0 + c, patch.c_wave, 0))
    events.extend(create_event(0xC0 + ch, patch.feedback, 0))

def clamp_u6(v):
    if v < 0:
        return 0
    if v > 63:
        return 63
    return v

def slew_tl(prev_tl, target_tl, max_step=6):
    if prev_tl is None:
        return target_tl
    if target_tl > prev_tl + max_step:
        return prev_tl + max_step
    if target_tl < prev_tl - max_step:
        return prev_tl - max_step
    return target_tl

def get_mix_trim(profile, ch_idx, inst_id, note):
    # Negative values make a channel louder by reducing TL attenuation.
    # Positive values tame loud channels.
    if profile == 'clean':
        # Base channel mix for cleaner profile.
        channel_trim = [
            2,   # ch0 pulse 1
            5,   # ch1 pulse 2
            1,   # ch2 triangle bass
            0,   # ch3 noise drum
            0,   # ch4 dmc drum
            10,  # ch5 n163 lead (was overpowering)
            2,   # ch6 n163 bass
            0,
            0,
        ]

        trim = channel_trim[ch_idx] if ch_idx < len(channel_trim) else 0

        # Instrument-specific correction layer.
        if inst_id == 38:   # N163 lead
            trim += 6
        elif inst_id == 39: # N163 bass
            trim += 2
        elif inst_id == 33: # Triangle bass
            trim += 1

        # Keep very low notes from overwhelming the mix.
        if note > 0 and note < 48:
            trim += 4
        elif note > 0 and note < 60:
            trim += 2

        return trim

    # Default neutral mix for other profiles.
    return 0

def apply_patch_profile(rpt, profile):
    # Patch profiles tuned for 2A03 + N163 translation.
    # Keep drums from the converter bank (Inst 115/118) and refine melodic IDs.

    if profile == 'clean':
        # Less modulation and softer harmonics for long listening sessions.
        p = rpt.user_bank[80]
        p.m_ave, p.c_ave = 0x21, 0x21
        p.m_ksl, p.c_ksl = 0x1E, 0x06
        p.m_atdec, p.c_atdec = 0xE4, 0xF4
        p.m_susrel, p.c_susrel = 0x17, 0x18
        p.m_wave, p.c_wave = 0x01, 0x01
        p.feedback = 0x02

        p = rpt.user_bank[81]
        p.m_ave, p.c_ave = 0x21, 0x21
        p.m_ksl, p.c_ksl = 0x20, 0x08
        p.m_atdec, p.c_atdec = 0xD4, 0xF3
        p.m_susrel, p.c_susrel = 0x18, 0x19
        p.m_wave, p.c_wave = 0x01, 0x01
        p.feedback = 0x02

        p = rpt.user_bank[33]
        p.m_ave, p.c_ave = 0x23, 0x21
        p.m_ksl, p.c_ksl = 0x10, 0x80
        p.m_atdec, p.c_atdec = 0xA8, 0xC4
        p.m_susrel, p.c_susrel = 0x46, 0x58
        p.m_wave, p.c_wave = 0x02, 0x01
        p.feedback = 0x06

        p = rpt.user_bank[38]
        p.m_ave, p.c_ave = 0x23, 0x21
        p.m_ksl, p.c_ksl = 0x0C, 0x06
        p.m_atdec, p.c_atdec = 0xE2, 0xF2
        p.m_susrel, p.c_susrel = 0x28, 0x29
        p.m_wave, p.c_wave = 0x01, 0x01
        p.feedback = 0x03

        p = rpt.user_bank[39]
        p.m_ave, p.c_ave = 0x22, 0x21
        p.m_ksl, p.c_ksl = 0x18, 0x8C
        p.m_atdec, p.c_atdec = 0xA8, 0xD3
        p.m_susrel, p.c_susrel = 0x46, 0x58
        p.m_wave, p.c_wave = 0x02, 0x01
        p.feedback = 0x05
        return

    if profile == 'bright':
        # More arcade bite and upper harmonics.
        p = rpt.user_bank[80]
        p.m_ave, p.c_ave = 0x21, 0x21
        p.m_ksl, p.c_ksl = 0x16, 0x00
        p.m_atdec, p.c_atdec = 0xF3, 0xF5
        p.m_susrel, p.c_susrel = 0x14, 0x16
        p.m_wave, p.c_wave = 0x01, 0x01
        p.feedback = 0x02

        p = rpt.user_bank[81]
        p.m_ave, p.c_ave = 0x21, 0x21
        p.m_ksl, p.c_ksl = 0x12, 0x00
        p.m_atdec, p.c_atdec = 0xF2, 0xF4
        p.m_susrel, p.c_susrel = 0x16, 0x18
        p.m_wave, p.c_wave = 0x01, 0x01
        p.feedback = 0x02

        p = rpt.user_bank[33]
        p.m_ave, p.c_ave = 0x01, 0x01
        p.m_ksl, p.c_ksl = 0x18, 0x80
        p.m_atdec, p.c_atdec = 0xD4, 0xC4
        p.m_susrel, p.c_susrel = 0xF2, 0x8A
        p.m_wave, p.c_wave = 0x00, 0x00
        p.feedback = 0x02

        p = rpt.user_bank[38]
        p.m_ave, p.c_ave = 0x21, 0x21
        p.m_ksl, p.c_ksl = 0x00, 0x00
        p.m_atdec, p.c_atdec = 0xF1, 0xF1
        p.m_susrel, p.c_susrel = 0x38, 0x38
        p.m_wave, p.c_wave = 0x01, 0x00
        p.feedback = 0x01

        p = rpt.user_bank[39]
        p.m_ave, p.c_ave = 0x01, 0x01
        p.m_ksl, p.c_ksl = 0x18, 0x80
        p.m_atdec, p.c_atdec = 0xD4, 0xC4
        p.m_susrel, p.c_susrel = 0xF2, 0x92
        p.m_wave, p.c_wave = 0x00, 0x00
        p.feedback = 0x00
        return

    # balanced (default): smoother chip-style voices with restrained highs.
    p = rpt.user_bank[80]
    p.m_ave, p.c_ave = 0x21, 0x21
    p.m_ksl, p.c_ksl = 0x1D, 0x00
    p.m_atdec, p.c_atdec = 0xF2, 0xF4
    p.m_susrel, p.c_susrel = 0x15, 0x15
    p.m_wave, p.c_wave = 0x01, 0x01
    p.feedback = 0x00

    p = rpt.user_bank[81]
    p.m_ave, p.c_ave = 0x21, 0x21
    p.m_ksl, p.c_ksl = 0x12, 0x00
    p.m_atdec, p.c_atdec = 0xE2, 0xF3
    p.m_susrel, p.c_susrel = 0x17, 0x18
    p.m_wave, p.c_wave = 0x01, 0x01
    p.feedback = 0x00

    p = rpt.user_bank[33]
    p.m_ave, p.c_ave = 0x01, 0x01
    p.m_ksl, p.c_ksl = 0x18, 0x80
    p.m_atdec, p.c_atdec = 0xD4, 0xC4
    p.m_susrel, p.c_susrel = 0xF2, 0x8A
    p.m_wave, p.c_wave = 0x00, 0x00
    p.feedback = 0x02

    p = rpt.user_bank[38]
    p.m_ave, p.c_ave = 0x21, 0x21
    p.m_ksl, p.c_ksl = 0x00, 0x00
    p.m_atdec, p.c_atdec = 0xF1, 0xF1
    p.m_susrel, p.c_susrel = 0x38, 0x38
    p.m_wave, p.c_wave = 0x01, 0x00
    p.feedback = 0x01

    p = rpt.user_bank[39]
    p.m_ave, p.c_ave = 0x01, 0x01
    p.m_ksl, p.c_ksl = 0x18, 0x80
    p.m_atdec, p.c_atdec = 0xD4, 0xC4
    p.m_susrel, p.c_susrel = 0xF2, 0x9A
    p.m_wave, p.c_wave = 0x00, 0x00
    p.feedback = 0x00

def convert_rpt_to_bin(rpt, output_bin, target_loops=2, profile='balanced'):
    events = bytearray()

    apply_patch_profile(rpt, profile)

    # Step 1: Initial silence & zero events matching PACMAN01.BIN / PACMAN02.BIN structure
    for ch in range(9):
        events.extend(create_event(0xB0 + ch, 0x00, 0))
    for reg in range(1, 12):
        events.extend(create_event(reg, 0x00, 0))

    # Pad header events up to event index 253 to match PACMAN01.BIN
    while len(events) < 254 * 4:
        events.extend(create_event(0x01, 0x00, 0))

    # Event 254: OPL2 Waveform Select Enable
    events.extend(create_event(0x01, 0x20, 1))

    # Initialize channels from instrument IDs emitted by import_nsf.py.
    default_inst_for_channel = [80, 81, 33, 115, 118, 38, 39]
    channel_inst = [None] * 9
    channel_car_ksl_hi = [0] * 9
    channel_prev_active = [False] * 9
    channel_prev_note = [0] * 9
    channel_prev_vol = [0] * 9
    channel_last_b0_no_key = [0] * 9
    channel_last_tl = [None] * 9

    for ch, inst_id in enumerate(default_inst_for_channel):
        patch = rpt.user_bank[inst_id]
        write_patch(events, ch, patch)
        channel_inst[ch] = inst_id
        channel_car_ksl_hi[ch] = patch.c_ksl & 0xC0

    def set_channel_patch(ch_idx, inst_id):
        if ch_idx >= len(channel_inst):
            return
        if inst_id <= 0 or inst_id >= len(rpt.user_bank):
            return
        if channel_inst[ch_idx] == inst_id:
            return
        patch = rpt.user_bank[inst_id]
        write_patch(events, ch_idx, patch)
        channel_inst[ch_idx] = inst_id
        channel_car_ksl_hi[ch_idx] = patch.c_ksl & 0xC0

    ticks_per_row = 6 # 60Hz frames per row at 150 BPM

    for loop in range(target_loops):
        for seq_idx in range(rpt.song_length):
            pat_id = rpt.sequence[seq_idx]
            abs_row_base = pat_id * 32

            for r in range(32):
                abs_row = abs_row_base + r

                # Process all channel note changes for this row with delay=0.
                for ch_idx in range(min(7, 9)):
                    cell = rpt.patterns[abs_row][ch_idx]
                    note = cell.note
                    inst = cell.inst
                    vol = cell.vol

                    if inst:
                        set_channel_patch(ch_idx, inst)

                    key_active = (note > 0 and note != 255 and vol > 0)
                    prev_active = channel_prev_active[ch_idx]
                    prev_note = channel_prev_note[ch_idx]
                    prev_vol = channel_prev_vol[ch_idx]

                    if key_active:
                        c = CAR_OFFSETS[ch_idx]

                        # Volume translation from RPT 0..63 to OPL TL 63..0; keep patch KSL bits.
                        vol_63 = max(0, min(63, vol))
                        attenuation = 63 - vol_63

                        # Apply profile mix trims (channel + instrument + note-range compensation).
                        attenuation = clamp_u6(attenuation + get_mix_trim(profile, ch_idx, inst, note))

                        # Keep drums hot so short hits stay audible.
                        if ch_idx in (3, 4):
                            attenuation = min(attenuation, 2)

                        attenuation = slew_tl(channel_last_tl[ch_idx], attenuation, max_step=6)
                        events.extend(create_event(0x40 + c, channel_car_ksl_hi[ch_idx] | attenuation, 0))
                        channel_last_tl[ch_idx] = attenuation

                        h_b, l_b = midi_to_opl_freq(note)
                        # Avoid clicky artifacts from volume-driven retriggers on melodic voices.
                        # Drums still retrigger every active step for punch.
                        if ch_idx in (3, 4):
                            retrigger = key_active
                        else:
                            retrigger = (not prev_active) or (note != prev_note)
                        if retrigger:
                            # OPL needs key-off->key-on to reliably retrigger identical notes (critical for drums).
                            if prev_active:
                                events.extend(create_event(0xB0 + ch_idx, channel_last_b0_no_key[ch_idx], 0))
                            events.extend(create_event(0xA0 + ch_idx, l_b, 0))
                            events.extend(create_event(0xB0 + ch_idx, h_b, 0))
                            channel_last_b0_no_key[ch_idx] = h_b & 0x1F
                    else:
                        if prev_active or note == 255:
                            events.extend(create_event(0xB0 + ch_idx, channel_last_b0_no_key[ch_idx], 0))

                    channel_prev_active[ch_idx] = key_active
                    channel_prev_note[ch_idx] = note
                    channel_prev_vol[ch_idx] = vol

                # Synchronize all channels by emitting row delay ONCE at the end of the row
                events.extend(create_event(0x01, 0x20, ticks_per_row))

    # Append end marker
    events.extend(create_event(0xFF, 0xFF, 0))

    # Pad to 512-byte boundary with end markers
    remainder = len(events) % 512
    if remainder != 0:
        padding_bytes = 512 - remainder
        for _ in range(padding_bytes // 4):
            events.extend(create_event(0xFF, 0xFF, 0))

    os.makedirs(os.path.dirname(os.path.abspath(output_bin)), exist_ok=True)
    with open(output_bin, 'wb') as f:
        f.write(events)

    print(f"✓ Generated {output_bin}: {len(events)} bytes ({len(events)//512} x 512-byte blocks), profile={profile}")

def parse_args(argv):
    parser = argparse.ArgumentParser(description='Export Pac-Man CE NSF tracks to RP6502 OPL2 BIN files.')
    parser.add_argument('--profile', choices=['clean', 'balanced', 'bright'], default='balanced',
                        help='Patch profile for melodic channels.')
    parser.add_argument('--track', type=int, default=0,
                        help='Track number to export (1-based). 0 means export all tracks.')
    parser.add_argument('--loops', type=int, default=0,
                        help='Override loop count (0 uses script defaults).')
    parser.add_argument('--max-seconds', type=int, default=0,
                        help='Override conversion capture length in seconds (0 uses script defaults).')
    parser.add_argument('--suffix', default='',
                        help='Optional filename suffix before .BIN, e.g. _clean.')
    return parser.parse_args(argv)

def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    nsf_path = 'NSF/Pac-Man_CE.nsf'
    output_dir = 'music'
    converter = NSFConverter(nsf_path)

    print(f"Extracting tracks from {nsf_path} with profile={args.profile}...")

    if args.track > 0:
        if args.track > converter.total_songs:
            raise ValueError(f"Track {args.track} out of range (1..{converter.total_songs})")
        song_indices = [args.track - 1]
    else:
        song_indices = list(range(converter.total_songs))

    for song_idx in song_indices:
        track_num = song_idx + 1
        suffix = args.suffix if args.suffix else ''
        bin_filename = os.path.join(output_dir, f"PacManCE_{track_num:02d}{suffix}.BIN")
        print(f"\nProcessing Track {track_num:02d} ({bin_filename})...")

        loops = args.loops if args.loops > 0 else (3 if track_num in (1, 3) else 2)
        max_seconds = args.max_seconds if args.max_seconds > 0 else (300 if track_num in (1, 3) else 60)
        rpt = converter.convert_track(song_idx, max_seconds=max_seconds)
        convert_rpt_to_bin(rpt, bin_filename, target_loops=loops, profile=args.profile)

if __name__ == '__main__':
    main()
