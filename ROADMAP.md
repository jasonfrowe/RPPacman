# RPPacMan Roadmap

Updated: 2026-08-23
Branch: gameplay-sfx-tuning (forked from main after the NFS-Music-Take2 merge)

Big-picture task list for finishing Pac-Man CE beyond the music work tracked
in `Plan.md` (which stays scoped to the NSF->OPL2 translation effort). We are
close to a complete game loop for Normal mode; this is what's left.

## Status: Normal mode

Playable end-to-end (title -> menu -> maze -> game over -> title). Missing
before it's "done":

### i. Sound effects in Normal mode -- DONE

Wired up. `sfx_set_ambient`/`sfx_play`/`sfx_stop`/`update_sfx_advance` in
`src/opl.c` drive OPL2 channel 5 as a fully independent lane alongside the
always-playing gameplay music: an ambient loop (frightened or normal, track
20/21) plays by default, with one-shot stingers (12=death, 13=pellet,
15=ghost-eaten, 16=maze-update, 18=prize-placed, 19=extra-life) cutting in
and automatically falling back to the current ambient when they finish.
Call sites live in `src/player.c` (pellet/extra-life), `src/ghost.c`
(death/ghost-eaten/frightened-ambient-swap), and `src/prizes.c`
(maze-update/prize-placed). Tracks 8-11/17 (the short slide/bleep effects)
were not wired to a specific gameplay event -- still candidates for the
Sound Test screen (ii-e) rather than an automatic trigger.

### ii. OPTIONS menu

Menu shell exists (`TITLE_SUBSTATE_MENU_READY` lists NORMAL/EXTRA/OPTIONS
in `src/main.c`) but OPTIONS itself has no submenu yet. Sub-items:

- **ii-a. Achievements** -- track milestones (needs a persistence story;
  RP6502 has no battery-backed save by default, check what's available).
- **ii-b. Ranking** -- track and display high scores (also needs
  persistence).
- **ii-c. Replay Normal** (stretch) -- rewatch the last Normal-mode game.
  Needs an input/event log recorded during play, replayed through the
  same deterministic update loop.
- **ii-d. Replay Extra** (stretch) -- same as ii-c for Extra mode.
- **ii-e. Sound test** -- play any music/SFX track on demand. Straightforward
  once the SFX-trigger API from (i) exists -- this is largely a UI over it,
  and a good forcing function to have *all* tracks (not just 0/1/2)
  actually wired into the build.
  Sound test also isn't just a QoL menu here -- it's a screen where music
  actually needs to be played standalone, same as the wide-profile tracks.
- **ii-f. Exit** -- return to main menu.

### iii. Extra mode

Needs extra maps built first (blocked on map/asset work, not audio).

### iv. Palette swap synced to kick-drum -- DONE

Built via option (a): `opl_consume_kick_hit()` in `src/opl.c` flags a
rising edge on rhythm register `0xBD`'s bass-drum bit, consumed once per
frame by `tile_mode2_palette_update()`. Palette index 11 mirrors index 6's
current color for 8 frames after each real kick, otherwise black.

Also built, same session: a frightened-mode outline-color cycle. Palette
indices 6/8 step through an 8-stage color sequence
(`FRIGHTENED_PALETTE_STAGES` in `src/tile_mode2.c`, written by
`set_frightened_palette_stage()`) while ghosts are vulnerable, reverting
to normal on expiry or Pac-Man death. `src/ghost.c` paces the 8 stages
evenly across whatever frightened duration is active for the current
level (`FRIGHTENED_DURATION_TABLE`, 600 down to 240 frames) via a plain
`duration >> 3` shift, so the cycle runs faster on later, shorter
levels -- matching the pacing of the ghosts' own sprite flash-rate
speedup.

### v. Game-over -> Results transition

New title-state machine states, mirroring the existing
`TITLE_SUBSTATE_WARM_FADE_OUT`/`BLACK`/`FADE_IN` pattern already used for
the game-over -> title transition.

### vi. Results screen

Shows how well the player did, including a histogram of points over time.
Needs score sampled at some interval during play (a ring buffer written in
`update_game_timer_display()`'s neighborhood, or wherever the per-frame
gameplay tick already lives) and a bar/line-graph renderer in tile or
sprite mode.

### vii. Ranking screen

Appears after Results. Depends on (ii-b)'s persistence story.

## Music: what's confirmed and what's still open

From the NSF instrument census (all 22 tracks, `NSF/Pac-Man_CE.nsf`) and
in-game listening, current best understanding of each track -- **the
game-usage descriptions are the user's own, from playing/listening; no
authoritative public source for this homebrew/demake's internals was found
(see caveat below)**:

| Track | Use | Length | Notes |
|---|---|---|---|
| 00 | Maze/gameplay music | 300s | Sounding good after the triangle gap-bridging fix; staying with this version. |
| 01 | READY?/GO! jingle (maze start) | ~4s | Needs regen (old free-channel method). |
| 02 | Title screen music | 300s | Sounding okay; needs regen. |
| 03 | High-score screen | ~15s | Never concurrent with other music/SFX -- moved to the wide channel profile (up to 4 simultaneous N163 channels, previously silently dropped to 2). Not yet implemented in-game. |
| 04 | EXTRA mode music | 300s | `.wav` sounds good, `.bin` sounds bad on hardware -- checked for waveform-2/3 patches (none) and rapid DMC retriggering (DMC has only 44 transitions over 30s, 0.67-1.2s on-runs -- not the staccato pattern that broke triangle); root cause still open, needs a fresh listen after regen. Not yet implemented in-game. |
| 05 | Results-phase music | 300s | Wide profile (4 simultaneous N163). Not yet implemented in-game. |
| 06 | Unused | 300s | Regenerate anyway for consistency; no known use. |
| 07 | OPTIONS menu music | 300s | Reported note-clipping, most likely genuine channel oversubscription (not a chip-model issue) -- moved to the wide profile. Not yet implemented in-game. |
| 08 | Slide-down effect | ~0.1s | |
| 09 | Slide-up effect | ~0.1s | |
| 10 | Looks like 08+09 combined | ~0.2s | |
| 11 | (unlabeled short effect) | ~0.6s | |
| 12 | Pac-Man caught/dying | ~2.4s | |
| 13 | Slide-up effect | ~0.2s | |
| 14 | Pellet-eating (chomp) | 300s | Long file is almost certainly a repeating "wakawaka" loop, not one continuous 5-minute idea. |
| 15 | Slide-up effect | ~0.4s | |
| 16 | Prize eaten + maze transition | 300s | |
| 17 | Short bleep | ~0.3s | |
| 18 | Prize spawning | ~0.5s | |
| 19 | Extra life jingle | ~2s | |
| 20 | Ghosts vulnerable (post power-pellet) | 300s | |
| 21 | Pac-Man wandering (normal-mode idle/ambient) | 300s | |

**Source-confirmation caveat**: searched for a public track list / sound
test documentation for this game specifically (it's an unreleased 2008
homebrew demake by "coke774," later finished by M2 for the official 2020
Namcot Collection release -- no disassembly or source was ever published).
Nothing at that granularity turned up. One search result (a TCRF page)
returned an embedded prompt-injection attempt instead of real content and
was discarded, not used as a source. Treat the table above as informed by
play-testing, not externally verified.

## Channel allocation (music generation)

Two profiles in `tools/opl2_translate.py`, selected automatically by track
index (`WIDE_TRACK_INDICES = {3, 5, 7}`):

- **Standard** (tracks 0,1,2,4,6,8-21): channels 0-4 = sq1/sq2/tri/n163_0/
  n163_1 (fixed 1:1, no voice-stealing), channel 5 reserved for SFX,
  channels 6-8 = OPL2 hardware rhythm mode (BD/HH+SD/TOM+CYM).
- **Wide** (tracks 3, 5, 7): these never play concurrently with gameplay
  music or SFX, so channel 5 isn't needed for SFX headroom, and none of the
  three use the triangle channel at all -- channels 0-5 = sq1/sq2/n163_0..3
  (fixed 1:1, 6 sources for 6 channels), channels 6-8 still rhythm.

## Open technical question: why `.wav` renders sound different from real hardware

Two proven mechanisms so far, not mutually exclusive:

1. **Two different OPL2 implementations.** `.wav` renders always go through
   `emu8950` (software). RPPacMan's actual runtime (`USE_NATIVE_OPL2` in
   `src/opl.h`) writes straight to the real FPGA OPL2 core
   (`vendor/opl2_fpga` in the picocomputer/rp6502 repo). Proven divergent
   once already: waveform-2 phase-folding differs between the two, causing
   a real octave-up error on hardware/the CI emulator that no `.wav` render
   ever showed (fixed by reverting the triangle patch off waveform 2).
2. **Rapid envelope retriggering** is a second, distinct divergence
   candidate -- real OPL2 envelope-generator behavior under fast key-on/
   key-off cycling (confirmed via direct `$4008` tracing: Pac-Man CE's
   triangle driver gates its linear counter in 4-12ms bursts starting at
   ~64s in track 0) likely isn't reproduced faithfully by software
   synthesis. Addressed for track 0's triangle via gap-bridging
   (`_bridge_short_gaps`/`TRIANGLE_MIN_GATE_TICKS`) rather than fully
   characterizing the real hardware's envelope timing.

PacManCE_04's "wav good, bin bad" report doesn't match either known
mechanism yet (no waveform-2/3 patches in use anywhere now; DMC's
retrigger rate is slow, not staccato) -- still open, needs a real listen
once regenerated.

## Immediate next steps

1. Build the OPTIONS submenu shell (ii), starting with Sound Test since it
   forces every track to actually be reachable in-game (including 03/04/05/
   07's wide-profile tracks and the un-triggered 08-11/17 stingers, none of
   which are reachable in-game yet).
2. Extra mode (iii) -- blocked on new maps/assets, not audio.
3. Game-over -> Results -> Ranking screens (v-vii) -- needs a persistence
   story first (also blocks ii-a/ii-b), then the new title-state-machine
   states and the score-sampling/histogram renderer.
