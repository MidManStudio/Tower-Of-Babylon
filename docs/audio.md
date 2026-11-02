# audio

## Overview

The game makes its own sound. There are no sound files: every sound is a **recipe** in `assets/config/sounds.mdix` (up to four layers: a tone or noise, a pitch glide, an envelope, a sweeping low-pass, a soft clip) that a small synthesiser (`src/audio/synth.c`) renders into samples when the game starts, a few sounds per frame. That keeps the download small, lets every number be tested, and means a sound can be changed by editing a line of text.

The rules of the game are about noise, and without sound the noise system is invisible. So the first set covers what carries meaning: your own footsteps (spaced by how fast you move, louder when you sprint), each gun's own shot, the shotgun's pump, melee, hit and kill ticks, getting hurt, the heartbeat when health is low, a monster's first growl and its skittering as it closes in, the flashlight click, pickups, doors, the ambience.

Design cues come from the Unity audio package (`MidManStudio_Unity/packages/com.midmanstudio.audio` and `rust_lib/mid_audio`): a **peak limiter on the final mix**, **categories** with their own volume and mute, sounds named by **id** from a data catalogue, a **pool of voices** per sound that steals the oldest, **pitch variation** and **never the same variant twice in a row**, and one-shots **at a position** (pan and distance). What that package does for Unity (an AudioSource pool, Unity's mixer, the native Rust bridge) is replaced here by raylib's sound functions and plain C that tests can run.

```
sounds.mdix --bake--> config_baked.c --\
                                        +--> SoundBank_LoadFrom --> SoundDef[SND_COUNT]
sounds.mdix --wasm (web)--------------/            |
                                                    v
                          synth.c renders each recipe (a few per frame) --> raylib Sound + aliases (or a Music loop)
                                                    |
        main.c --> audio_events.c --> audio.c (take, voice, pitch, pan, volume) --> raylib --> limiter --> speakers
```

## Hearing it

- **In the game:** tap the FPS counter to show the diagnostics. The green `sound:` line says `waiting for a tap` until you touch the screen (a browser keeps the sound card shut until then; on Android it opens at once), then `on`, how many sounds are rendered, how many are playing and the limiter's gain. **Tap that line to play the next sound in the bank** and its name is shown as `next:`. Loops (heartbeat, ambience) are skipped: the heartbeat starts by itself below 40% health and the ambience is always on.
- **On the phone, before building:** `tower_sound_previews.zip` holds each sound as a WAV (the first take of each), rendered by the same code (`AUDIO_DUMP=folder ./audio_headless`). Play them in any player.
- **Tuning a sound** is editing its lines in `assets/config/sounds.mdix`. On the web build with the wasm up, the file is read live (`config:` line says `from the .mdix files`) and the sounds are rendered again; otherwise run `node tools/mdix_bake.mjs`. The Android workflow bakes by itself.

## Adding a sound

1. Add its id to `SoundId` in `src/audio/sound_bank.h`, its name to `SOUND_NAMES` in `sound_bank.c`, and its name to `SOUND_IDS` in `tools/mdix_bake.mjs` (all three in the same order).
2. Write its `snd(...)` row and one `lyr(...)` row per layer in `sounds.mdix`, at the same place in the order. The column meanings are in the comment at the top of that file.
3. `node tools/mdix_bake.mjs`, then `bash tools/run_tests.sh`. The tests render every recipe and fail on silence, clipping, a missing layer or a wrong order.
4. Play it from `audio_events.c` (or `Audio_Play` and friends directly).

## Modules (all in `src/audio/`)

### `synth.h` and `synth.c`
**What it does:** `Synth_Render(recipe, seed, rate, out, cap)` fills 16 bit mono samples; `Synth_Frames` says how many. Per layer: sine, square, saw, triangle or noise (xorshift32, seeded); the frequency glides exponentially from `f0` to `f1`; the level rises over `attack` then falls by 1/e every `decay` seconds; a two-pole low-pass (12 dB per octave) sweeps from `lp0` to `lp1`; `drive` pushes the layer through `tanh` (grit); a layer can start after a `delay`. The layers are added; if the sum passes 0.98 the whole sound is scaled down to 0.98 (never up); a non-looping sound ends with a 4 ms fade so it stops without a click. A **loop** renders `loopFade` seconds past its end and cross-fades that tail into the start (linear, so a tone stays at full level), which makes the repeat seamless.

**Decisions:** 22050 Hz, mono, 16 bit: a gunshot is 25 KB, all 25 sounds with their takes are about 1 MB, and the sound card resamples. The seed makes a recipe repeatable (a test can compare samples) while variants of a sound use different seeds. No float buffer is kept after rendering. Nothing here touches raylib, so the tests run it without a sound card.

### `audio_core.h` and `audio_core.c`
**What it does:** the arithmetic of the audio system, all plain C. `Mix_Gain` (sound x category x master, mute). `Limiter_Process`: the peak of the buffer is measured first, so a loud buffer is brought under the threshold in that same buffer; the gain then recovers by `releasePerSec` (smoothed over 1.5 ms). `Voices_Acquire`: a free slot, else the one that started longest ago. `Spatial_Compute`: full volume to a near distance, falling off with the square of what is left to a far distance, and a pan from where the source is to the left or right of where the listener faces (raylib's right-handed axes: right is `(-fz, fx)`). `Footsteps_Advance`: steps by distance walked, feet alternating. `Sched_*`: a 16 item "play this in a moment" queue (the shotgun's pump).

**Decisions:** the limiter is the Unity package's idea made one step safer: that one reacts a buffer late and can let the first loud buffer through, this one looks at the buffer it is about to output. A drop in gain is immediate (any ramp would let a peak early in the buffer through); a recovery is smoothed. A sound within 2 m of the listener pans less, so passing close does not swing it from ear to ear. Voices are tracked by **time** (when each started and when it will end, known because the sounds are synthesised) instead of asking raylib whether a sound is still playing, so the pool logic is testable and costs no calls.

### `sound_bank.h` and `sound_bank.c`
**What it does:** `SoundId` (the order of the rows), `SoundDef` (category, volume, pitch jitter, voices, variants, loop, falloff, recipe) and `SoundBank_LoadFrom(doc)`, which reads `sounds.mdix` and checks every value (type and range, the ids in order, one to four layers each, a tone layer has a frequency, filter ends come as a pair, a loop has a duration and a fade). It builds into a spare table and installs only if everything is right, then raises `SoundBank_Revision` so the player knows to render again.

**Decisions:** `sounds.mdix` is one group array with two kinds of row, `kind = 0` a sound and `kind = 1` a layer of the sound above it, written with the QuickFuncs `snd(...)` and `lyr(...)`. That keeps a sound to a few lines instead of forty key-value pairs, and the baked table is the same flat paths as every other config (`sounds[3].wave`). The id in each sound row must match the C list in order, so a row can never be attached to the wrong sound.

### `audio.h` and `audio.c`
**What it does:** the raylib side. `Audio_Init` registers `sounds.mdix` with the config layer (`GameConfig_RegisterExtra`, see `config.md`). `Audio_Update` (every frame) opens the sound card (web: only after the first touch, mouse button or movement key; elsewhere at once), attaches the limiter to the final mix with `AttachAudioMixedProcessor`, renders the next sounds (at least one a frame, then while under 3 ms), runs the delay queue and fades the loops. Playing picks a variant that is not the one just played, a voice, a pitch (`pitch` times up to `pitch_jitter` either way), a volume (`Mix_Gain` and distance) and a pan, and calls raylib. A sound with `voices` copies has that many aliases (`LoadSoundAlias`, which share the samples) per variant. Loops are raylib `Music` streams over a WAV kept in memory (raylib has no loop flag on a `Sound`), faded in and out at 2 volume units a second.

**Decisions:** nothing blocks and nothing is allocated while playing: until a sound is rendered, playing it returns false and the game goes on. Rendering is spread over frames so the first seconds do not hitch (about 1 MB of samples). When a new `sounds.mdix` arrives (web live reload) everything is freed and rendered again.

### `audio_events.h` and `audio_events.c`
**What it does:** what the game does to make sound, in one place. `main.c` passes plain facts (speed, grounded, sliding, a weapon id, a health fraction, the nearest awake enemy) and never names a sound. Footsteps: 1.7 m apart walking, 2.3 m sprinting, feet alternate in pitch and a little in pan; a jump sounds on leaving the ground going up, a landing when falling faster than 3 m/s (harder falls are louder); sliding starts the skid; the heartbeat comes in below 40% health, louder and faster as it gets worse; death sounds once; the shotgun's pump follows its blast by 0.55 s; the first enemy to wake gets one `alert` at its place, then a `skitter` from it every 0.3 s (quicker as it nears) within 22 m.

**Decisions:** these numbers are the first guess, chosen by reasoning about the player's speeds (4.5 m/s walking, 7.6 sprinting), not by ear.

### `tests/audio_headless.c`
**What it covers (68 checks):** pitch (zero crossings of a fixed tone and of a 100 to 400 Hz glide against the maths), the envelope (1/e after the top), the low-pass, a layer's delay, no clipping and no scaling up, same seed same samples, refusing a too-small buffer, the seamless loop, the mixer, the limiter (a peak of 1.8 under 0.9 in the same buffer, recovery in about half a second), voices, pan and distance, footsteps, the delay queue, the baked table (sorted, hash equals the file), the bank loading, **every take of every shipped recipe** (full length, audible, not clipped, ends on zero), bad documents refused and nothing changed, and the config registry (register after load installs at once, the list is bounded).

**Run:** `AUDIO_DUMP=folder ./audio_headless` also writes the WAVs.

## Not verified

- **Nobody has listened.** The recipes were designed from how these sounds are made (a gunshot is a noise burst with a falling low-pass over a falling sine thump and a tail) and measured for level, length and clipping, but not heard. Expect to tune them; that is what the audition tap and `sounds.mdix` are for. Levels between sounds are the first thing to check.
- The sound card path (`InitAudioDevice`, the limiter attached to the real mix, aliases, `Music` from memory, panning) has only been compiled against the raylib 6.0 and 6.1-dev headers and linked-for against the web `libraylib.a` (every function used is in it), never run. The browser's rule about opening audio after a touch is handled but unseen.
- raylib calls the mixed-audio processor with the device's rate; the limiter assumes 44100 Hz (raylib's default). A device at another rate changes only how long the recovery takes.
- No occlusion: a monster behind a wall sounds as loud as in the open. The roadmap's "sound through walls at a reduced radius" belongs with the monsters' hearing.

## Fixes and Problems

- None recorded yet.
