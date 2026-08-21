# Pac-Man CE OPL2 Translation Plan

Updated: 2026-08-20
Branch: NFS-Music-Take2

## Goal

Implement a true NSF-to-OPL2 translation layer for the Pac-Man CE music, using the importer as the semantic source and the RP6502 OPL2 runtime as the serialization contract.

This is not a patch-tuning exercise and not a guessed-instrument hack. The target is a correct translation from NSF note/instrument/volume semantics into RP6502 OPL2 register events.

## Constraints

- Use the importer in `tools/import_nsf.py` as the source of truth.
- Keep the runtime contract defined by `src/opl.c` and `src/opl.h` as the final arbiter.
- Use the RPTracker drum definitions at `../RPTracker/src/instruments.c` entries 253, 254, 255 for percussion/drum voices.
- Map volume correctly in OPL2 log space; do not treat volume as a simple linear 0..63 transfer.
- Do not perform `NOTE_OFF` when changing volume; keep the note alive to avoid clicks.
- Generate all 22 tracks from `NSF/Pac-Man_CE.nsf`.
- Use `NSF/track0.flac` as the validation target for the first 60 seconds of the target sound.

## Architecture

### 1) Import and normalize the NSF

- Iterate through all tracks in the NSF file (`total_songs`).
- For each track, call the importer and collect semantic row data:
  - note
  - volume
  - instrument category
  - channel lane
  - timing / delay
- Normalize to a common event stream before translation.

### 2) Route by lane

- Square 1 -> melodic lead / synth lane
- Square 2 -> secondary melodic lane
- Triangle -> bass / tonic support
- Noise -> percussion lane
- DMC -> percussion / transient lane
- N163 channel 0 / 1 -> support or alternate melodic content

### 3) Build a real OPL2 translation layer

Translate the semantic stream into register-level events:
- F-number from MIDI note
- key-on / key-off
- patch select
- volume / TL update
- envelope and pitch effects
- delay packet emission for RP6502 timing

### 4) Use dedicated OPL2 drum patches

- Use the drum patch definitions you identified in `../RPTracker/src/instruments.c`.
- Route Noise/DMC events into those drum voices.
- Use short attack and percussive decay for kicks/snare/claps/toms.
- Keep melodic translation separate from drum translation.

### 5) Volume semantics

- Convert importer volume to a perceptually correct OPL2 TL value.
- Use a monotonic logarithmic curve rather than a linear mapping.
- Update TL directly without note-off to prevent click artifacts.

## Implementation phases

### Phase 1: single-track proof of architecture

- Import one track from the NSF
- Convert to event stream
- Emit a direct OPL2 register stream
- Confirm the stream matches the runtime contract in `src/opl.c`
- Compare the first 60 seconds to `NSF/track0.flac`

### Phase 2: percussion and drum pipeline

- Map noise and DMC to drum voices using the RPTracker definitions
- Add transient envelopes and pitch/decay behaviors
- Validate that the percussion no longer clicks or pops unexpectedly

### Phase 3: all 22 tracks

- Expand the translator to all `total_songs`
- Ensure each track gets exported as a distinct `.BIN` asset
- Validate the generated outputs by track count and timing consistency

### Phase 4: refinement against the reference

- Compare the translated output against the NSF reference audio
- Adjust patch families, envelope curves, and drum transient shape
- Keep the work rooted in correct translation, not in guessed patches

## Success criteria

- All 22 tracks export successfully
- Track 0 matches the first 60 seconds of `NSF/track0.flac` in spirit and structure
- No volume-triggered note-off clicking
- OPL2 volume and drum behavior use proper log-space mapping and dedicated drum voices
- The generated `.BIN` stream is valid for the RP6502 OPL2 player contract

## Work log

### Current status

- [x] Branch confirmed: `NFS-Music-Take2`
- [x] Plan saved to `Plan.md`
- [ ] Create the translation event model for importer output
- [ ] Implement direct melodic note-to-OPL2 translation
- [ ] Add drum/percussion mapping from RPTracker patch definitions
- [ ] Validate against `NSF/track0.flac`
- [ ] Expand to all 22 tracks
- [ ] Finalize and record progress in Git

## Git discipline

- Keep this branch as the work-tracking branch.
- Update `Plan.md` as milestones complete.
- Record progress in the branch history as work advances.
- Do not mix unrelated cleanup into the music translation work.
