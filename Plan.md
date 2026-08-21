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
- [x] Create the translation event model for importer output
- [x] Implement direct melodic note-to-OPL2 translation
- [x] Add drum/percussion mapping from RPTracker patch definitions
- [ ] Validate against `NSF/track0.flac`
- [ ] Expand to all 22 tracks
- [x] Finalize and record progress in Git

### Active phase

- [x] Phase 1: importer-driven semantic event model for the first track
- [x] Phase 1: direct note-to-OPL2 translation and volume mapping skeleton
- [ ] Phase 1: validation against `NSF/track0.flac`
- [x] Phase 1: generated a structurally sane Pac-Man CE asset at `music/PacManCE_01.BIN`

The importer output is now being treated as the semantic source, and a file-level translation pass has been created in `tools/opl2_translate.py` using the runtime contract in `src/opl.c`. We identified the true `.BIN` contract from the RPTracker export path: each record is `[reg, val, delay_lo, delay_hi]`, followed by an end marker of `0xFF, 0xFF, 0x00, 0x00`, and the file is padded to 512 bytes. The translator was then rewritten to match that contract, and the generated output now feels sane instead of representing a raw patch dump.

### 2026-08-20: offline render/compare loop + real OPL2 rhythm mode

Built an offline validation loop instead of relying on live BlackHole capture: `emu8950` (the same software OPL core `rp6502-emu`'s non-native audio path uses, `../rp6502/vendor/emu8950`, compiled standalone with `-DUSE_EMU8950_OPL=1`) renders a `.BIN` register stream straight to WAV honoring the 60Hz tick delays, and a PIL/numpy spectrogram + per-second RMS/spectral-centroid script compares a render against `NSF/track0.flac`.

First pass tried replacing `tools/opl2_translate.py`'s hand-tuned patch bank with RPTracker's canonical `../RPTracker/src/instruments.c` values — **this was wrong and was reverted**. Those RPTracker patches are the stock "Apogee" bank and are not a good fit for this project; the hand-tuning in `opl2_translate.py` was intentional, not drift. Lesson: RPTracker is the source of truth for the `.BIN` *contract*, not for what the patches should sound like.

Concrete, ear-confirmed problems and fixes, driven by feedback on the first 60s of `music/PacManCE_01.BIN`:
- **Drums were completely missing/wrong.** Root cause: noise/DMC were played as ordinary 2-op FM voices on channels 3/4 with hand-picked "drum" patches — 2-op FM can only produce tonal buzzes, never real percussion, because it never touches the chip's actual noise generator. Fixed by routing noise/DMC into real **OPL2 rhythm mode** (register `0xBD`, channels 6/7/8 = BD/SD/TOM/CYM/HH), which uses the chip's hardware noise for HH/SD/CYM. Verified real hardware support (`../rp6502/vendor/opl2_fpga/.../calc_rhythm_phase.sv`), verified `emu8950` implements it (so the offline renderer can validate it), and verified `src/opl.c`'s `music_reg_allowed()` already passes register `0xBD` through untouched — **no runtime/firmware change needed**, this is scoped entirely to `tools/opl2_translate.py`.
- **Freed a channel budget for future SFX.** Moving drums off channels 3/4 onto 6/7/8 frees channels for melodic use. Per user request, `sq1/sq2/tri/n163_0/n163_1` (5 logical NES/N163 lanes) now share 4 physical channels (0-3) dynamically via voice stealing (`OPL2Translator.acquire_channel`: prefer free, then idle, then steal the quietest/oldest active voice), guaranteeing channels 4-5 stay reserved and untouched for interactive SFX (eating pellets, ghosts, dying, etc.) that doesn't exist yet.
- **Voice 3 (triangle) was too loud.** Root cause: `import_nsf.py` reports the NES triangle channel at constant max volume (`15 if tri_note else 0`) because the real NES triangle has no volume register — accurate to the chip, but with no mix balancing on top, it always hit OPL TL=0 (loudest possible). Added a per-logical-source mix trim (`MIX_TRIM_BY_SOURCE`) applied in `volume_set`, generalizing (and keeping) the ad hoc `channel==5`/`channel==6` trims that existed before dynamic voice allocation made "channel" no longer mean "source".
- **The low voice (n163_0, inst 38) needed more buzz.** Root cause: the patch used a parallel/additive connection (`feedback` byte bit 0 = 1) with two identical plain-sine operators at the same multiple — the modulator was never actually modulating anything, just doubling the same sine. Switched to series/FM connection, raised the modulator's frequency multiple, gave it a harmonic-rich quarter-sine waveform, and added feedback, so the modulator now actually injects overtones into the carrier.
- The new rhythm-mode BD/SD/TOM/CYM/HH envelope bytes (patch IDs 253/254/255, repurposed from the old fake-FM "drum" patches) are a **reasoned first pass, not looked-up values** — research (ModdingWiki, VGMPF, DMXOPL) confirmed the rhythm-mode architecture and register semantics but turned up no authoritative published byte-level AdLib rhythm-kit table, so these need ear-driven iteration via BlackHole capture.

Offline render comparison against `NSF/track0.flac` (spectral centroid, RMS) moved from clearly wrong (centroid ~11-12kHz vs reference's ~2.2-3.3kHz, RMS ~5-10x too quiet) to structurally in the right neighborhood (centroid ~3.0-3.3kHz, RMS within ~2-3x, drum transients visibly raising peak levels where noise/DMC fire) — but this is a numeric/structural sanity check, not a substitute for actually listening. Next step is the user auditioning via BlackHole capture and giving feedback on the rhythm-kit envelopes, the triangle trim amount, and the n163_0 buzz character.

## What we learned

- The RPTracker export file is not a raw OPL2 patch list; it is a sequence of register writes with delay metadata.
- The runtime in `src/opl.c` reads 4-byte entries and treats `0xFF,0xFF` as the loop/restart sentinel.
- Earlier broken output came from emitting un-timed register bursts with no end marker or 512-byte alignment.
- The correct architecture is a stateful OPL2 voice translator, not a guessed patch-bank hack.
- RPTracker's own patch bank (`gm_bank`) is the stock "Apogee" AdLib set and is explicitly *not* wanted for this project's sound; RPTracker is only the source of truth for the `.BIN` runtime contract, not for patch content. Hand-tuned patches in `opl2_translate.py` are intentional and should not be silently replaced.
- Real drums require OPL2 rhythm mode (register `0xBD`, channels 6-8), not 2-op FM with a "drum-ish" patch — 2-op FM has no access to the chip's hardware noise generator, so it can never sound like real percussion, only a tonal beep.
- `../rp6502/vendor/emu8950` compiles standalone on the host (guard with `-DUSE_EMU8950_OPL=1`) and implements rhythm mode's real LFSR noise, so `.BIN` output — including drums — can be rendered to WAV and sanity-checked offline, without a live emulator session or BlackHole audio routing.
- Volume must be mapped in the OPL2 log-space TL domain, and should not force `NOTE_OFF` when changing amplitude.
- The importer output is the semantic source of truth, while the runtime contract is the final serializer truth.
- The first 60 seconds of `NSF/track0.flac` remains the correct benchmark for final tuning once the stream structure is valid.

## Git discipline

- Keep this branch as the work-tracking branch.
- Update `Plan.md` as milestones complete.
- Record progress in the branch history as work advances.
- Do not mix unrelated cleanup into the music translation work.
