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
sys.path.insert(0, './tools')
from import_nsf import NSFConverter
from nsf_to_opl2 import translate_rpt_to_bin

def convert_rpt_to_bin(rpt, output_bin, target_loops=2, profile='bright'):
    translate_rpt_to_bin(rpt, output_bin, target_loops=target_loops, profile=profile)

def parse_args(argv):
    parser = argparse.ArgumentParser(description='Export Pac-Man CE NSF tracks to RP6502 OPL2 BIN files.')
    parser.add_argument('--profile', choices=['clean', 'balanced', 'bright'], default='bright',
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
