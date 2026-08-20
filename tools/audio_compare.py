#!/usr/bin/env python3
"""
Objective audio comparison helper.

Given a reference file (for example NSF/03_Normal_Mode.flac) and a candidate
recording (for example an emulator capture), this script compares loudness and
spectral shape so patch changes are no longer blind.

Requirements:
  - ffmpeg on PATH

Example:
  python3 tools/audio_compare.py \
    --reference NSF/03_Normal_Mode.flac \
    --candidate captures/pacman_clean.wav \
    --start 5 --duration 45
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Stats:
    i_lufs: float | None
    lra: float | None
    tp_dbfs: float | None
    rms_db: float | None
    centroid_hz: float | None
    spread_hz: float | None
    flatness: float | None
    entropy: float | None


def run_ffmpeg(cmd: list[str]) -> str:
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "ffmpeg failed")
    return proc.stderr


def parse_ebur128(stderr: str) -> tuple[float | None, float | None, float | None]:
    # Final summary includes lines like:
    # I:         -16.9 LUFS
    # LRA:         8.1 LU
    # Peak:       -1.2 dBFS
    i_lufs = None
    lra = None
    peak = None

    vals = re.findall(r"\bI:\s*([-+]?\d+(?:\.\d+)?)\s*LUFS", stderr)
    if vals:
        i_lufs = float(vals[-1])

    vals = re.findall(r"\bLRA:\s*([-+]?\d+(?:\.\d+)?)\s*LU", stderr)
    if vals:
        lra = float(vals[-1])

    vals = re.findall(r"\bPeak:\s*([-+]?\d+(?:\.\d+)?)\s*dBFS", stderr)
    if vals:
        peak = float(vals[-1])

    return i_lufs, lra, peak


def parse_astats(stderr: str) -> float | None:
    # Use overall RMS level from astats output.
    m = re.search(r"RMS level dB:\s*([-+]?\d+(?:\.\d+)?)", stderr)
    if m:
        return float(m.group(1))
    return None


def parse_aspectralstats(stderr: str) -> tuple[float | None, float | None, float | None, float | None]:
    # Pull every frame value, then take median for robustness.
    def collect(name: str) -> float | None:
        vals = [float(v) for v in re.findall(rf"{name}=([-+]?\d+(?:\.\d+)?)", stderr)]
        if not vals:
            return None
        return statistics.median(vals)

    centroid = collect("centroid")
    spread = collect("spread")
    flatness = collect("flatness")
    entropy = collect("entropy")
    return centroid, spread, flatness, entropy


def analyze(path: Path, start: float, duration: float | None) -> Stats:
    if not path.exists():
        raise FileNotFoundError(str(path))

    time_args: list[str] = []
    if start > 0:
        time_args += ["-ss", f"{start:.3f}"]
    if duration is not None and duration > 0:
        time_args += ["-t", f"{duration:.3f}"]

    # Loudness summary
    ebur_cmd = [
        "ffmpeg", "-hide_banner", "-nostats", "-v", "info",
        *time_args,
        "-i", str(path),
        "-af", "ebur128=peak=true",
        "-f", "null", "-",
    ]
    ebur_out = run_ffmpeg(ebur_cmd)
    i_lufs, lra, tp_dbfs = parse_ebur128(ebur_out)

    # Time-domain RMS
    astats_cmd = [
        "ffmpeg", "-hide_banner", "-nostats", "-v", "info",
        *time_args,
        "-i", str(path),
        "-af", "astats=metadata=0:reset=1:length=0.4",
        "-f", "null", "-",
    ]
    astats_out = run_ffmpeg(astats_cmd)
    rms_db = parse_astats(astats_out)

    # Spectral-frame metrics
    spec_cmd = [
        "ffmpeg", "-hide_banner", "-nostats", "-v", "info",
        *time_args,
        "-i", str(path),
        "-af", "aspectralstats=win_size=2048:overlap=0.75:measure=centroid+spread+flatness+entropy,ametadata=mode=print",
        "-f", "null", "-",
    ]
    spec_out = run_ffmpeg(spec_cmd)
    centroid_hz, spread_hz, flatness, entropy = parse_aspectralstats(spec_out)

    return Stats(
        i_lufs=i_lufs,
        lra=lra,
        tp_dbfs=tp_dbfs,
        rms_db=rms_db,
        centroid_hz=centroid_hz,
        spread_hz=spread_hz,
        flatness=flatness,
        entropy=entropy,
    )


def fmt(v: float | None, ndigits: int = 2) -> str:
    return "n/a" if v is None else f"{v:.{ndigits}f}"


def print_report(ref: Stats, cand: Stats) -> None:
    print("Metric,Reference,Candidate,Delta(C-R)")

    def line(label: str, r: float | None, c: float | None, nd: int = 2):
        d = None if (r is None or c is None) else (c - r)
        print(f"{label},{fmt(r, nd)},{fmt(c, nd)},{fmt(d, nd)}")

    line("I_LUFS", ref.i_lufs, cand.i_lufs)
    line("LRA_LU", ref.lra, cand.lra)
    line("TruePeak_dBFS", ref.tp_dbfs, cand.tp_dbfs)
    line("RMS_dB", ref.rms_db, cand.rms_db)
    line("Centroid_Hz", ref.centroid_hz, cand.centroid_hz, 1)
    line("Spread_Hz", ref.spread_hz, cand.spread_hz, 1)
    line("Flatness", ref.flatness, cand.flatness, 4)
    line("Entropy", ref.entropy, cand.entropy, 4)


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Compare a candidate audio render against a reference recording.")
    p.add_argument("--reference", required=True, help="Reference audio path, e.g. NSF/03_Normal_Mode.flac")
    p.add_argument("--candidate", required=True, help="Candidate audio path to compare")
    p.add_argument("--start", type=float, default=0.0, help="Start offset in seconds (applied to both files)")
    p.add_argument("--duration", type=float, default=45.0, help="Analyze this many seconds from start")
    p.add_argument("--json", dest="json_out", action="store_true", help="Output JSON in addition to CSV")
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    ref_path = Path(args.reference)
    cand_path = Path(args.candidate)

    ref = analyze(ref_path, args.start, args.duration)
    cand = analyze(cand_path, args.start, args.duration)

    print_report(ref, cand)

    if args.json_out:
        out = {
            "reference": ref.__dict__,
            "candidate": cand.__dict__,
        }
        print(json.dumps(out, indent=2))

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
