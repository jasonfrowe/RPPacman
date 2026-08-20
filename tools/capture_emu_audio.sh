#!/usr/bin/env bash
set -euo pipefail

# Capture emulator audio from a macOS AVFoundation input and trim intro delay.
#
# Example:
#   tools/capture_emu_audio.sh \
#     --rom build/RPPacMan.rp6502 \
#     --audio-device 4 \
#     --intro-skip 12 \
#     --duration 60 \
#     --out captures/pacman_take1.flac
#
# Notes:
# - --audio-device is from: ffmpeg -f avfoundation -list_devices true -i ""
# - This script records to a temporary WAV first, then trims to final output.

ROM="build/RPPacMan.rp6502"
EMU="./tools/rp6502-emu"
AUDIO_DEVICE=""
INTRO_SKIP="0"
DURATION=""
OUT=""
SAMPLE_RATE="48000"
CHANNELS="2"

usage() {
  cat <<EOF
Usage: $0 --audio-device N [options]

Required:
  --audio-device N       AVFoundation audio device index

Optional:
  --rom PATH             ROM path (default: ${ROM})
  --emu PATH             Emulator path (default: ${EMU})
  --intro-skip SEC       Seconds to trim from start (default: 0)
  --duration SEC         Keep this many seconds after intro trim
  --out PATH             Output audio path (default: captures/<rom>_capture.flac)
  --sample-rate HZ       Capture sample rate (default: 48000)
  --channels N           Capture channels (default: 2)
  --list-devices         Print AVFoundation devices and exit
EOF
}

list_devices() {
  ffmpeg -f avfoundation -list_devices true -i "" 2>&1 || true
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --audio-device)
      AUDIO_DEVICE="${2:-}"
      shift 2
      ;;
    --rom)
      ROM="${2:-}"
      shift 2
      ;;
    --emu)
      EMU="${2:-}"
      shift 2
      ;;
    --intro-skip)
      INTRO_SKIP="${2:-}"
      shift 2
      ;;
    --duration)
      DURATION="${2:-}"
      shift 2
      ;;
    --out)
      OUT="${2:-}"
      shift 2
      ;;
    --sample-rate)
      SAMPLE_RATE="${2:-}"
      shift 2
      ;;
    --channels)
      CHANNELS="${2:-}"
      shift 2
      ;;
    --list-devices)
      list_devices
      exit 0
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "${AUDIO_DEVICE}" ]]; then
  echo "Missing required --audio-device" >&2
  usage >&2
  exit 2
fi

if [[ ! -x "${EMU}" ]]; then
  echo "Emulator not executable: ${EMU}" >&2
  exit 1
fi

if [[ ! -f "${ROM}" ]]; then
  echo "ROM not found: ${ROM}" >&2
  exit 1
fi

rom_base="$(basename "${ROM}" .rp6502)"
out_default="captures/${rom_base}_capture.flac"
OUT="${OUT:-$out_default}"
mkdir -p "$(dirname "${OUT}")"

tmp_raw="$(mktemp -t rp6502_audio_raw_XXXXXX.wav)"
cleanup() {
  rm -f "${tmp_raw}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[capture] Recording from audio device ${AUDIO_DEVICE} to ${tmp_raw}"
ffmpeg -hide_banner -loglevel warning -y \
  -f avfoundation -i ":${AUDIO_DEVICE}" \
  -ar "${SAMPLE_RATE}" -ac "${CHANNELS}" \
  -c:a pcm_s16le "${tmp_raw}" &
ff_pid=$!

# Allow device to prime before launching emulator.
sleep 0.5

echo "[capture] Launching emulator: ${EMU} ${ROM}"
set +e
"${EMU}" "${ROM}"
emu_rc=$?
set -e

if kill -0 "${ff_pid}" >/dev/null 2>&1; then
  echo "[capture] Stopping ffmpeg capture"
  kill -INT "${ff_pid}" >/dev/null 2>&1 || true
  wait "${ff_pid}" 2>/dev/null || true
fi

trim_args=( -hide_banner -loglevel warning -y -i "${tmp_raw}" )
if [[ "${INTRO_SKIP}" != "0" ]]; then
  trim_args+=( -ss "${INTRO_SKIP}" )
fi
if [[ -n "${DURATION}" ]]; then
  trim_args+=( -t "${DURATION}" )
fi
trim_args+=( -c:a flac "${OUT}" )

echo "[capture] Writing trimmed output: ${OUT}"
ffmpeg "${trim_args[@]}"

echo "[capture] Done. Emulator exit code: ${emu_rc}"
exit "${emu_rc}"
