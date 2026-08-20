#!/usr/bin/env python3
"""
Generator for PACMAN03.BIN for RP6502.
Converts NSF/Pac-Man_CE.nsf Track 2 (5+ minutes / 310 seconds) into OPL2 binary stream (.BIN).
Captures Namco 163 (N163) expansion audio as well as 2A03 APU channels.
"""

import os
import sys
import math
import struct

# Import NSFConverter engine from RPTracker tools
sys.path.insert(0, '/Users/jasonrowe/Software/rp6502/RPTracker/tools')
from import_nsf import NSFConverter

# F-Number table for Octave 4 @ 3.579545 MHz OPL2 clock (matches src/opl.c)
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

def midi_to_opl_noteoff(midi_note):
    if midi_note < 12: midi_note = 12
    if midi_note > 107: midi_note = 107
    block = (midi_note - 12) // 12
    note_idx = (midi_note - 12) % 12
    if block > 7: block = 7
    f_num = FNUM_TABLE[note_idx]
    high_byte = (block << 2) | ((f_num >> 8) & 0x03)
    low_byte = f_num & 0xFF
    return high_byte, low_byte

def create_event(reg, val, delay=0):
    return bytes([reg & 0xFF, val & 0xFF, delay & 0xFF, (delay >> 8) & 0xFF])

def generate_pacman03_bin(nsf_path, output_bin, track_idx=2, duration_sec=310):
    print(f"Converting {nsf_path} Track {track_idx} ({duration_sec}s / {duration_sec/60:.2f} min)...", flush=True)
    
    converter = NSFConverter(nsf_path)
    rpt = converter.convert_track(track_idx, max_seconds=duration_sec)
    print(f"✓ Extracted {rpt.song_length} order sequences across RPT patterns at {rpt.bpm} BPM.", flush=True)

    events = bytearray()

    # Initial silence & zero events matching PACMAN01.BIN / PACMAN02.BIN structure
    for ch in range(9):
        events.extend(create_event(0xB0 + ch, 0x00, 0))
    for reg in range(1, 12):
        events.extend(create_event(reg, 0x00, 0))

    # Pad header events up to event index 253 to match PACMAN01.BIN
    while len(events) < 254 * 4:
        events.extend(create_event(0x01, 0x00, 0))

    # Event 254: OPL2 Waveform Select Enable
    events.extend(create_event(0x01, 0x20, 1))

    # Configure OPL2 Patches for channels 0..5
    patches = [
        # ch 0: Pulse 1 Square Lead
        (0x21, 0x21, 0x1D, 0x00, 0xF2, 0xF4, 0x15, 0x15, 0x01, 0x01, 0x00),
        # ch 1: Pulse 2 Square Harmony
        (0x21, 0x21, 0x1D, 0x00, 0xF2, 0xF4, 0x15, 0x15, 0x01, 0x01, 0x00),
        # ch 2: Triangle Bass
        (0x01, 0x01, 0x18, 0x80, 0xD4, 0xC4, 0xF2, 0x8A, 0x00, 0x00, 0x02),
        # ch 3: Noise Rhythm / Percussion
        (0x32, 0x11, 0x44, 0x00, 0xF8, 0xF5, 0xFF, 0x7F, 0x00, 0x00, 0x04),
        # ch 4: N163 Expansion Synth Lead (Channel 0)
        (0x21, 0x21, 0x00, 0x00, 0xF1, 0xF1, 0x38, 0x38, 0x01, 0x00, 0x01),
        # ch 5: N163 Expansion Synth Bass (Channel 1)
        (0x01, 0x01, 0x18, 0x80, 0xD4, 0xC4, 0xF2, 0x8A, 0x00, 0x00, 0x00),
    ]

    for ch, patch in enumerate(patches):
        m = MOD_OFFSETS[ch]
        c = CAR_OFFSETS[ch]
        m_ave, c_ave, m_ksl, c_ksl, m_atdec, c_atdec, m_susrel, c_susrel, m_wave, c_wave, fb = patch
        events.extend(create_event(0x20 + m, m_ave, 0))
        events.extend(create_event(0x20 + c, c_ave, 0))
        events.extend(create_event(0x40 + m, m_ksl, 0))
        events.extend(create_event(0x40 + c, c_ksl, 0))
        events.extend(create_event(0x60 + m, m_atdec, 0))
        events.extend(create_event(0x60 + c, c_atdec, 0))
        events.extend(create_event(0x80 + m, m_susrel, 0))
        events.extend(create_event(0x80 + c, c_susrel, 0))
        events.extend(create_event(0xE0 + m, m_wave, 0))
        events.extend(create_event(0xE0 + c, c_wave, 0))
        events.extend(create_event(0xC0 + ch, fb, 0))

    prev_state = {}
    ticks_per_row = 6 # 60Hz frames per row at 150 BPM

    # Loop the 32-pattern song order 3 times to get full 310s (5+ minutes) of audio data
    for loop_count in range(3):
        for seq_idx in range(rpt.song_length):
            pat_id = rpt.sequence[seq_idx]
            abs_row_base = pat_id * 32

            for r in range(32):
                abs_row = abs_row_base + r
                first_event_in_row = True

                for ch_idx in range(min(6, 9)):
                    cell = rpt.patterns[abs_row][ch_idx]
                    note = cell.note
                    vol = cell.vol

                    key_active = (note > 0 and note != 255 and vol > 0)
                    state_key = (ch_idx, key_active, note, vol)

                    if prev_state.get(ch_idx) != state_key:
                        prev_state[ch_idx] = state_key
                        c = CAR_OFFSETS[ch_idx]
                        d = ticks_per_row if first_event_in_row else 0
                        if first_event_in_row: first_event_in_row = False

                        # Volume attenuation: 0..63
                        attenuation = 63 - (vol * 4 if vol <= 15 else vol)
                        if attenuation < 0: attenuation = 0
                        events.extend(create_event(0x40 + c, attenuation, d))

                        if key_active:
                            h_b, l_b = midi_to_opl_freq(note)
                            events.extend(create_event(0xA0 + ch_idx, l_b, 0))
                            events.extend(create_event(0xB0 + ch_idx, h_b, 0))
                        elif note == 255:
                            events.extend(create_event(0xB0 + ch_idx, 0x00, 0))

                if first_event_in_row:
                    events.extend(create_event(0x01, 0x20, ticks_per_row))

    # Append end marker
    events.extend(create_event(0xFF, 0xFF, 0))

    # Pad to 512-byte boundary with end markers
    remainder = len(events) % 512
    if remainder != 0:
        padding_bytes = 512 - remainder
        for _ in range(padding_bytes // 4):
            events.extend(create_event(0xFF, 0xFF, 0))

    os.makedirs(os.path.dirname(output_bin), exist_ok=True)
    with open(output_bin, 'wb') as f:
        f.write(events)

    print(f"✓ Successfully generated {output_bin}: {len(events)} bytes ({len(events)//512} x 512-byte blocks)")

if __name__ == '__main__':
    generate_pacman03_bin('NSF/Pac-Man_CE.nsf', 'music/PACMAN03.BIN', track_idx=2, duration_sec=310)
