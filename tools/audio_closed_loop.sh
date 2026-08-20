#!/usr/bin/env bash
set -euo pipefail

# Closed-loop audio iteration harness.
#
# Per iteration:
# 1) optional build command
# 2) capture emulator audio for a fixed runtime
# 3) compare capture against reference and store metrics
# 4) optional update command (e.g., regenerate patches/BIN)
#
# Example:
# tools/audio_closed_loop.sh \
#   --iterations 3 \
#   --audio-device 1 \
#   --emu-seconds 90 \
#   --trim-duration 60 \
#   --reference NSF/03_Normal_Mode.flac \
#   --build-cmd "cmake --build build" \
#   --update-cmd "echo update patches here"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

FFMPEG_BIN="${FFMPEG_BIN:-$(command -v ffmpeg || true)}"
if [[ -z "${FFMPEG_BIN}" ]]; then
  for candidate in /opt/homebrew/bin/ffmpeg /usr/local/bin/ffmpeg /opt/local/bin/ffmpeg /usr/bin/ffmpeg; do
    if [[ -x "${candidate}" ]]; then
      FFMPEG_BIN="${candidate}"
      break
    fi
  done
fi
if [[ -z "${FFMPEG_BIN}" ]]; then
  echo "ffmpeg not found on PATH; install FFmpeg or set FFMPEG_BIN." >&2
  exit 1
fi
PYTHON_BIN="${PYTHON_BIN:-$(command -v python3 || command -v python || true)}"
if [[ -z "${PYTHON_BIN}" ]]; then
  echo "Python interpreter not found on PATH" >&2
  exit 1
fi

ITERATIONS="3"
AUDIO_DEVICE="${AUDIO_DEVICE:-0}"
ROM="build/llvm-mos/release/RPPacMan.rp6502"
REFERENCE="NSF/track0.flac"
CAPTURE_DIR="captures/loop"
EMU_SECONDS="90"
TRIM_DURATION="60"
AUTO_START="1"
INTRO_SKIP="0"
PRE_ROLL="1.25"
START_NOISE="-42dB"
START_MIN_SILENCE="0.25"
BUILD_CMD=""
UPDATE_CMD=""
COMPARE_START="0"
COMPARE_DURATION="60"

usage() {
  cat <<EOF
Usage: $0 [--audio-device DEVICE] [options]

Options:
  --audio-device DEVICE      AVFoundation device index or name (default: BlackHole 2ch)

Options:
  --iterations N             Number of iterations (default: ${ITERATIONS})
  --rom PATH                 ROM path (default: ${ROM})
  --reference PATH           Reference audio (default: ${REFERENCE})
  --capture-dir DIR          Output directory (default: ${CAPTURE_DIR})
  --emu-seconds SEC          Emulator runtime per take (default: ${EMU_SECONDS})
  --trim-duration SEC        Trimmed output duration per take (default: ${TRIM_DURATION})
  --auto-start               Auto-detect start from silence (default on)
  --no-auto-start            Disable auto-detect, use --intro-skip
  --intro-skip SEC           Fixed trim offset if auto-start disabled (default: ${INTRO_SKIP})
  --pre-roll SEC             Audio kept before detected start (default: ${PRE_ROLL})
  --start-noise DB           silencedetect threshold (default: ${START_NOISE})
  --start-min-silence SEC    silencedetect min silence (default: ${START_MIN_SILENCE})
  --compare-start SEC        Comparison window start (default: ${COMPARE_START})
  --compare-duration SEC     Comparison window duration (default: ${COMPARE_DURATION})
  --build-cmd CMD            Command to run before each capture (optional)
  --update-cmd CMD           Command to run after each compare (optional)
  -h, --help                 Show help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --audio-device)
      AUDIO_DEVICE="${2:-}"
      shift 2
      ;;
    --iterations)
      ITERATIONS="${2:-}"
      shift 2
      ;;
    --rom)
      ROM="${2:-}"
      shift 2
      ;;
    --reference)
      REFERENCE="${2:-}"
      shift 2
      ;;
    --capture-dir)
      CAPTURE_DIR="${2:-}"
      shift 2
      ;;
    --emu-seconds)
      EMU_SECONDS="${2:-}"
      shift 2
      ;;
    --trim-duration)
      TRIM_DURATION="${2:-}"
      shift 2
      ;;
    --auto-start)
      AUTO_START="1"
      shift
      ;;
    --no-auto-start)
      AUTO_START="0"
      shift
      ;;
    --intro-skip)
      INTRO_SKIP="${2:-}"
      shift 2
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
    --compare-start)
      COMPARE_START="${2:-}"
      shift 2
      ;;
    --compare-duration)
      COMPARE_DURATION="${2:-}"
      shift 2
      ;;
    --build-cmd)
      BUILD_CMD="${2:-}"
      shift 2
      ;;
    --update-cmd)
      UPDATE_CMD="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2
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

PYTHON_BIN="${PYTHON_BIN:-$(command -v python3 || command -v python || true)}"
if [[ -z "${PYTHON_BIN}" ]]; then
  echo "Python interpreter not found on PATH" >&2
  exit 1
fi

if [[ ! -f "${REFERENCE}" ]]; then
  echo "Reference not found: ${REFERENCE}" >&2
  exit 1
fi

mkdir -p "${CAPTURE_DIR}"

for i in $(seq 1 "${ITERATIONS}"); do
  echo ""
  echo "========== Iteration ${i}/${ITERATIONS} =========="

  if [[ -n "${BUILD_CMD}" ]]; then
    echo "[loop] Build step"
    eval "${BUILD_CMD}"
  fi

  take_file="${CAPTURE_DIR}/take_$(printf '%02d' "${i}").flac"
  metrics_file="${CAPTURE_DIR}/take_$(printf '%02d' "${i}")_metrics.csv"

  cap_args=(
    --rom "${ROM}"
    --audio-device "${AUDIO_DEVICE}"
    --emu-seconds "${EMU_SECONDS}"
    --duration "${TRIM_DURATION}"
    --out "${take_file}"
    --pre-roll "${PRE_ROLL}"
    --start-noise "${START_NOISE}"
    --start-min-silence "${START_MIN_SILENCE}"
  )

  if [[ "${AUTO_START}" == "1" ]]; then
    cap_args+=( --auto-start )
  else
    cap_args+=( --intro-skip "${INTRO_SKIP}" )
  fi

  echo "[loop] Capture step"
  ./tools/capture_emu_audio.sh "${cap_args[@]}"

  echo "[loop] Compare step"
  "${PYTHON_BIN}" tools/audio_compare.py \
    --reference "${REFERENCE}" \
    --candidate "${take_file}" \
    --start "${COMPARE_START}" \
    --duration "${COMPARE_DURATION}" | tee "${metrics_file}"

  if [[ -n "${UPDATE_CMD}" && "${i}" -lt "${ITERATIONS}" ]]; then
    echo "[loop] Update step"
    eval "${UPDATE_CMD}"
  fi

done

echo ""
echo "[loop] Done. Captures and metrics are in ${CAPTURE_DIR}"
