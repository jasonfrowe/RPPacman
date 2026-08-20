#!/usr/bin/env python3
"""
Translation layer from NSF-derived note/instrument rows to RP6502 OPL2 event streams.

This intentionally models the final RP6502 .BIN encoding rather than trying to
"tune" a patch bank by guessing at OPL2 values. The converter operates in terms of
actual NSF row semantics (note, instrument, volume) and emits the register writes
that the OPL2 player understands.
"""

from __future__ import annotations

from typing import Optional

try:
    from tools.import_nsf import OPLPatch
except ModuleNotFoundError:
    from import_nsf import OPLPatch


FNUM_TABLE = [308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579]
MOD_OFFSETS = [0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12]
CAR_OFFSETS = [0x03, 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x13, 0x14, 0x15]


def create_event(reg: int, val: int, delay: int = 0) -> bytes:
    return bytes([reg & 0xFF, val & 0xFF, delay & 0xFF, (delay >> 8) & 0xFF])


def clamp_u6(value: int) -> int:
    return max(0, min(63, value))


def midi_to_opl_freq(midi_note: int):
    if midi_note < 12:
        midi_note = 12
    if midi_note > 107:
        midi_note = 107
    block = (midi_note - 12) // 12
    note_idx = (midi_note - 12) % 12
    if block > 7:
        block = 7
    f_num = FNUM_TABLE[note_idx]
    high_byte = 0x20 | (block << 2) | ((f_num >> 8) & 0x03)
    low_byte = f_num & 0xFF
    return high_byte, low_byte


def slewtl(prev: Optional[int], target: int, max_step: int = 6) -> int:
    if prev is None:
        return target
    if target > prev + max_step:
        return prev + max_step
    if target < prev - max_step:
        return prev - max_step
    return target


def get_mix_trim(profile: str, ch_idx: int, inst_id: int, note: int) -> int:
    """Channel-level trimming for balancing translated voices in the final output."""
    if profile == 'clean':
        channel_trim = [2, 5, 1, 0, 0, 10, 2, 0, 0]
        trim = channel_trim[ch_idx] if ch_idx < len(channel_trim) else 0
        if inst_id == 38:
            trim += 6
        elif inst_id == 39:
            trim += 2
        elif inst_id == 33:
            trim += 1
        if 0 < note < 48:
            trim += 4
        elif 0 < note < 60:
            trim += 2
        return trim

    if profile == 'bright':
        channel_trim = [0, 2, 0, 0, 0, 6, 1, 0, 0]
        trim = channel_trim[ch_idx] if ch_idx < len(channel_trim) else 0
        if inst_id == 38:
            trim -= 2
        elif inst_id == 39:
            trim -= 1
        elif inst_id == 33:
            trim -= 1
        if 0 < note < 48:
            trim -= 1
        elif 0 < note < 60:
            trim -= 1
        return trim

    return 0


def apply_patch_profile(rpt, profile: str) -> None:
    """Apply a deterministic Pac-Man CE voice map to the imported NSF instrument bank."""
    def set_patch(inst_id: int, m_ave, m_ksl, m_atdec, m_susrel, m_wave,
                  c_ave, c_ksl, c_atdec, c_susrel, c_wave, feedback):
        p = rpt.user_bank[inst_id]
        p.m_ave, p.c_ave = m_ave, c_ave
        p.m_ksl, p.c_ksl = m_ksl, c_ksl
        p.m_atdec, p.c_atdec = m_atdec, c_atdec
        p.m_susrel, p.c_susrel = m_susrel, c_susrel
        p.m_wave, p.c_wave = m_wave, c_wave
        p.feedback = feedback

    if profile == 'clean':
        set_patch(80, 0x21, 0x1E, 0xE4, 0x17, 0x01, 0x21, 0x06, 0xF4, 0x18, 0x01, 0x02)
        set_patch(81, 0x21, 0x20, 0xD4, 0x18, 0x01, 0x21, 0x08, 0xF3, 0x19, 0x01, 0x02)
        set_patch(33, 0x23, 0x10, 0xA8, 0x46, 0x02, 0x21, 0x80, 0xC4, 0x58, 0x01, 0x06)
        set_patch(38, 0x23, 0x0C, 0xE2, 0x28, 0x01, 0x21, 0x06, 0xF2, 0x29, 0x01, 0x03)
        set_patch(39, 0x22, 0x18, 0xA8, 0x46, 0x02, 0x21, 0x8C, 0xD3, 0x58, 0x01, 0x05)
        return

    if profile == 'bright':
        # Pac-Man CE voice map: brighter lead, tighter bass, crisp percussion.
        set_patch(80, 0x21, 0x14, 0xF0, 0x12, 0x01, 0x21, 0x00, 0xF1, 0x15, 0x01, 0x02)
        set_patch(81, 0x21, 0x12, 0xF0, 0x14, 0x01, 0x21, 0x00, 0xF2, 0x17, 0x01, 0x02)
        set_patch(33, 0x01, 0x18, 0xD0, 0xF0, 0x00, 0x21, 0x80, 0xC0, 0x84, 0x00, 0x02)
        set_patch(38, 0x21, 0x00, 0xE8, 0x30, 0x01, 0x21, 0x00, 0xE8, 0x30, 0x00, 0x01)
        set_patch(39, 0x01, 0x18, 0xD0, 0xF0, 0x00, 0x21, 0x80, 0xC0, 0x90, 0x00, 0x00)
        set_patch(115, 0x32, 0x44, 0xF8, 0xFF, 0x00, 0x11, 0x00, 0xF5, 0x7F, 0x00, 0x04)
        set_patch(118, 0x00, 0x0D, 0xE8, 0xEF, 0x00, 0x00, 0x00, 0xA5, 0xFF, 0x00, 0x02)
        return

    # default balanced profile
    set_patch(80, 0x21, 0x1D, 0xF2, 0x15, 0x01, 0x21, 0x00, 0xF4, 0x15, 0x01, 0x00)
    set_patch(81, 0x21, 0x12, 0xE2, 0x17, 0x01, 0x21, 0x00, 0xF3, 0x18, 0x01, 0x00)
    set_patch(33, 0x01, 0x18, 0xD4, 0xF2, 0x00, 0x01, 0x80, 0xC4, 0x8A, 0x00, 0x02)
    set_patch(38, 0x21, 0x00, 0xF1, 0x38, 0x01, 0x21, 0x00, 0xF1, 0x38, 0x00, 0x01)
    set_patch(39, 0x01, 0x18, 0xD4, 0xF2, 0x00, 0x01, 0x80, 0xC4, 0x8A, 0x00, 0x00)


class OPL2Translator:
    def __init__(self, rpt, profile: str = 'bright'):
        self.rpt = rpt
        self.profile = profile
        apply_patch_profile(rpt, profile)

    def set_channel_patch(self, ch_idx: int, inst_id: int, channel_inst: list[int], channel_car_ksl_hi: list[int], events: bytearray) -> None:
        if ch_idx >= 9:
            return
        if inst_id <= 0 or inst_id >= len(self.rpt.user_bank):
            return
        if channel_inst[ch_idx] == inst_id:
            return
        patch = self.rpt.user_bank[inst_id]
        m = MOD_OFFSETS[ch_idx]
        c = CAR_OFFSETS[ch_idx]
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
        events.extend(create_event(0xC0 + ch_idx, patch.feedback, 0))
        channel_inst[ch_idx] = inst_id
        channel_car_ksl_hi[ch_idx] = patch.c_ksl & 0xC0

    def translate(self, target_loops: int = 2) -> bytearray:
        events = bytearray()

        for ch in range(9):
            events.extend(create_event(0xB0 + ch, 0x00, 0))
        for reg in range(1, 12):
            events.extend(create_event(reg, 0x00, 0))

        while len(events) < 254 * 4:
            events.extend(create_event(0x01, 0x00, 0))

        events.extend(create_event(0x01, 0x20, 1))

        default_inst_for_channel = [80, 81, 33, 115, 118, 38, 39]
        channel_inst = [None] * 9
        channel_car_ksl_hi = [0] * 9
        channel_prev_active = [False] * 9
        channel_prev_note = [0] * 9
        channel_prev_vol = [0] * 9
        channel_last_b0_no_key = [0] * 9
        channel_last_tl = [None] * 9

        for ch, inst_id in enumerate(default_inst_for_channel):
            patch = self.rpt.user_bank[inst_id]
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
            channel_inst[ch] = inst_id
            channel_car_ksl_hi[ch] = patch.c_ksl & 0xC0

        ticks_per_row = 6

        for loop in range(target_loops):
            channel_prev_active = [False] * 9
            channel_prev_note = [0] * 9
            channel_prev_vol = [0] * 9
            channel_last_b0_no_key = [0] * 9
            channel_last_tl = [None] * 9

            for seq_idx in range(self.rpt.song_length):
                pat_id = self.rpt.sequence[seq_idx]
                abs_row_base = pat_id * 32

                for r in range(32):
                    abs_row = abs_row_base + r
                    for ch_idx in range(min(7, 9)):
                        cell = self.rpt.patterns[abs_row][ch_idx]
                        note = cell.note
                        inst = cell.inst
                        vol = cell.vol

                        if inst:
                            self.set_channel_patch(ch_idx, inst, channel_inst, channel_car_ksl_hi, events)

                        key_active = (note > 0 and note != 255 and vol > 0)
                        prev_active = channel_prev_active[ch_idx]
                        prev_note = channel_prev_note[ch_idx]
                        prev_vol = channel_prev_vol[ch_idx]

                        if key_active:
                            c = CAR_OFFSETS[ch_idx]
                            vol_63 = max(0, min(63, vol))
                            attenuation = 63 - vol_63
                            attenuation = clamp_u6(attenuation + get_mix_trim(self.profile, ch_idx, inst, note))
                            if ch_idx in (3, 4):
                                attenuation = min(attenuation, 2)

                            target_tl = channel_car_ksl_hi[ch_idx] | attenuation
                            last_tl = channel_last_tl[ch_idx]
                            if last_tl is None or last_tl != target_tl:
                                adjusted = slewtl(last_tl, attenuation, max_step=6)
                                events.extend(create_event(0x40 + c, channel_car_ksl_hi[ch_idx] | adjusted, 0))
                                channel_last_tl[ch_idx] = adjusted

                            h_b, l_b = midi_to_opl_freq(note)
                            if ch_idx in (3, 4):
                                retrigger = key_active and (not prev_active or prev_note != note or prev_vol == 0)
                            else:
                                retrigger = (not prev_active) or (note != prev_note)
                            if retrigger:
                                if prev_active:
                                    events.extend(create_event(0xB0 + ch_idx, channel_last_b0_no_key[ch_idx], 0))
                                events.extend(create_event(0xA0 + ch_idx, l_b, 0))
                                events.extend(create_event(0xB0 + ch_idx, h_b, 0))
                                channel_last_b0_no_key[ch_idx] = h_b & 0x1F
                        else:
                            if prev_active or note == 255:
                                events.extend(create_event(0xB0 + ch_idx, channel_last_b0_no_key[ch_idx], 0))
                                channel_last_b0_no_key[ch_idx] = channel_last_b0_no_key[ch_idx] & 0x1F

                        channel_prev_active[ch_idx] = key_active
                        channel_prev_note[ch_idx] = note
                        channel_prev_vol[ch_idx] = vol

                    events.extend(create_event(0x01, 0x20, ticks_per_row))

        events.extend(create_event(0xFF, 0xFF, 0))
        remainder = len(events) % 512
        if remainder != 0:
            pad = 512 - remainder
            for _ in range(pad // 4):
                events.extend(create_event(0xFF, 0xFF, 0))

        return events


def translate_rpt_to_bin(rpt, output_bin: str, target_loops: int = 2, profile: str = 'bright') -> None:
    translator = OPL2Translator(rpt, profile=profile)
    events = translator.translate(target_loops=target_loops)
    import os
    os.makedirs(os.path.dirname(os.path.abspath(output_bin)), exist_ok=True)
    with open(output_bin, 'wb') as f:
        f.write(events)
    print(f"✓ Generated {output_bin}: {len(events)} bytes ({len(events)//512} x 512-byte blocks), profile={profile}")
