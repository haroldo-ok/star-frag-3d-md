# Star Frag 3D — Mega Drive edition

A port of [Star Frag 3D](https://github.com/haroldo-ok/star-frag-3d-sms) (Sega Master System) to the
Sega Mega Drive / Genesis, written in C with SGDK 2.12. It's a two-player split-screen deathmatch: you
can play against a friend, or play alone against the CPU.

`out/rom.bin` is a 2 MB ROM with no mapper and no SRAM. It has only been tested in Genesis Plus GX
(the libretro core, run headless). It has not been tested on real hardware or in other emulators.

## Controls (3- or 6-button pad; player 1 on port 1, player 2 on port 2)

| button | action |
|---|---|
| Up / Down | walk forward / back |
| Left / Right | turn |
| A + Left / Right | strafe |
| B | fire (hold for auto-fire); after being fragged, respawn |
| C (or X / Y / Z) | next weapon |
| Start | pause |

**Match setup:** Up/Down picks a line, Left/Right changes it, and Start begins the match. Touching
Left/Right on pad 2 makes player 2 a human and changes player 2's skin.

## What changed from the Master System version

- **Graphics**
  - 2-px columns instead of 4-px.
  - Each view is 152 × 128 instead of 128 × 96.
  - The wall textures come from Brutal Doom, with a 15-colour palette for each arena and lit and
    dark sides.
  - The sky and floor use dithered gradients.
  - Each player skin has its own 15-colour palette, at 13 pre-scaled sizes up to 88 px, with walk,
    attack, pain and a 4-frame death.
  - Explosions gib the player with Brutal Doom blood.
  - The status bar uses big Doom-style numbers.
  - The title screen has a metal logo with an animated sheen.
- **Weapons from Brutal Doom:** pistol, shotgun, super shotgun, assault rifle, minigun, grenade
  launcher, rocket launcher, plasma rifle, flamethrower, railgun and BFG 9000. They use Brutal Doom's
  first-person sprites, pickup sprites and firing sounds.
- **Sound:** SGDK's XGM2 driver.
  - The music is FM (5 channels) plus PSG (3 tones and noise hi-hats) plus DAC-sampled drums (kick,
    snare, tom and crash).
  - The tracks are the three Star Frag themes, re-orchestrated, and four Brutal Doom deathmatch MIDIs
    (D_DM05, 06, 07 and 10).
  - The sound effects are 25 digitized Brutal Doom sounds at 13.3 kHz, with one PCM channel for
    each player.
- **Arenas:** there are 10 instead of 4. The four originals are Outpost Alpha, Reactor Core, Cargo
  Bay and Force Dome. The six new ones are Toxic Refinery, Hell Gate, Bunker 9, Rust Yard, Jungle
  Post and White Lab.
- **Skins:** there are 12 instead of 6. The 6 originals are joined by Slayer, Cowboy, Snake, Santa,
  Frost and Punk, all from the 150 Doomguy Skins pack, each with its own HUD face.

## How it works (short)

- **Two ray-cast views, mirrored walls.** Wall textures are symmetric about the horizon, so only the
  top 64 rows of each view are drawn. Plane A shows the bottom half with the same tiles V-flipped.
  - The views are drawn into a column-major RAM tile buffer by compiled 68000 scalers
    (`src/gen/scalers.s`).
  - Odd rays are interpolated where both neighbours hit the same wall face.
- **Double buffering.** There are two RAM buffers and two VRAM banks, with one plane-A name table
  for each bank.
  - Only the changed tile columns are uploaded.
  - All VDP traffic goes through a job ring that is DMAed in VBlank within a byte budget
    (`src/vdpq.c`).
  - A FLIP job switches plane A and uploads the matching sprite table, so a frame is shown only after
    all of its tiles have arrived.
- **Pre-scaled hardware sprites.** Each graphic is stored as 8-px tile strips at 13 sizes.
  - A strip is hidden when the wall at its centre ray is nearer.
  - Tiles come from a VRAM cache shared by both views.
  - The HUD and divider sit on high-priority plane B, so sprites never leak into the other view.
- **Game time is independent of frame rate,** as on the Master System.

Measured frame rate in the headless emulator (NTSC):

| situation | frames per second |
|---|---|
| player 1 standing still, CPU moving | 19 |
| player 1 turning | 11 |
| everything moving and firing | 10 |

## Building

You need SGDK 2.12 (set `GDK`), an m68k GCC, Java, python3 with Pillow and mido, and ffmpeg. The
generated data is already in `src/gen/` and `res/`. To regenerate it from the source assets:

    BDPK3=/path/to/extracted/brutalv21.pk3 SKINWAD=/path/to/150skins.wad python3 -I tools/gen.py
    make

Tests (Genesis Plus GX libretro, headless): `bash tests/run_headless_emulator_test.sh` (boot,
playthrough and frame checks), `bash tests/fps.sh` (frame rate).

## Credits

- **Original game:** Star Frag 3D for the Master System by haroldo-ok, built on the Splat Trooper 3D
  engine design.
- **Weapons, wall textures, item sprites, sound effects and four music tracks:** Brutal Doom v21 by
  Sergeant_Mark_IV and contributors.
- **Player skins and HUD faces:** the 150 Doomguy Skins pack compiled by Doomkid. The pack credits
  the individual skin authors.
- **Doom palette:** Freedoom (BSD licence, `tools/doompal.COPYING`).
- **Logo font:** Poppins Bold (SIL OFL).
- **Toolchain:** SGDK by Stephane Dallongeville.
