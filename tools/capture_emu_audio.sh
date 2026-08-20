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
# - --emu-seconds uses a wall-clock timer (real-time), not frame-script mode.

ROM="build/RPPacMan.rp6502"
EMU="./tools/rp6502-emu"
AUDIO_DEVICE=""
INTRO_SKIP="0"
DURATION=""
OUT=""
SAMPLE_RATE="48000"
CHANNELS="2"
AUTO_START="0"
PRE_ROLL="0.75"
START_NOISE="-42dB"
START_MIN_SILENCE="0.25"
EMU_SECONDS=""
EMU_STOP_GRACE="1"

usage() {
  cat <<EOF
Usage: $0 --audio-device N [options]

Required:
  --audio-device N       AVFoundation audio device index

Optional:
  --rom PATH             ROM path (default: ${ROM})
  --emu PATH             Emulator path (default: ${EMU})
  --intro-skip SEC       Seconds to trim from start (default: 0)
  --auto-start           Detect start from captured audio and trim automatically
  --pre-roll SEC         Keep this much audio before detected start (default: 0.75)
  --start-noise DB       silencedetect threshold (default: -42dB)
  --start-min-silence S  silencedetect minimum silence duration (default: 0.25)
  --duration SEC         Keep this many seconds after intro trim
  --emu-seconds SEC      Auto-exit emulator after this many real-time seconds
  --emu-stop-grace SEC   Wait time between stop signals (default: 1)
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
    --auto-start)
      AUTO_START="1"
      shift
      ;;
    --pre-roll)
      PRE_ROLL="${2:-}"
      shift 2
      ;;
    --start-noise)
      START_NOISE="${2:-}"
      shift 2
      ;;
    --start-min-silence)
      START_MIN_SILENCE="${2:-}"
      shift 2
      ;;
    --duration)
      DURATION="${2:-}"
      shift 2
      ;;
    --emu-seconds)
      EMU_SECONDS="${2:-}"
      shift 2
      ;;
    --emu-stop-grace)
      EMU_STOP_GRACE="${2:-}"
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

tmp_raw="$(mktemp -t rp6502_audio_raw_XXXXXX)"
tmp_timer_flag=""
cleanup() {
  rm -f "${tmp_raw}" >/dev/null 2>&1 || true
  if [[ -n "${tmp_timer_flag}" ]]; then
    rm -f "${tmp_timer_flag}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

echo "[capture] Recording from audio device ${AUDIO_DEVICE} to ${tmp_raw}"
ffmpeg -hide_banner -loglevel warning -y \
  -f avfoundation -i ":${AUDIO_DEVICE}" \
  -ar "${SAMPLE_RATE}" -ac "${CHANNELS}" \
  -c:a pcm_s16le -f wav "${tmp_raw}" &
ff_pid=$!

# Allow device to prime before launching emulator.
sleep 0.5

echo "[capture] Launching emulator: ${EMU} ${ROM}"
set +e
if [[ -n "${EMU_SECONDS}" ]]; then
  echo "[capture] Auto-exit enabled: ${EMU_SECONDS}s (wall-clock)"
  "${EMU}" "${ROM}" &
  emu_pid=$!
  tmp_timer_flag="$(mktemp -t rp6502_timer_fired_XXXXXX)"
  rm -f "${tmp_timer_flag}"

  stop_emu_with_signal() {
    local sig="$1"
    if kill -0 "${emu_pid}" >/dev/null 2>&1; then
      # Try children first, then parent.
      pkill -"${sig}" -P "${emu_pid}" >/dev/null 2>&1 || true
      kill -"${sig}" "${emu_pid}" >/dev/null 2>&1 || true
      return 0
    fi
    return 1
  }

  (
    sleep "${EMU_SECONDS}"
    if kill -0 "${emu_pid}" >/dev/null 2>&1; then
      echo "[capture] Auto-exit timer reached; stopping emulator (INT -> TERM -> KILL)"
      : > "${tmp_timer_flag}"
      stop_emu_with_signal INT || true
      sleep "${EMU_STOP_GRACE}"

      if kill -0 "${emu_pid}" >/dev/null 2>&1; then
        stop_emu_with_signal TERM || true
        sleep "${EMU_STOP_GRACE}"
      fi

      if kill -0 "${emu_pid}" >/dev/null 2>&1; then
        stop_emu_with_signal KILL || true
      fi
    fi
  ) &
  timer_pid=$!

  wait "${emu_pid}"
  emu_rc=$?

  # If we intentionally stopped on timer, a signal exit code is expected.
  if [[ -f "${tmp_timer_flag}" && ( ${emu_rc} -eq 130 || ${emu_rc} -eq 137 || ${emu_rc} -eq 143 ) ]]; then
    emu_rc=0
  fi

  if kill -0 "${timer_pid}" >/dev/null 2>&1; then
    kill "${timer_pid}" >/dev/null 2>&1 || true
    wait "${timer_pid}" 2>/dev/null || true
  fi
else
  "${EMU}" "${ROM}"
  emu_rc=$?
fi
set -e

capture_rc=0
if kill -0 "${ff_pid}" >/dev/null 2>&1; then
  echo "[capture] Stopping ffmpeg capture"
  kill -INT "${ff_pid}" >/dev/null 2>&1 || true
  wait "${ff_pid}" 2>/dev/null || capture_rc=$?
fi

duration_sec=""
if [[ -s "${tmp_raw}" ]]; then
  duration_sec="$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "${tmp_raw}" 2>/dev/null || true)"
fi

# ffmpeg often exits non-zero when interrupted intentionally; accept that if
# we still captured non-empty audio with measurable duration.
if [[ ${capture_rc} -ne 0 && ${capture_rc} -ne 130 && ${capture_rc} -ne 255 ]] || [[ ! -s "${tmp_raw}" ]] || [[ -z "${duration_sec}" ]]; then
  echo "[capture] Audio capture failed (ffmpeg rc=${capture_rc}, file='${tmp_raw}')." >&2
  echo "[capture] Check --audio-device and macOS output routing (BlackHole / Multi-Output)." >&2
  exit 1
fi

echo "[capture] Raw audio duration: ${duration_sec}s"

effective_intro_skip="${INTRO_SKIP}"
if [[ "${AUTO_START}" == "1" ]]; then
  detect_log="$(ffmpeg -hide_banner -nostats -v info -i "${tmp_raw}" -af "silencedetect=noise=${START_NOISE}:d=${START_MIN_SILENCE}" -f null - 2>&1 || true)"
  silence_end="$(printf '%s\n' "${detect_log}" | grep -m1 -oE 'silence_end: [0-9]+(\.[0-9]+)?' | awk '{print $2}')"

  if [[ -n "${silence_end}" ]]; then
    effective_intro_skip="$(awk -v s="${silence_end}" -v p="${PRE_ROLL}" 'BEGIN { v = s - p; if (v < 0) v = 0; printf "%.3f", v }')"
    echo "[capture] Auto-start: silence_end=${silence_end}s, pre_roll=${PRE_ROLL}s, trim=${effective_intro_skip}s"
  else
    echo "[capture] Auto-start: no silence_end detected; falling back to --intro-skip ${INTRO_SKIP}s"
  fi
fi

trim_args=( -hide_banner -loglevel warning -y -i "${tmp_raw}" )
if [[ "${effective_intro_skip}" != "0" ]]; then
  trim_args+=( -ss "${effective_intro_skip}" )
fi
if [[ -n "${DURATION}" ]]; then
  trim_args+=( -t "${DURATION}" )
fi
trim_args+=( -c:a flac "${OUT}" )

echo "[capture] Writing trimmed output: ${OUT}"
ffmpeg "${trim_args[@]}"

echo "[capture] Done. Emulator exit code: ${emu_rc}"
exit "${emu_rc}"
