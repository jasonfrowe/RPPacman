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

### 2026-08-20: voice-stealing regression found and fixed, and a real channel-budget question

User feedback after the rhythm-mode rewrite: overall a vast improvement, but voice 3 (triangle) had "vanished" rather than just gotten quieter. Root cause: `acquire_channel`'s steal-priority used each channel's *mixed* TL (post `MIX_TRIM_BY_SOURCE`) as its "safe to steal" signal — so the triangle's own new volume trim made it look like the best voice to evict, and it was getting cut off mid-note repeatedly (measured: 19 mid-note interruptions in the first ~860 rows), not just sitting back in the mix. Fixed by dropping TL/loudness from the steal heuristic entirely; stealing now only happens when all 4 pool channels are actively sounding at once, and picks the least-recently-triggered one. Triangle interruptions dropped to 0 in the same measurement.

While diagnosing this, measured how often the 4-channel dynamic pool is actually oversubscribed by the 5 melodic lanes (sq1/sq2/tri/n163_0/n163_1) across track 0's ~864 rows: **all 5 lanes are simultaneously active in 21.5% of rows, and 4+ lanes in 49.8%**. This is denser than the opening-bars sample that originally motivated "dynamic stealing, 2 spare channels for SFX" — sq1 and sq2 (the lead voices) are themselves getting stolen from 38-40 times each in this measurement window, not just the sparser n163 lanes. Worth revisiting whether 2 SFX-reserved channels is still the right tradeoff given how often the pool is actually saturated, versus 1 spare channel for less frequent (but not zero) contention.

### 2026-08-20: git-checkout mistake found, and partially corrected in the wrong direction

User feedback after the LRU-steal fix: overall improvement confirmed, but drums still not audible at all, and asked to double check the OPL2 playback mode. Investigating that surfaced a mistake in my own process: `git status` at the very start of this session already showed `tools/import_nsf.py` and `tools/opl2_translate.py` as modified (uncommitted work-in-progress predating this conversation). When I ran `git checkout -- tools/import_nsf.py tools/opl2_translate.py Plan.md` to undo my own RPTracker-patch-bank edit, that command doesn't undo one edit -- it discards *all* uncommitted changes and resets to the last commit, silently regressing several things I'd built on top of without noticing.

My first attempt to repair this assumed the discarded *uncommitted* content was the one to restore (recovered verbatim from my own first `Read` of both files, before any edits this session). **That assumption was backwards** -- per direct user correction, the uncommitted patches for instruments 80/81/33/39 were an abandoned, disliked experiment; the committed (git-HEAD) values for those same instruments, and `velocity_to_opl_tl`'s `log2` curve, are the ones actually being iterated on and approved throughout this conversation (including the "vast improvement" and triangle-fix feedback). Re-restored 80/81/33/39 and the `log2` volume curve back to the git-HEAD values. Lesson: "uncommitted changes exist" does not imply "uncommitted changes are the wanted state" -- ask, don't assume, when a `git checkout` is about to discard content that predates the session.

One thing did NOT get re-reverted, because it's an independent, unambiguous bug rather than a matter of preference: `import_nsf.py`'s noise-channel decode. The git-HEAD version had regressed the noise decode from a real per-hit pitch lookup (`NOISE_PERIOD_TO_MIDI[noise_period]`) to a hardcoded constant (`36` whenever noise is on) -- that constant sits below the HH/TOM threshold in `opl2_translate.py`'s rhythm router, so **100% of noise hits were landing on the cymbal voice**, never hi-hat or tom, the whole time this conversation's rhythm-mode work was being built and approved. This was never a "which version do you like" question; it's a straightforward correctness bug independent of the patch-bank preference issue, so the pitch-lookup fix stays.

While fixing the noise-pitch regression, also found a second real bug in the new rhythm code: `process_rhythm` was retriggering the target voice's attack on *every* row of a held noise burst (bursts run ~3 rows on average), instead of once at the burst's start -- so cymbal/tom, which are deliberately slower-decaying, never actually got to ring. Fixed to only fire on silence-to-active or a change of target voice, matching how the melodic note-on logic already avoids re-triggering an unchanged note.

Also acted on user feedback that the 4-channel dynamic melodic pool was too tight even with the LRU-only steal fix: gave back the channel, `MELODIC_POOL` is now `[0,1,2,3,4]` (5 channels for 5 logical sources, 1 reserved for future SFX instead of 2). Verified zero voice-stealing events now occur on track 0 (5 sources, 5 channels -- demand can never exceed supply). Noise, now correctly pitch-decoded, splits realistically across HH/TOM/CYM (237/126/69 hits respectively) instead of 432/0/0.

User then clarified the patch-bank preference was the other way around: the git-HEAD (committed) values for instruments 80/81/33/39 and the `log2` volume curve were the ones actually being iterated on and liked throughout this conversation; the uncommitted content my first recovery attempt restored was an abandoned, disliked experiment. Re-reverted 80/81/33/39 and the volume curve back to git-HEAD's values. The noise-pitch-decode fix and the retrigger-on-attack fix stayed, since those are independent correctness bugs, not a patch-preference question.

### 2026-08-20: mix balance -- drums too quiet, sq1/n163_0 too loud, triangle buried

Explicit instruction: stop comparing absolute loudness to `NSF/track0.flac`; the RMS gap vs the reference doesn't matter. What matters is relative balance between instruments -- drums inaudible, the first two voices (sq1, n163_0) too loud, triangle inaudible again (not cut off this time, just buried under sq1/n163_0).

Root cause for the drums: rhythm-mode voices never go through `volume_set` (that's a per-note melodic-channel mechanism) -- their level is whatever TL got written once in `rhythm_setup()`. HH's modulator and TOM/CYM's operators were left at a moderate attenuation (TL 4-16) on the assumption drums should sit back in the mix; instead they were far too quiet to register. In OPL2 rhythm mode HH/SD and TOM/CYM are NOT FM-chained -- each operator is independently audible -- so `*_ksl`'s TL directly sets that voice's own loudness. Maxed all four (BD carrier was already TL=0) to TL=0.

Measured effect (isolated renders, track 0, 21-30s window where drums are active): rhythm-only RMS went from ~0.013-0.017 (roughly -12dB under the melodic-only RMS in the same window) to 0.0205 RMS / 0.17 peak, now comparable to or louder than melodic-only's 0.0153 RMS / 0.06 peak in the same window -- drums read as present, not background noise.

For the melodic balance, extended `MIX_TRIM_BY_SOURCE` (ear-tuned, ch0/ch5 previously had no entry or an actual boost): sq1 (source 0) and n163_0 (source 5) now get +10 TL trim each (quieter); triangle (source 2)'s trim came down from +10 to +2 (much less attenuated, should read as present). This is a starting point, not a finished mix -- needs another listening pass.

### 2026-08-21: per-voice isolated review workflow, and a real whole-tone tuning bug

Too much happening at once to judge changes by ear against the full mix, so switched to a one-voice-at-a-time review workflow: a standalone script (not committed to the repo, lives in the session scratchpad) reuses `OPL2Translator.process_melodic`/`process_rhythm` directly to render just one logical voice (or a chosen combination) from real track-0 data to a WAV, bypassing the emulator/BlackHole loop entirely. Iterated through sq1 and n163_0 this way; user picked RPTracker patches by ear for each after listening to them isolated and combined (`gm_bank[0x3B]` Muted Trumpet for sq1/inst 80, `gm_bank[0x18]` Nylon Guitar for n163_0/inst 38, after an intermediate `gm_bank[6]` Harpsichord attempt). Also printed the actual MIDI note sequence per voice (row, timestamp, note name) on request, since "what note is this" is hard to answer by ear alone.

While isolating triangle, user reported the melody sounded flat relative to `NSF/track0.flac`, not just differently-voiced. Verified mathematically and then empirically (rendered a single A4 note and confirmed its measured fundamental): `midi_to_fnum()`'s `fnum_table` is tuned so index 0 is Bb, not C, but the code computed `note_idx = (note - 12) % 12`, which assumes index 0 is C -- a consistent **whole-tone (2 semitone) flat** error on every note, verified from ~-2.0 to -2.03 semitones off across MIDI 60/61/69/72. Fixed by changing the base offset from 12 to 10 for both `block` and `note_idx`; re-verified numerically to within 3 cents across the full practical octave range, and empirically via FFT on a rendered A4 (measured 439.99 Hz).

Important: `src/opl.c`'s `midi_to_opl_freq()` has the exact same table and the exact same off-by-12-instead-of-10 bug. It does **not** affect this project's pre-baked `.BIN` playback (the runtime just replays raw register bytes this translator already computed; `midi_to_opl_freq()` isn't in that call path), so it didn't need fixing here -- but it's a real, verified bug in shared runtime code that would affect anything calling `OPL_NoteOn()` directly (live SFX, if/when built). Flagged for the user, who asked to fix it too.

Fixed `src/opl.c`'s `midi_to_opl_freq()` to match: base offset 10 instead of 12 for both `block` and `note_idx`, and the low clamp moved from `midi_note < 12` to `midi_note < 10` (still exists for the same reason -- prevents `note_idx` going negative -- just needs to match the new base so it doesn't clamp one note too high). No `cmake` available in this environment to do a full ROM build/smoke-test, so this needs a build + on-device/emulator check before being trusted; the change itself is small and mechanically identical to the already math- and FFT-verified Python fix.

### 2026-08-21: triangle channel was missing the linear-counter gate (real bug, real fix)

User reported the triangle voice still sounded flat/wrong even on the Accordion patch, and asked whether the NSF was being interpreted correctly (comparing against their `track0.flac`, made via `vgm2wav -r 0 -t 60`, confirmed to be the same track 0 we're converting).

Cross-checked the CPU emulation first, since `tools/import_nsf.py` uses a hand-written fallback 6502 core when `py65` isn't installed: installed `py65` and re-ran the importer with it. Byte-identical output (same notes, same `bpm=129`, same `song_length=27`) to the custom fallback -- the CPU core is not the bug.

Then built a small harness (`gme_isolate.c`, scratchpad-only) against `vgm2wav`'s own `libgme` to isolate individual NSF voices with a mature, trusted emulator, as ground truth. Isolating the real Triangle channel and tracking narrowband energy at our computed frequency (537.79 Hz) across `track0.flac` showed only weak/ambiguous energy before t=14.4s and a clear, ~6x-stronger burst starting exactly at t=14.42s -- while our decode reported continuous triangle activity from t=7.12s. Traced the raw APU register writes directly (frame-by-frame, via the verified-correct CPU) and found the cause: register `$4008`'s bit 7 (the linear counter's control/halt flag) is held at 1 throughout this track, which forces the linear counter to reload from `$4008`'s low 7 bits every frame -- so the composer is using that reload value itself as a fast on/off gate (0 = silent, nonzero = audible), independent of the channel-enable and period bits our decode was already checking. This is a well-documented, easy-to-miss NES APU subtlety (NESdev wiki flags the triangle's linear counter as a common emulation-bug source for exactly this reason).

Fixed `import_nsf.py`'s Triangle decode to also require the linear-counter reload value be nonzero when the control flag is set (full linear-counter countdown emulation for the control=0 case isn't implemented -- not observed in this file). Side effect: this made triangle's note value flicker on/off every few frames even during genuinely sustained passages, which polluted the `frames_per_row` tempo auto-detector (it inspects note-change deltas across all channels) and briefly sent `bpm` from 129 to a bogus 300. Fixed by excluding triangle from that detector's input tuple -- its gate is an amplitude effect, not a tempo signal. `bpm`/`song_length` confirmed back to 129/27 after the exclusion.

Net effect: triangle is no longer a continuous tone from t=7.12s; it's the same underlying notes but rhythmically gated (522/864 rows active instead of all of them from row 61 on), matching what the reference recording actually does.

### 2026-08-21: dropping row/pattern quantization entirely -- translate straight from real 60Hz frames

User: n163_0 now fades away and never retriggers; triangle playback is still flawed; and why do we even need row requantization when the final `.BIN` format's delay is already free-running -- the 60Hz vsync tie is a video/tick constraint, not something the sound has to share.

That's correct, and it's a better fix than tuning the majority-vote threshold further. The RPT4 pattern/row grid exists for RPTracker editing compatibility; nothing about the OPL2 `.BIN` output actually needs it, and this pipeline never opens the intermediate file in RPTracker. Refactored: `NSFConverter.build_frame_history()` now returns the raw per-frame chip state with no row quantization (the frame-building loop pulled out of `convert_track`, which still uses it for the RPT4/`.RPT` export path unchanged). `OPL2Translator.translate_frame_history()` replaces the old row-driven `translate_track`: it calls `process_melodic`/`process_rhythm` once per real 60Hz frame instead of once per artificially-widened row -- those functions already only emit register writes when something actually changed, so no new "did it change" logic was needed, just the finer driving loop. 1 history frame = 1 tick in the runtime's delay units (both are the real 60Hz rate), so there's no `frames_per_row`/`bpm` conversion left to get wrong, and the triangle's linear-counter gate (or anything else that changes faster than 6-7 frames) is now represented exactly, not aliased through a majority vote. Also fixed the scratchpad `isolate_voice.py` review tool, which had silently kept using the old row-based path after this change -- it's outside the repo so nothing else needed updating.

**n163_0 (and it turns out sq1 too) fading away: a real bug, and a subtler one than it looked.** Traced the actual register writes and found the per-note TL value (the thing we control) was stable the whole time -- the fade wasn't there. The real cause: with glide (no retrigger) now used for same-instrument pitch changes, the OPL2 hardware envelope generator only gets a fresh Attack *once*, at the very start of the whole passage. Both patches (n163_0's Nylon Guitar, sq1's Muted Trumpet) came from RPTracker with Decay rate 1 -- the slowest possible -- which is fine for a normally-retriggered note (each note's own brief natural fade) but means the *one* envelope attack for an entire multi-second glided phrase spends many seconds crawling down toward its sustain level, audible as a slow fade to near-silence. Confirmed by checking sq1 in isolation too (same symptom, hadn't been reported yet) and triangle (fine -- its linear-counter gate causes real periodic retriggers, so it never hits this). Fixed by raising the decay rate on both patches from 1 to 9 (settles in a fraction of a second instead of many seconds).

That surfaced a second, related issue: even with fast decay, both voices settled at a level that was still too quiet. The patches' sustain level (SL) was independently attenuating on top of our own per-note TL/mix-trim system -- two separate attenuation stages stacking. Dropped carrier SL to 0 on both (modulator SL left alone -- it shapes FM brightness decay over time, not the core output level, and isn't causing the loudness problem) so the patch's envelope is just "as loud as it gets" and our own volume system is the sole loudness authority for a held note. Verified: both now hold a stable, audible level for the full 20s test instead of fading.

### 2026-08-21: a real timing bug in the frame-driven rewrite (the "hanging note")

User: isolated triangle has a note hanging from the very start that isn't there in the 3-voice mix; still too loud; and the patch reads as too "square", wants something closer to a sawtooth.

The hanging note was a genuine bug in yesterday's frame-driven rewrite, not just an isolation-tool quirk (though it happened to only be *visible* in isolation). `translate_frame_history` accumulated the gap between real events and attached it to the *new* batch's trailing event instead of the *previous* batch's trailing event -- off by one batch in whose delay represents "wait before this happens." For every other batch that's merely a timing shift; for the very first batch, when nothing preceded it (e.g. a solo triangle render with no rhythm setup already in the stream), there's nothing to attach a leading wait to at all, so it played instantly at t=0 instead of waiting the ~7 real seconds until its actual cue. In the full production mix this was invisible because `rhythm_setup()` (or an earlier-active voice like sq1) always populated the stream first, giving the gap somewhere to land -- but the timing shift itself was still there for every transition, just not obviously audible. Fixed in both `translate_frame_history` and the scratchpad `isolate_voice.py`: track the frame index of the last emitted batch, and when the next real event fires, set the *previous* batch's trailing delay to the gap (or, if nothing has been emitted yet, insert an explicit `0x01`/wait no-op to consume the leading silence). Verified: solo triangle is now silent through t=6.5s and starts exactly at t=7.0s, matching its real first activation frame. Rebuilt the production `.BIN` too, since this affected all timing there as well, just less visibly.

For "n163_0 lost its grit, maybe borrow from sq1": bumped n163_0's feedback from 0x06 (FB=3) to sq1's 0x0E (FB=7) -- the two patches are otherwise structurally identical (MULT=1 both operators, sustain envelope, series connection) after the recent fixes, and feedback amount is the main OPL2 lever for how much a sine operator buzzes up via self-modulation.

For triangle "too loud, too square": raised its mix trim from +8 to +14 (concrete, addresses the loudness complaint directly). Did not guess at a timbre fix -- OPL2 only has sine-family waveforms (0-3), no true square or sawtooth; what reads as "square" is more likely the Accordion patch's modulator multiple of 4 (a brighter, more inharmonic FM character) than anything waveform-selectable, and RPTracker's `gm_bank[81]` is literally named "Lead 2 (sawtooth)" if a more sawtooth-flavored patch is wanted next -- flagged for the user to decide rather than swapped speculatively.

### 2026-08-21: triangle -> gm_bank[81] Sawtooth Lead, applied proactively with the glide-safe pattern

User asked to try RPTracker's gm_bank[81] (Lead 2, Sawtooth) for triangle and cut its volume further. Checked the raw patch first: same structural problem as sq1/n163_0 before their fixes -- EGT=0 (percussive) on both operators and a carrier sustain level of 15 (near-silent) -- so applying it as-is would reproduce the multi-second fade-to-near-nothing bug under glide. Applied the same fix pattern proactively instead of shipping it broken and waiting for another report: EGT flipped to sustain on both operators, decay rate raised 0->9, carrier sustain level dropped to 0. `m_susrel` (modulator), feedback, KSL, and waveform left at the patch's original values. Also raised triangle's mix trim from +14 to +20. Verified stable, non-fading RMS through a 20s render.

User: still too loud even at +20 -- push triangle down further and bring sq1/n163_0's volume back up. Rebalanced: triangle +20 -> +28, sq1/n163_0 +10 -> +4 each. n163_1's trim (+6) untouched, not implicated in any feedback yet.

User: too faint now, and didn't like the Sawtooth Lead patch -- try gm_bank[0x1C] (Muted Guitar) instead, trim somewhere between +20 and +28. Checked the raw patch: modulator already sustain-type, carrier percussive with SL=12 -- applied the glide fix only to the carrier this time (EGT->sustain, decay 2->9, SL 12->0), left modulator/feedback/ksl/wave alone. Trim set to +24 (midpoint). Verified stable, non-fading.

### 2026-08-21: triangle reads as "out of tune" -- a hypothesis, and a test of it

User: triangle plays out of tune with the rest of the song on the Muted Guitar patch; asked for ideas why, and to try gm_bank[0x20] next.

Working hypothesis: the modulator's frequency multiple (MULT) amplifies the OPL2 `fnum_table`'s small built-in equal-temperament rounding error (already measured earlier when fixing the whole-tone bug: up to a few cents per note, e.g. ~3 cents on A4) at the rate of the harmonic number -- a 3-cent fundamental error becomes roughly a 9-cent error at the 3rd harmonic. Muted Guitar's modulator MULT is 3; sq1 ("now quite good") and every triangle patch that *hasn't* drawn an out-of-tune complaint use MULT=1. Not proven, but it's a concrete, testable difference between the patch that prompted the complaint and the one that didn't. Feedback amount is a secondary candidate (self-modulation distortion can also read as roughness at some pitches) but Muted Guitar's feedback (FB=2) is mild, making MULT the more likely driver.

Applied gm_bank[0x20] (Acoustic Bass) as a direct test: both operators already came sustain-type with MULT=1 on both -- matching sq1's modulator MULT exactly. Only needed the usual carrier glide-safety margin (decay 3->9, SL 2->0); modulator/feedback/ksl/wave left at the patch's original values. Verified stable, non-fading. If this reads as in-tune where Muted Guitar didn't, that's reasonable (not conclusive, `N=1`) support for the MULT hypothesis.

### 2026-08-21: exact f-number computation (real, verified tuning improvement) + a fine-tune knob

User: still flat on Acoustic Bass (which weakens the modulator-MULT hypothesis, since that patch's MULT matched sq1's) -- go back to Square Lead and add a way to fine-tune.

Replaced `midi_to_fnum`'s fixed 12-entry table with an exact computation of the OPL2 block/f-number directly from the target frequency (searching for the lowest block whose f-number fits in 10 bits, maximizing resolution). The table wasn't wrong in kind -- it was the fix for the earlier whole-tone bug -- but it was a fixed approximation with up to ~3 cents of per-note error; direct computation removes that rounding. Verified: average absolute tuning error across one octave dropped from about 1.15 cents (signed, up to -3.02/+4.22 on individual notes) to about 0.63 cents (up to ~1.45 cents on individual notes) -- roughly halved, likely near the practical limit of OPL2's f-number quantization at usable octaves. This also opened up a real fine-tune mechanism: `midi_to_fnum` and `note_on`/`note_glide` now take an optional `cents` offset, and a new `FINE_TUNE_CENTS_BY_SOURCE` dict (empty by default) can apply a deliberate per-voice detune if a specific voice still needs one. Verified the knob itself: MIDI A4 + 50 cents computes to 452.89 Hz against an expected 452.9 Hz.

Reapplied Square Lead (`gm_bank[0x50]`) to triangle -- the very first patch tried, back when the whole-tone fnum bug and the linear-counter gate bug were both still live, so the original "sounds flat" verdict on it almost certainly reflected those real bugs rather than the patch itself. Same envelope safety treatment as before (decay 2->9 both operators, carrier SL zeroed). Verified stable, non-fading. Whether it now reads as in-tune is the next thing to hear -- if it still doesn't, `FINE_TUNE_CENTS_BY_SOURCE[2]` is there to dial in directly rather than continuing to swap patches blind.

User: still flat, sharpen it; volume up a notch. Set `FINE_TUNE_CENTS_BY_SOURCE[2] = 15.0` as a first data point (no prior measurement of *how* flat it reads, so starting with a clearly audible nudge rather than a token amount -- easy to walk back). Trim eased from +24 to +20.

### 2026-08-21: measured against the real chip via libgme -- found a real ~7s gap, not a tuning issue; length-counter fix implemented but didn't resolve it

User: still out of tune, and asked to actually measure against the real NSF via `vgm2wav`/`libgme` rather than keep guessing, since their ear can tell there's a problem but not which direction or how far to correct.

Reused the `gme_isolate` harness from the earlier linear-counter investigation to render just the real triangle channel in isolation. First attempt used a stale mute mask (`27`, sized for 5 APU voices) against a 13-voice NSF (5 APU + 8 N163 channels) -- silently left all 8 N163 channels unmuted, so the "reference" was actually triangle+N163 mixed. Corrected mask (`8187`, muting all 12 other voices) gives a clean isolated triangle track.

With the correct isolation: the real triangle channel is **completely silent (exactly zero samples) from t=0 to ~t=14.2s**, then jumps to sustained activity (RMS 0.067, 81% of samples nonzero). Our decode has triangle first active at t=7.1s (frame 426) -- a genuine ~7-second gap, not a cents-level tuning error. This is almost certainly what's actually reading as "out of tune": real content colliding with content that shouldn't be playing yet.

Investigated several mechanisms and ruled most out:
- Not the linear-counter gate (already implemented) -- register data shows it legitimately open at frame 426.
- Not a duplicate/echo artifact -- checked whether the frame-426 and frame-846 sequences were exact repeats (would suggest double-processing); only 17/60 frames matched, consistent with normal scale reuse, not a mechanism.
- User chose to pursue real NES length-counter state (separate from the linear counter, also gated by the same halt bit) as the most likely remaining APU-level explanation. Implemented properly: `$4015` writes that clear a channel's enable bit now force that channel's length counter to zero; `$4003`/`$4007`/`$400B`/`$400F` (the length-load register for pulse1/pulse2/triangle/noise) reload it, but only if the channel is currently enabled. Didn't need the real 32-entry NES length table -- every entry in it is nonzero, and the halt flag is held at 1 for this whole track (confirmed earlier), so the counter never decrements once loaded; only the enable/reload *events* matter here, not specific durations. Applied to all four length-counter-bearing channels (pulse1/2, triangle, noise), not just triangle, since the same latent bug could affect any of them. Verified sq1 unaffected (still 1200/1200 active frames).
- Traced it directly: triangle's length counter gets exactly one load event in the whole 900-frame window, at frame 426 -- the same "too early" frame already suspected, never disabled afterward. The length-counter theory, correctly implemented, does not explain the gap.

Also found, while sanity-checking the measurement tooling itself: `gme_isolate`'s track-0 output has a clean, high-confidence (~0.97 normalized correlation) exact 1.0-second offset against the user's own `track0.flac`. That's a real discrepancy in my measurement setup, but far too small to account for the ~7s gap on its own.

Net: the length-counter fix is a legitimate correctness improvement (kept), and the git-vs-libgme measurement approach is proven sound (confirmed via three independent methods now: py65 cross-check, real reference audio, mask-verified isolation) -- but the specific mechanism causing triangle's ~7s-early entry is still unidentified after ruling out the standard APU gating registers. Likely candidates left: something specific to this NSF driver's own code (not a generic 2A03 register I haven't modeled), which would need actual disassembly around the frame-426 boundary rather than further register-trace guessing.

User then reported triangle now reads as too loud, and that sq1/n163_0 seem to fade when it enters, and asked whether the OPL2 log-scale volume conversion is correct. Answer: the conversion itself (`velocity_to_opl_tl`, a `**1.5` power curve into the TL register, which is already 0.75dB/step log-linear) isn't the mechanism at play for triangle -- the importer always reports triangle at max velocity (63, since the real NES triangle has no volume register), so the curve always resolves to TL=0 regardless of musical context; the curve never touches triangle's loudness at all. The actual lever is `MIX_TRIM_BY_SOURCE`, which had triangle at only +2 TL (barely attenuated) against sq1/n163_0's +10 each. There's no real per-channel gain interaction on OPL2 hardware -- one channel's TL cannot affect another's output -- so the "sq1/n163_0 fade when triangle enters" is masking (a louder, harmonically-rich triangle makes the comparatively quieter voices less noticeable), not a bug. Raised triangle's trim to +8, closer to the other two, as a starting point for the next listen.

### 2026-08-21: n163_0 clicking (real bug, fixed) and triangle "missing notes" (tried a fix, reverted it)

User: n163_0 clicks between notes -- from aggressive note-off? And triangle is missing a majority of its notes -- a 60Hz-vsync problem?

**n163_0 clicking: confirmed, real bug.** `process_melodic` did a full `note_off` then `note_on` on *every* note change, even a same-instrument pitch change on an already-sounding voice -- forcing a fresh envelope attack each time. With n163_0's arpeggio now correctly captured (changing pitch ~8.5 times/second after the row-sampling fix) and a fast-attack patch (Nylon Guitar), that's an audible click on every note boundary. Fixed with a `note_glide` method: on OPL2, writing new f-num/block bytes while the key-on bit stays set changes pitch in place without restarting the envelope -- standard FM-synth technique. `process_melodic` now only does a full retrigger when the instrument changes or the voice was silent; a same-instrument pitch change glides instead.

**Triangle missing notes: not quite a vsync issue, but a related row-quantization one -- investigated, then reverted my own fix.** Traced the real chip state again and measured: the row-level majority vote (used to pick each row's note) was discarding real triangle activity in 17 of 67 rows, because its linear-counter gate cycles faster (~3-4 frames) than the 7-frame row width, so it's a legitimate minority within many row windows. Tried fixing this by voting among nonzero notes only (silence loses unless the whole window is silent) -- but measuring the result against the real chip's duty cycle showed this was an overcorrection: it reported every single row as active (0 silent gaps) against a *measured true duty cycle of 65%*, i.e. it erased the gate's rhythm entirely rather than fixing a misalignment. Plain majority (what shipped) captures 75% of rows as active in the same measurement, much closer to that 65% true figure, with real silent gaps preserved. Reverted to plain majority. The clicking fix may address more of the "missing notes" perception than expected on its own, since a note getting cut short by an unwanted retrigger can read as "missing" -- worth another listen before deciding whether the row-quantization gap needs a different kind of fix (e.g. finer frames_per_row) rather than a different vote rule.

### 2026-08-21: the real note-sampling bug -- fixed-grid drift, then a bug in the fix itself

User: "we're being tripped up by the arpeggio and only playing some off-notes from the sequence." Verified directly: `convert_track`'s row loop samples a single fixed frame per row (`history[r_idx * frames_per_row]`), but the real note-hold durations for this track alternate 6/7/7 frames, not a constant `frames_per_row` -- so the fixed sampling grid drifts out of phase with the true note boundaries. Spot check: 36 of 80 rows (45%) sampled the tail of the *previous* note instead of the actual one for that row.

Fixed by majority-voting each channel's note across its full row window instead of sampling one frame. This introduced a second, self-inflicted bug: the vote was taken on the full `(note, vol)` tuple, and volume often ticks every single frame even while the note holds (N163 envelope automation does this) -- so nearly every tuple in a window was unique, and `Counter.most_common` silently degraded back to "first frame in window," i.e. almost the exact bug being fixed, but now hidden (it happened to look right for sq1, whose volume in this passage holds flat, and wrong for n163_0, whose volume doesn't). Fixed by voting on the note component alone, then taking the volume from the last frame matching the winning note. Verified: sq1/n163_0's octave-doubling relationship, previously a clean -25 semitones on every row before this whole investigation started, had been scrambled row-to-row by the tuple-voting bug (e.g. row 9 showed a -33 semitone gap) and is now back to a consistent -25 on every single row.

User also asked whether triangle sounding "flatter and out of tune" in the 3-voice mix (vs solo) pointed to another bug. Measured directly: extracted the actual sounding frequency of the same triangle note (C6, row 65) from both the solo and 3-voice-mix renders via FFT -- **identical, 1044.31 Hz in both**. The register data reaching the chip for triangle is byte-for-byte the same regardless of what else is playing; there is no pitch-shift bug. Most likely explanation is beating/masking between triangle's harmonics and sq1/n163_0's harmonics (all three now have overtone-rich patches after the RPTracker swaps) -- a normal FM-mixing perceptual effect, not something a code fix addresses. Responds to patch/level choices, not a bug fix.

Offline render comparison against `NSF/track0.flac` (spectral centroid, RMS) moved from clearly wrong (centroid ~11-12kHz vs reference's ~2.2-3.3kHz, RMS ~5-10x too quiet) to structurally in the right neighborhood (centroid ~3.0-3.3kHz, RMS within ~2-3x, drum transients visibly raising peak levels where noise/DMC fire) — but this is a numeric/structural sanity check, not a substitute for actually listening. Next step is the user auditioning via BlackHole capture and giving feedback on the rhythm-kit envelopes, the triangle trim amount, and the n163_0 buzz character.

### 2026-08-21: quarter-frame APU timing (4x oversample) + decoupled audio tick rate

User's redirect: NTSC's real APU envelope/counter clock runs at ~240Hz (quarter-frame), 4x denser than the 60Hz `PLAY`-call rate we'd been sampling at -- not cycle-accurate to 1.79MHz, and explicitly fine with that ("we don't have to have the exact same tempo"), but worth modeling that 4x ratio since the RP6502 (8MHz CPU, 60Hz vsync only for video) has trivial headroom to run audio ticks independent of the video frame rate. Goal: "good," not "perfect."

Implemented in `tools/import_nsf.py`: replaced the boolean `length_nonzero` tracking and the triangle-only `tri_linear_gate` register snapshot with real integer `length_counter` (sq1/sq2/tri/noise) and `linear_counter` (triangle) state, advanced by genuine quarter-frame (linear counter, all 4 sub-ticks per frame) and half-frame (length counters, every other sub-tick) clocks -- run internally at true 240Hz regardless of how many of those sub-ticks get kept. `build_frame_history()` (still used by `convert_track()`'s RPT4/RPTracker-grid export path) keeps its 60Hz-per-entry contract by keeping only the last of each 4 sub-ticks; a new `build_subframe_history()` keeps all 4, giving quarter-frame-resolution history to the direct OPL2 path. `opl2_translate.py`'s `translate_nsf_track()`/`export_all_tracks()` now call `build_subframe_history()` -- no other logic changes needed, since `translate_frame_history()` already treated each history entry as a generic tick with no assumption about what real time it represents (confirms the earlier row-quantization removal was the right foundation). `src/main.c` scales `music_ticks` by 4x so 1 `.BIN` tick = 1 real quarter-frame at runtime, matching the new contract; affects all three music tracks since they share one player, confirmed acceptable. **Not build/test-verified on-device** (no `cmake` in this environment) -- needs a real build + on-device/emulator check.

Verified via the offline `opl2_render`/`gme_isolate` scratchpad tools: total render duration unchanged (100.0s in, 100.0s out -- the 4x-denser ticks represent proportionally shorter real time each, so overall tempo is preserved, not sped up); event count rose modestly (21469 -> 27141, ~26%, not a 4x blowup, since most sub-ticks still collapse into the same output event via the existing gap-coalescing logic); sq1 and n163_0 (previously approved) show no structural regression (still ~99.8%/100% active-time fraction, no new dropouts). Triangle's onset **did not move** (still ~t=7.0s vs the real chip's ~t=14.0s, re-confirmed via a fresh `gme_isolate` render with the correct 13-voice mute mask) -- exactly the outcome flagged as possible going in: this track holds triangle's linear-counter control/halt bit at 1 for its entire duration, which forces a reload every single quarter-frame regardless of whether that clock runs at 60Hz or 240Hz, so finer sampling of that particular gate can't change its outcome for this file. The quarter-frame architecture is real and correct as a general improvement; the specific ~7s triangle-onset gap remains unexplained by any APU register mechanism checked so far (linear counter, length counter, quarter-frame timing) and would need actual driver disassembly around the frame-426 boundary, not further register-trace guessing, to pin down further.

Regenerated `music/PacManCE_01.BIN` from the new pipeline (109056 bytes, 27141 events).

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
