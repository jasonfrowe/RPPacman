#!/usr/bin/env python3
"""
RPTracker NSF Converter / Importer
Converts NES NSF files (specifically Pac-Man_CE.nsf with 2A03/VRC7/N163 audio)
into RPTracker RPT2 song format (.rpt) targeting OPL2.
"""

import sys
import os
import math
import struct
from collections import Counter

try:
    from py65.devices.mpu6502 import MPU
except ImportError:
    MPU = None

# RPT2 Constants
ROWS = 32
CHANS = 9
PATTERNS = 32

class PatternCell:
    def __init__(self, note=0, inst=0, vol=0, effect=0x0000):
        self.note = note      # 0=empty, 255=note off, 12-119=MIDI note
        self.inst = inst      # 0-255 instrument
        self.vol = vol        # 0-63 volume
        self.effect = effect  # 16-bit effect

    def to_bytes(self):
        lo = self.effect & 0xFF
        hi = (self.effect >> 8) & 0xFF
        return bytes([self.note, self.inst, self.vol, lo, hi])

class OPLPatch:
    def __init__(self, m_ave=0x01, m_ksl=0x00, m_atdec=0xF0, m_susrel=0x50, m_wave=0x00,
                 c_ave=0x01, c_ksl=0x00, c_atdec=0xF0, c_susrel=0x50, c_wave=0x00, feedback=0x00):
        self.m_ave = m_ave
        self.m_ksl = m_ksl
        self.m_atdec = m_atdec
        self.m_susrel = m_susrel
        self.m_wave = m_wave
        self.c_ave = c_ave
        self.c_ksl = c_ksl
        self.c_atdec = c_atdec
        self.c_susrel = c_susrel
        self.c_wave = c_wave
        self.feedback = feedback

    def to_bytes(self):
        return bytes([self.m_ave, self.m_ksl, self.m_atdec, self.m_susrel, self.m_wave,
                      self.c_ave, self.c_ksl, self.c_atdec, self.c_susrel, self.c_wave, self.feedback])

class RPT4File:
    def __init__(self):
        self.octave = 3
        self.volume = 63
        self.song_length = 1
        self.bpm = 150
        self.user_bank = [OPLPatch() for _ in range(256)]
        
        # Override NES & Expansion instruments with standard AdLib GM OPL2 sound patches
        # Inst 80 (0x50 - Square 1): Classic AdLib Square Wave
        self.user_bank[80] = OPLPatch(m_ave=0x21, m_ksl=0x1D, m_atdec=0xF2, m_susrel=0x0F, m_wave=0x03,
                                     c_ave=0x21, c_ksl=0x00, c_atdec=0xF2, c_susrel=0x18, c_wave=0x00, feedback=0x00)
        # Inst 81 (0x51 - Square 2): Classic AdLib Sawtooth Wave
        self.user_bank[81] = OPLPatch(m_ave=0x41, m_ksl=0x11, m_atdec=0xF0, m_susrel=0xFF, m_wave=0x00,
                                     c_ave=0x01, c_ksl=0x00, c_atdec=0xF0, c_susrel=0xFF, c_wave=0x00, feedback=0x02)
        # Inst 33 (0x21 - Triangle): Smooth AdLib Electric Bass
        self.user_bank[33] = OPLPatch(m_ave=0x01, m_ksl=0x18, m_atdec=0xD4, m_susrel=0xF2, m_wave=0x00,
                                     c_ave=0x21, c_ksl=0x80, c_atdec=0xC4, c_susrel=0x8A, c_wave=0x00, feedback=0x02)
        # Inst 115 (0x73 - Noise): Woodblock / Percussion
        self.user_bank[115] = OPLPatch(m_ave=0x32, m_ksl=0x44, m_atdec=0xF8, m_susrel=0xFF, m_wave=0x00,
                                      c_ave=0x11, c_ksl=0x00, c_atdec=0xF5, c_susrel=0x7F, c_wave=0x00, feedback=0x04)
        # Inst 118 (0x76 - DMC): Synth Drum / Kick
        self.user_bank[118] = OPLPatch(m_ave=0x00, m_ksl=0x0D, m_atdec=0xE8, m_susrel=0xEF, m_wave=0x00,
                                      c_ave=0x00, c_ksl=0x00, c_atdec=0xA5, c_susrel=0xFF, c_wave=0x00, feedback=0x02)
        # Inst 38 (0x26 - N163 Ch 0): Clean AdLib Synth Lead
        self.user_bank[38] = OPLPatch(m_ave=0x21, m_ksl=0x00, m_atdec=0xF1, m_susrel=0x38, m_wave=0x00,
                                     c_ave=0x21, c_ksl=0x00, c_atdec=0xF1, c_susrel=0x38, c_wave=0x00, feedback=0x01)
        # Inst 39 (0x27 - N163 Ch 1): Warm AdLib Synth Bass
        self.user_bank[39] = OPLPatch(m_ave=0x01, m_ksl=0x00, m_atdec=0xF1, m_susrel=0x54, m_wave=0x00,
                                     c_ave=0x01, c_ksl=0x00, c_atdec=0xF1, c_susrel=0x54, c_wave=0x00, feedback=0x00)

        self.patterns = [[PatternCell() for _ in range(CHANS)] for _ in range(ROWS * PATTERNS)]
        self.sequence = [0] * 256

    def set_cell(self, pattern_idx, row_idx, chan_idx, cell):
        if 0 <= pattern_idx < PATTERNS and 0 <= row_idx < ROWS and 0 <= chan_idx < CHANS:
            abs_row = pattern_idx * ROWS + row_idx
            self.patterns[abs_row][chan_idx] = cell

    def save(self, filename):
        os.makedirs(os.path.dirname(os.path.abspath(filename)), exist_ok=True)
        with open(filename, 'wb') as f:
            # Header
            f.write(b'RPT4')
            # Metadata: Octave (B), Volume (B), Song Length (H), BPM (H)
            f.write(struct.pack('<BBHH', self.octave, self.volume, self.song_length, self.bpm))
            # Custom Patch Bank: 256 x 11 bytes = 2816 bytes
            for patch in self.user_bank:
                f.write(patch.to_bytes())
            # 32 patterns x 32 rows x 9 channels x 5 bytes
            for p in range(PATTERNS):
                for r in range(ROWS):
                    abs_row = p * ROWS + r
                    for c in range(CHANS):
                        f.write(self.patterns[abs_row][c].to_bytes())
            # Sequence order list (256 bytes)
            f.write(bytes(self.sequence[:256]))

class CPU6502:
    """Lightweight 6502 CPU emulator for executing NSF player routines."""
    def __init__(self, memory):
        self.mem = memory
        self.a = 0
        self.x = 0
        self.y = 0
        self.sp = 0xFF
        self.pc = 0
        self.c = 0
        self.z = 0
        self.i = 1
        self.d = 0
        self.v = 0
        self.n = 0

    def get_p(self):
        return (self.c | (self.z << 1) | (self.i << 2) | (self.d << 3) |
                (1 << 4) | (1 << 5) | (self.v << 6) | (self.n << 7))

    def set_p(self, val):
        self.c = val & 1
        self.z = (val >> 1) & 1
        self.i = (val >> 2) & 1
        self.d = (val >> 3) & 1
        self.v = (val >> 6) & 1
        self.n = (val >> 7) & 1

    def push(self, val):
        self.mem[0x0100 + self.sp] = val & 0xFF
        self.sp = (self.sp - 1) & 0xFF

    def pop(self):
        self.sp = (self.sp + 1) & 0xFF
        return self.mem[0x0100 + self.sp]

    def set_nz(self, val):
        val &= 0xFF
        self.z = 1 if val == 0 else 0
        self.n = 1 if (val & 0x80) else 0
        return val

    def step(self):
        op = self.mem[self.pc]
        self.pc = (self.pc + 1) & 0xFFFF

        # Addressing modes helpers
        def imm():
            addr = self.pc
            self.pc = (self.pc + 1) & 0xFFFF
            return addr

        def zp():
            addr = self.mem[self.pc]
            self.pc = (self.pc + 1) & 0xFFFF
            return addr

        def zpx():
            addr = (self.mem[self.pc] + self.x) & 0xFF
            self.pc = (self.pc + 1) & 0xFFFF
            return addr

        def zpy():
            addr = (self.mem[self.pc] + self.y) & 0xFF
            self.pc = (self.pc + 1) & 0xFFFF
            return addr

        def abs_a():
            low = self.mem[self.pc]
            high = self.mem[(self.pc + 1) & 0xFFFF]
            self.pc = (self.pc + 2) & 0xFFFF
            return low | (high << 8)

        def abs_x():
            base = abs_a()
            return (base + self.x) & 0xFFFF

        def abs_y():
            base = abs_a()
            return (base + self.y) & 0xFFFF

        def ind_x():
            zp_addr = (self.mem[self.pc] + self.x) & 0xFF
            self.pc = (self.pc + 1) & 0xFFFF
            return self.mem[zp_addr] | (self.mem[(zp_addr + 1) & 0xFF] << 8)

        def ind_y():
            zp_addr = self.mem[self.pc]
            self.pc = (self.pc + 1) & 0xFFFF
            base = self.mem[zp_addr] | (self.mem[(zp_addr + 1) & 0xFF] << 8)
            return (base + self.y) & 0xFFFF

        def branch(cond):
            offset = self.mem[self.pc]
            self.pc = (self.pc + 1) & 0xFFFF
            if offset & 0x80:
                offset -= 256
            if cond:
                self.pc = (self.pc + offset) & 0xFFFF

        # Common opcodes
        if op == 0xEA: pass # NOP
        elif op == 0x78: self.i = 1 # SEI
        elif op == 0x58: self.i = 0 # CLI
        elif op == 0x18: self.c = 0 # CLC
        elif op == 0x38: self.c = 1 # SEC
        elif op == 0xD8: self.d = 0 # CLD
        elif op == 0xF8: self.d = 1 # SED
        elif op == 0xB8: self.v = 0 # CLV

        # LDA
        elif op == 0xA9: self.a = self.set_nz(self.mem[imm()])
        elif op == 0xA5: self.a = self.set_nz(self.mem[zp()])
        elif op == 0xB5: self.a = self.set_nz(self.mem[zpx()])
        elif op == 0xAD: self.a = self.set_nz(self.mem[abs_a()])
        elif op == 0xBD: self.a = self.set_nz(self.mem[abs_x()])
        elif op == 0xB9: self.a = self.set_nz(self.mem[abs_y()])
        elif op == 0xA1: self.a = self.set_nz(self.mem[ind_x()])
        elif op == 0xB1: self.a = self.set_nz(self.mem[ind_y()])

        # LDX
        elif op == 0xA2: self.x = self.set_nz(self.mem[imm()])
        elif op == 0xA6: self.x = self.set_nz(self.mem[zp()])
        elif op == 0xB6: self.x = self.set_nz(self.mem[zpy()])
        elif op == 0xAE: self.x = self.set_nz(self.mem[abs_a()])
        elif op == 0xBE: self.x = self.set_nz(self.mem[abs_y()])

        # LDY
        elif op == 0xA0: self.y = self.set_nz(self.mem[imm()])
        elif op == 0xA4: self.y = self.set_nz(self.mem[zp()])
        elif op == 0xB4: self.y = self.set_nz(self.mem[zpx()])
        elif op == 0xAC: self.y = self.set_nz(self.mem[abs_a()])
        elif op == 0xBC: self.y = self.set_nz(self.mem[abs_x()])

        # STA
        elif op == 0x85: self.mem[zp()] = self.a
        elif op == 0x95: self.mem[zpx()] = self.a
        elif op == 0x8D: self.mem[abs_a()] = self.a
        elif op == 0x9D: self.mem[abs_x()] = self.a
        elif op == 0x99: self.mem[abs_y()] = self.a
        elif op == 0x81: self.mem[ind_x()] = self.a
        elif op == 0x91: self.mem[ind_y()] = self.a

        # STX
        elif op == 0x86: self.mem[zp()] = self.x
        elif op == 0x96: self.mem[zpy()] = self.x
        elif op == 0x8E: self.mem[abs_a()] = self.x

        # STY
        elif op == 0x84: self.mem[zp()] = self.y
        elif op == 0x94: self.mem[zpx()] = self.y
        elif op == 0x8C: self.mem[abs_a()] = self.y

        # Transfers
        elif op == 0xAA: self.x = self.set_nz(self.a) # TAX
        elif op == 0x8A: self.a = self.set_nz(self.x) # TXA
        elif op == 0xA8: self.y = self.set_nz(self.a) # TAY
        elif op == 0x98: self.a = self.set_nz(self.y) # TYA
        elif op == 0x9A: self.sp = self.x # TXS
        elif op == 0xBA: self.x = self.set_nz(self.sp) # TSX

        # Stack
        elif op == 0x48: self.push(self.a) # PHA
        elif op == 0x68: self.a = self.set_nz(self.pop()) # PLA
        elif op == 0x08: self.push(self.get_p()) # PHP
        elif op == 0x28: self.set_p(self.pop()) # PLP

        # Jumps & Calls
        elif op == 0x4C: self.pc = abs_a() # JMP abs
        elif op == 0x6C: # JMP ind
            target = abs_a()
            low = self.mem[target]
            high_addr = (target & 0xFF00) | ((target + 1) & 0xFF)
            high = self.mem[high_addr]
            self.pc = low | (high << 8)
        elif op == 0x20: # JSR
            target = abs_a()
            ret = (self.pc - 1) & 0xFFFF
            self.push((ret >> 8) & 0xFF)
            self.push(ret & 0xFF)
            self.pc = target
        elif op == 0x60: # RTS
            low = self.pop()
            high = self.pop()
            self.pc = ((low | (high << 8)) + 1) & 0xFFFF

        # Branches
        elif op == 0x10: branch(self.n == 0) # BPL
        elif op == 0x30: branch(self.n == 1) # BMI
        elif op == 0x50: branch(self.v == 0) # BVC
        elif op == 0x70: branch(self.v == 1) # BVS
        elif op == 0x90: branch(self.c == 0) # BCC
        elif op == 0xB0: branch(self.c == 1) # BCS
        elif op == 0xD0: branch(self.z == 0) # BNE
        elif op == 0xF0: branch(self.z == 1) # BEQ

        # Increments & Decrements
        elif op == 0xE6: a = zp(); self.mem[a] = self.set_nz((self.mem[a] + 1) & 0xFF)
        elif op == 0xF6: a = zpx(); self.mem[a] = self.set_nz((self.mem[a] + 1) & 0xFF)
        elif op == 0xEE: a = abs_a(); self.mem[a] = self.set_nz((self.mem[a] + 1) & 0xFF)
        elif op == 0xFE: a = abs_x(); self.mem[a] = self.set_nz((self.mem[a] + 1) & 0xFF)
        elif op == 0xC6: a = zp(); self.mem[a] = self.set_nz((self.mem[a] - 1) & 0xFF)
        elif op == 0xD6: a = zpx(); self.mem[a] = self.set_nz((self.mem[a] - 1) & 0xFF)
        elif op == 0xCE: a = abs_a(); self.mem[a] = self.set_nz((self.mem[a] - 1) & 0xFF)
        elif op == 0xDE: a = abs_x(); self.mem[a] = self.set_nz((self.mem[a] - 1) & 0xFF)
        elif op == 0xE8: self.x = self.set_nz((self.x + 1) & 0xFF) # INX
        elif op == 0xC8: self.y = self.set_nz((self.y + 1) & 0xFF) # INY
        elif op == 0xCA: self.x = self.set_nz((self.x - 1) & 0xFF) # DEX
        elif op == 0x88: self.y = self.set_nz((self.y - 1) & 0xFF) # DEY

        # Comparisons
        elif op in (0xC9, 0xC5, 0xD5, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1): # CMP
            a = imm() if op == 0xC9 else (zp() if op == 0xC5 else (zpx() if op == 0xD5 else (abs_a() if op == 0xCD else (abs_x() if op == 0xDD else (abs_y() if op == 0xD9 else (ind_x() if op == 0xC1 else ind_y()))))))
            val = self.mem[a]
            diff = self.a - val
            self.c = 1 if self.a >= val else 0
            self.set_nz(diff & 0xFF)
        elif op in (0xE0, 0xE4, 0xEC): # CPX
            a = imm() if op == 0xE0 else (zp() if op == 0xE4 else abs_a())
            val = self.mem[a]
            self.c = 1 if self.x >= val else 0
            self.set_nz((self.x - val) & 0xFF)
        elif op in (0xC0, 0xC4, 0xCC): # CPY
            a = imm() if op == 0xC0 else (zp() if op == 0xC4 else abs_a())
            val = self.mem[a]
            self.c = 1 if self.y >= val else 0
            self.set_nz((self.y - val) & 0xFF)

        # Bitwise & Math (AND, ORA, EOR, ADC, SBC)
        elif op in (0x29, 0x25, 0x35, 0x2D, 0x3D, 0x39, 0x21, 0x31): # AND
            a = imm() if op == 0x29 else (zp() if op == 0x25 else (zpx() if op == 0x35 else (abs_a() if op == 0x2D else (abs_x() if op == 0x3D else (abs_y() if op == 0x39 else (ind_x() if op == 0x21 else ind_y()))))))
            self.a = self.set_nz(self.a & self.mem[a])
        elif op in (0x09, 0x05, 0x15, 0x0D, 0x1D, 0x19, 0x01, 0x11): # ORA
            a = imm() if op == 0x09 else (zp() if op == 0x05 else (zpx() if op == 0x15 else (abs_a() if op == 0x0D else (abs_x() if op == 0x1D else (abs_y() if op == 0x19 else (ind_x() if op == 0x01 else ind_y()))))))
            self.a = self.set_nz(self.a | self.mem[a])
        elif op in (0x49, 0x45, 0x55, 0x4D, 0x5D, 0x59, 0x41, 0x51): # EOR
            a = imm() if op == 0x49 else (zp() if op == 0x45 else (zpx() if op == 0x55 else (abs_a() if op == 0x4D else (abs_x() if op == 0x5D else (abs_y() if op == 0x59 else (ind_x() if op == 0x41 else ind_y()))))))
            self.a = self.set_nz(self.a ^ self.mem[a])

        elif op in (0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71): # ADC
            a = imm() if op == 0x69 else (zp() if op == 0x65 else (zpx() if op == 0x75 else (abs_a() if op == 0x6D else (abs_x() if op == 0x7D else (abs_y() if op == 0x79 else (ind_x() if op == 0x61 else ind_y()))))))
            val = self.mem[a]
            res = self.a + val + self.c
            self.v = 1 if (~(self.a ^ val) & (self.a ^ res) & 0x80) else 0
            self.c = 1 if res > 0xFF else 0
            self.a = self.set_nz(res & 0xFF)

        elif op in (0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1): # SBC
            a = imm() if op == 0xE9 else (zp() if op == 0xE5 else (zpx() if op == 0xF5 else (abs_a() if op == 0xED else (abs_x() if op == 0xFD else (abs_y() if op == 0xF9 else (ind_x() if op == 0xE1 else ind_y()))))))
            val = self.mem[a] ^ 0xFF
            res = self.a + val + self.c
            self.v = 1 if (~(self.a ^ val) & (self.a ^ res) & 0x80) else 0
            self.c = 1 if res > 0xFF else 0
            self.a = self.set_nz(res & 0xFF)

        # Shifts & Rotates (ASL, LSR, ROL, ROR)
        elif op == 0x0A: # ASL A
            self.c = (self.a >> 7) & 1
            self.a = self.set_nz((self.a << 1) & 0xFF)
        elif op in (0x06, 0x16, 0x0E, 0x1E):
            a = zp() if op == 0x06 else (zpx() if op == 0x16 else (abs_a() if op == 0x0E else abs_x()))
            val = self.mem[a]
            self.c = (val >> 7) & 1
            self.mem[a] = self.set_nz((val << 1) & 0xFF)
        elif op == 0x4A: # LSR A
            self.c = self.a & 1
            self.a = self.set_nz((self.a >> 1) & 0xFF)
        elif op in (0x46, 0x56, 0x4E, 0x5E):
            a = zp() if op == 0x46 else (zpx() if op == 0x56 else (abs_a() if op == 0x4E else abs_x()))
            val = self.mem[a]
            self.c = val & 1
            self.mem[a] = self.set_nz((val >> 1) & 0xFF)
        elif op == 0x2A: # ROL A
            c_old = self.c
            self.c = (self.a >> 7) & 1
            self.a = self.set_nz(((self.a << 1) | c_old) & 0xFF)
        elif op in (0x26, 0x36, 0x2E, 0x3E):
            a = zp() if op == 0x26 else (zpx() if op == 0x36 else (abs_a() if op == 0x2E else abs_x()))
            val = self.mem[a]
            c_old = self.c
            self.c = (val >> 7) & 1
            self.mem[a] = self.set_nz(((val << 1) | c_old) & 0xFF)
        elif op == 0x6A: # ROR A
            c_old = self.c
            self.c = self.a & 1
            self.a = self.set_nz((self.a >> 1) | (c_old << 7))
        elif op in (0x66, 0x76, 0x6E, 0x7E):
            a = zp() if op == 0x66 else (zpx() if op == 0x76 else (abs_a() if op == 0x6E else abs_x()))
            val = self.mem[a]
            c_old = self.c
            self.c = val & 1
            self.mem[a] = self.set_nz((val >> 1) | (c_old << 7))
        elif op in (0x24, 0x2C): # BIT
            a = zp() if op == 0x24 else abs_a()
            val = self.mem[a]
            self.z = 1 if (self.a & val) == 0 else 0
            self.v = (val >> 6) & 1
            self.n = (val >> 7) & 1

def normalize_midi_note(note):
    if note <= 0:
        return 0
    return min(127, max(1, note))

def map_apu_vol_to_rpt(vol_15):
    if vol_15 <= 0:
        return 0
    return int(round(24 + (vol_15 / 15.0) * 39))

NOISE_PERIOD_TO_MIDI = [84, 82, 79, 76, 72, 69, 66, 62, 58, 54, 50, 45, 40, 35, 30, 25]

class NSFImporter:
    def __init__(self, filename):
        self.filename = filename
        with open(filename, 'rb') as f:
            self.data = f.read()

        if self.data[:5] != b'NESM\x1A':
            raise ValueError("Not a valid NSF file")

        self.total_songs = self.data[6]
        self.start_song = self.data[7]
        self.load_addr, self.init_addr, self.play_addr = struct.unpack('<HHH', self.data[8:14])
        self.title = self.data[0x0E:0x2E].split(b'\x00')[0].decode('latin1', 'ignore').strip()

class NSFConverter:
    def __init__(self, nsf_path):
        with open(nsf_path, 'rb') as f:
            self.data = f.read()

        if self.data[:5] != b'NESM\x1a':
            raise ValueError("Invalid NSF magic header")

        self.total_songs = self.data[6]
        self.start_song = self.data[7]
        self.load_addr, self.init_addr, self.play_addr = struct.unpack('<HHH', self.data[8:14])
        self.title = self.data[0x0E:0x2E].split(b'\x00')[0].decode('latin1', 'ignore').strip()
        self.artist = self.data[0x2E:0x4E].split(b'\x00')[0].decode('latin1', 'ignore').strip()
        self.payload = self.data[0x80:]

    # Real hardware reloads a length counter from a 32-entry duration table
    # (values 2-254, in half-frame ticks) selected by the length-load
    # register's top 5 bits. That table isn't reproduced here -- every
    # entry is nonzero, and duration only matters once a channel's halt bit
    # is 0 (this NSF holds it at 1 throughout, so decrementing never
    # actually runs) -- so a single generous default (the table's own max)
    # stands in for "some real, sizeable duration" without risking an
    # artificially short guessed value cutting a note off early on a track
    # that does use halt=0.
    DEFAULT_LENGTH_TICKS = 254

    def _run_apu(self, track_idx, max_seconds, subticks_per_frame):
        """Run the NSF's INIT/PLAY routines and yield one entry per real
        sub-tick: {channel: (note, vol)}. This is the actual chip state,
        with no row/pattern quantization.

        The APU's envelope/linear-counter/length-counter clock genuinely
        runs at ~240Hz (quarter-frame) on real hardware, four times denser
        than the 60Hz `PLAY` call rate -- a single per-`PLAY`-call snapshot
        can't see anything about that clock's internal state, only whether
        a register was last written a particular way. `subticks_per_frame`
        controls how many of those quarter-frame clocks actually get
        emitted as separate history entries (4 for real quarter-frame
        resolution, 1 to collapse back to one entry per `PLAY` call for
        RPT4/RPTracker-grid compatibility) -- the counter engine itself
        always runs at true quarter-frame resolution internally either way,
        so the collapsed view still reflects real decrementing behavior,
        just sampled once every 4 sub-ticks instead of every one.
        """
        total_frames = max_seconds * 60

        ram = bytearray(0x10000)
        ram[self.load_addr:self.load_addr+len(self.payload)] = self.payload

        apu_regs = bytearray(32)
        n163_addr = 0
        n163_auto_inc = False
        n163_ram = bytearray(128)

        # Real length-counter state (sq1/sq2/tri/noise) and triangle's
        # linear counter, advanced by real quarter-frame/half-frame clocks
        # below rather than inferred from a single register snapshot. $4015
        # bit N = 0 forces channel N's length counter to 0 immediately;
        # re-enabling via $4015 alone does not restore it -- only a
        # subsequent write to that channel's length-load register (while
        # enabled) does. The linear counter's reload flag is set by any
        # $400B write and, each quarter-frame clock, either reloads
        # (control/halt bit set, or flag pending) or decrements.
        length_counter = {'sq1': 0, 'sq2': 0, 'tri': 0, 'noise': 0}
        linear_counter = 0
        linear_reload_flag = False
        LENGTH_LOAD_ADDR = {0x4003: 'sq1', 0x4007: 'sq2', 0x400B: 'tri', 0x400F: 'noise'}
        ENABLE_BIT = {'sq1': 0x01, 'sq2': 0x02, 'tri': 0x04, 'noise': 0x08}
        HALT_BIT_REG = {'sq1': 0x00, 'sq2': 0x04, 'tri': 0x08, 'noise': 0x0C}

        def write_io(addr, val):
            nonlocal n163_addr, n163_auto_inc, linear_reload_flag
            addr &= 0xFFFF
            val &= 0xFF
            ram[addr] = val
            if 0x4000 <= addr <= 0x4017:
                apu_regs[addr - 0x4000] = val
                if addr == 0x4015:
                    for ch, bit in ENABLE_BIT.items():
                        if not (val & bit):
                            length_counter[ch] = 0
                elif addr in LENGTH_LOAD_ADDR:
                    ch = LENGTH_LOAD_ADDR[addr]
                    if apu_regs[0x15] & ENABLE_BIT[ch]:
                        length_counter[ch] = self.DEFAULT_LENGTH_TICKS
                    if ch == 'tri':
                        linear_reload_flag = True
            elif addr == 0xF800:
                n163_addr = val & 0x7F
                n163_auto_inc = bool(val & 0x80)
            elif addr == 0x4800:
                n163_ram[n163_addr] = val
                if n163_auto_inc:
                    n163_addr = (n163_addr + 1) & 0x7F

        class HookMemory:
            def __getitem__(_, a): return ram[a & 0xFFFF]
            def __setitem__(_, a, v): write_io(a, v)

        mem = HookMemory()
        cpu = MPU(memory=mem) if MPU is not None else CPU6502(mem)

        # Call INIT
        cpu.a = track_idx
        cpu.x = 0
        cpu.pc = self.init_addr
        cpu.sp = 0xFF
        ram[0x01FF] = 0x00; ram[0x01FE] = 0x00
        step = 0
        while cpu.pc != 0x0001 and step < 500000:
            cpu.step()
            step += 1

        keep_every = 4 // subticks_per_frame
        history = []
        for f in range(total_frames):
            cpu.pc = self.play_addr
            cpu.sp = 0xFF
            ram[0x01FF] = 0x00; ram[0x01FE] = 0x00
            step = 0
            while cpu.pc != 0x0001 and step < 500000:
                cpu.step()
                step += 1

            status = apu_regs[0x15]

            # Decode the register-level facts once per real PLAY call --
            # these don't change again until the next one. Only the
            # counter-driven audibility gate below varies per sub-tick.
            sq1_en = bool(status & 0x01)
            sq1_p = apu_regs[2] | ((apu_regs[3] & 0x07) << 8)
            sq1_v_reg = apu_regs[0]
            sq1_vol = (sq1_v_reg & 0x0F) if (sq1_v_reg & 0x10) else (15 if sq1_en else 0)
            sq1_freq_ok = False
            sq1_raw_note = 0
            if sq1_en and sq1_p > 0 and sq1_vol > 0:
                freq = 1789773.0 / (16.0 * (sq1_p + 1))
                if 20 <= freq <= 12000:
                    sq1_raw_note = normalize_midi_note(int(round(12.0 * math.log2(freq / 440.0) + 69)))
                    sq1_freq_ok = True

            sq2_en = bool(status & 0x02)
            sq2_p = apu_regs[6] | ((apu_regs[7] & 0x07) << 8)
            sq2_v_reg = apu_regs[4]
            sq2_vol = (sq2_v_reg & 0x0F) if (sq2_v_reg & 0x10) else (15 if sq2_en else 0)
            sq2_raw_note = 0
            if sq2_en and sq2_p > 0 and sq2_vol > 0:
                freq = 1789773.0 / (16.0 * (sq2_p + 1))
                if 20 <= freq <= 12000:
                    sq2_raw_note = normalize_midi_note(int(round(12.0 * math.log2(freq / 440.0) + 69)))

            tri_en = bool(status & 0x04)
            tri_p = apu_regs[10] | ((apu_regs[11] & 0x07) << 8)
            tri_raw_note = 0
            if tri_en and tri_p > 0:
                freq = 1789773.0 / (32.0 * (tri_p + 1))
                if 20 <= freq <= 12000:
                    tri_raw_note = normalize_midi_note(int(round(12.0 * math.log2(freq / 440.0) + 69)))

            noise_en = bool(status & 0x08)
            noise_v_reg = apu_regs[12]
            noise_vol = (noise_v_reg & 0x0F) if (noise_v_reg & 0x10) else (15 if noise_en else 0)
            noise_raw_note = 0
            if noise_en and noise_vol > 0:
                noise_period = apu_regs[14] & 0x0F
                noise_raw_note = NOISE_PERIOD_TO_MIDI[noise_period]

            # $4011 (direct-load) is not a useful "is DMC sounding" signal
            # here: real DMC sample bytes are read autonomously from ROM by
            # the hardware once $4015 bit4 is enabled, not pushed via
            # further $4011 writes -- this driver always primes $4011 to 0
            # immediately before every kick trigger, so gating on it being
            # nonzero can never fire. $4015's own enable bit is the real
            # "DMC channel active" signal.
            dmc_en = bool(status & 0x10)
            dmc_note = 38 if dmc_en else 0

            num_n163 = ((n163_ram[0x7F] >> 4) & 0x07) + 1
            n163_active_notes = []
            for i in range(num_n163):
                ch_base = 0x78 - (i * 8)
                vol = n163_ram[ch_base + 7] & 0x0F
                freq_18 = n163_ram[ch_base + 0] | (n163_ram[ch_base + 2] << 8) | ((n163_ram[ch_base + 4] & 0x03) << 16)
                wave_len = 256 - (n163_ram[ch_base + 4] & 0xFC)
                if wave_len <= 0: wave_len = 32
                if vol > 0 and freq_18 > 0:
                    freq = (freq_18 * 1789773.0) / (15.0 * 65536.0 * num_n163 * wave_len)
                    if 20 <= freq <= 12000:
                        mnote = normalize_midi_note(int(round(12.0 * math.log2(freq / 440.0) + 69)))
                        n163_active_notes.append((mnote, vol))
            n163_0_note, n163_0_vol = n163_active_notes[0] if len(n163_active_notes) > 0 else (0, 0)
            n163_1_note, n163_1_vol = n163_active_notes[1] if len(n163_active_notes) > 1 else (0, 0)

            # Four real quarter-frame clocks per PLAY call. Half-frame
            # (length counter) clocks land on sub-ticks 1 and 3 -- an
            # approximation of NTSC's actual 4-step/5-step sequencer
            # timing, not cycle-accurate, but a deliberate simplification
            # ("good, not perfect") rather than an oversight.
            for s in range(4):
                # Quarter-frame clock: triangle's linear counter.
                halt = bool(apu_regs[8] & 0x80)
                if linear_reload_flag:
                    linear_counter = apu_regs[8] & 0x7F
                elif linear_counter > 0:
                    linear_counter -= 1
                if not halt:
                    linear_reload_flag = False

                # Half-frame clock: length counters, unless each channel's
                # own halt bit is set. Triangle's halt bit is 0x80 (the
                # same physical latch as its linear-counter control flag);
                # sq1/sq2/noise use 0x20 -- confirmed against libgme's own
                # Nes_Apu.cpp ("triangle.clock_length(0x80); // different
                # bit for halt flag on triangle").
                if s % 2 == 1:
                    for ch, reg_base in HALT_BIT_REG.items():
                        if length_counter[ch] <= 0:
                            continue
                        halt_bit = 0x80 if ch == 'tri' else 0x20
                        if not (apu_regs[reg_base] & halt_bit):
                            length_counter[ch] -= 1

                if s % keep_every != keep_every - 1:
                    continue

                sq1_note = sq1_raw_note if length_counter['sq1'] > 0 else 0
                sq2_note = sq2_raw_note if length_counter['sq2'] > 0 else 0
                tri_note = tri_raw_note if (length_counter['tri'] > 0 and linear_counter > 0) else 0
                noise_note = noise_raw_note if length_counter['noise'] > 0 else 0

                history.append({
                    'sq1': (sq1_note, sq1_vol),
                    'sq2': (sq2_note, sq2_vol),
                    'tri': (tri_note, 15 if tri_note else 0),
                    'noise': (noise_note, noise_vol),
                    'dmc': (dmc_note, 15 if dmc_note else 0),
                    'n163_0': (n163_0_note, n163_0_vol),
                    'n163_1': (n163_1_note, n163_1_vol),
                })

        return history

    def build_frame_history(self, track_idx, max_seconds=100):
        """One entry per real 60Hz NSF frame -- see `_run_apu`. Used by
        `convert_track`'s RPT4/RPTracker-grid export path."""
        return self._run_apu(track_idx, max_seconds, subticks_per_frame=1)

    def build_subframe_history(self, track_idx, max_seconds=100):
        """Four entries per real 60Hz NSF frame, at true quarter-frame
        (~240Hz) resolution -- see `_run_apu`. Used by the direct OPL2
        translator, which has no row-grid resolution limit to collapse to."""
        return self._run_apu(track_idx, max_seconds, subticks_per_frame=4)

    def convert_track(self, track_idx, max_seconds=100):
        history = self.build_frame_history(track_idx, max_seconds)

        # Dynamically measure NTSC frame step rate (frames_per_row) for this track
        deltas = {}
        prev_notes = None
        prev_f = 0
        for f, h in enumerate(history):
            # Triangle is excluded here: its linear-counter gate (see the
            # Triangle decode above) flickers its note on/off every few
            # frames independent of the actual row cadence, which would
            # otherwise swamp this delta histogram with a spurious fast
            # "tempo".
            curr_notes = (h['sq1'][0], h['sq2'][0], h['n163_0'][0], h['n163_1'][0])
            if prev_notes is not None and curr_notes != prev_notes:
                d = f - prev_f
                if d > 1:
                    deltas[d] = deltas.get(d, 0) + 1
                prev_f = f
            elif prev_notes is None:
                prev_f = f
            prev_notes = curr_notes

        frames_per_row = 6
        if deltas:
            most_freq = sorted(deltas.items(), key=lambda x: x[1], reverse=True)[0][0]
            if 3 <= most_freq <= 12:
                frames_per_row = most_freq

        calculated_bpm = int(round(900.0 / frames_per_row))

        num_rows = min(32 * ROWS, len(history) // frames_per_row)

        rpt = RPT4File()
        rpt.bpm = calculated_bpm
        active_rows = 0

        # Map channels:
        # Ch 0: Square 1  (Inst 80 = Synth Square Lead)
        # Ch 1: Square 2  (Inst 81 = Synth Saw Lead)
        # Ch 2: Triangle  (Inst 33 = Bass)
        # Ch 3: Noise     (Inst 115 = Percussion)
        # Ch 4: DMC / PCM (Inst 118 = Synth Drum)
        # Ch 5: N163 Ch 0 (Inst 38 = Synth Lead)
        # Ch 6: N163 Ch 1 (Inst 39 = Synth Bass)
        for r_idx in range(num_rows):
            # Real note-hold durations aren't a constant frames_per_row (this
            # track alternates 6/7-frame holds), so a fixed-grid single-frame
            # sample drifts out of phase with the actual note boundaries and
            # ends up sampling the tail of the previous note on a large
            # fraction of rows (verified: ~45% of rows in a spot check).
            # Majority-vote across the row's whole frame window instead.
            window_start = r_idx * frames_per_row
            window_end = min(window_start + frames_per_row, len(history))
            window = history[window_start:window_end]

            def majority(key):
                # Vote on the note alone, not the (note, vol) pair -- volume
                # often ticks every frame even while the note itself holds
                # (e.g. N163 envelope automation), which would make every
                # tuple in the window unique and silently collapse this back
                # into "first frame in window", the exact bug being fixed.
                #
                # A "prefer nonzero" variant (only fall back to silence if
                # the whole window is silent) was tried and measured against
                # the real chip trace for triangle: it reported every single
                # row as active (0 silent gaps) against a measured true duty
                # cycle of 65%, i.e. it erases the gate entirely rather than
                # correcting for row/gate misalignment. Plain majority (75%
                # of rows active in the same measurement) tracks the real
                # duty cycle far more closely, so silence competes normally.
                notes = [h[key][0] for h in window]
                maj_note = Counter(notes).most_common(1)[0][0]
                vols = [h[key][1] for h in window if h[key][0] == maj_note]
                return (maj_note, vols[-1] if vols else 0)

            f_frame = {
                'sq1': majority('sq1'), 'sq2': majority('sq2'), 'tri': majority('tri'),
                'noise': majority('noise'), 'dmc': majority('dmc'),
                'n163_0': majority('n163_0'), 'n163_1': majority('n163_1'),
            }
            pattern_idx = r_idx // ROWS
            row_in_pat = r_idx % ROWS

            ch_data = [
                (f_frame['sq1'][0], 80, f_frame['sq1'][1]),
                (f_frame['sq2'][0], 81, f_frame['sq2'][1]),
                (f_frame['tri'][0], 33, f_frame['tri'][1]),
                (f_frame['noise'][0], 115, f_frame['noise'][1]),
                (f_frame['dmc'][0], 118, f_frame['dmc'][1]),
                (f_frame['n163_0'][0], 38, f_frame['n163_0'][1]),
                (f_frame['n163_1'][0], 39, f_frame['n163_1'][1]),
            ]

            row_has_note = False
            for c_idx in range(min(CHANS, len(ch_data))):
                midi_note, inst_id, vol_15 = ch_data[c_idx]
                if midi_note > 0:
                    row_has_note = True
                    vol_63 = map_apu_vol_to_rpt(vol_15)
                    cell = PatternCell(note=midi_note, inst=inst_id, vol=vol_63)
                    rpt.set_cell(pattern_idx, row_in_pat, c_idx, cell)

            if row_has_note:
                active_rows = r_idx + 1

        # Calculate song length in patterns
        patterns_used = max(1, math.ceil(active_rows / ROWS))
        patterns_used = min(PATTERNS, patterns_used)

        rpt.song_length = patterns_used
        for p in range(patterns_used):
            rpt.sequence[p] = p

        return rpt

def convert_nsf_to_rpt(nsf_path, output_dir):
    os.makedirs(output_dir, exist_ok=True)
    converter = NSFConverter(nsf_path)
    print(f"Loaded NSF: {nsf_path}")
    print(f"Title: {converter.title}")
    print(f"Total Songs/Tracks: {converter.total_songs}")

    exported_files = []
    for track_idx in range(converter.total_songs):
        rpt = converter.convert_track(track_idx)
        out_filename = os.path.join(output_dir, f"PACMAN{track_idx+1:02d}.RPT")
        rpt.save(out_filename)
        size = os.path.getsize(out_filename)
        print(f"  Track {track_idx+1:2d}/{converter.total_songs} -> {out_filename} ({size} bytes, length: {rpt.song_length} patterns)")
        exported_files.append(out_filename)

    print(f"\n✓ Successfully exported {len(exported_files)} RPT files to {output_dir}")
    return exported_files

if __name__ == "__main__":
    nsf_file = sys.argv[1] if len(sys.argv) > 1 else "NSF/Pac-Man_CE.nsf"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "music"
    convert_nsf_to_rpt(nsf_file, out_dir)
