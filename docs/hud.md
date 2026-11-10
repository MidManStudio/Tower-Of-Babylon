# hud

## Overview

Everything drawn over the 3D view that is not a screen effect lives in `src/hud/`: the touch controls, the STATS button and popup, the pickup prompt, the toast, and the diagnostics overlay. `main.c` fills one `HudView` a frame with plain values and calls `Hud_Draw`; the HUD asks the game for nothing and keeps no pointer into it.

**The stats are behind a button.** Tap STATS (top left) or press P: a popup shows the floor, health with its bar and numbers, stamina with its bar (TIRED when exhausted), the weapon with its rounds loaded and in reserve (or the pipe's swings left), the enemy count, the flashlight (on or off, seconds left), the FPS and the generator's numbers for the floor. It has an X in its corner (and the STATS button toggles it again, and P). It does not pause the game, and it stays clear of the lower part of the screen so the stick, fire pad and jump work while it is open; fingers on the popup itself do nothing else. Opening the inventory closes it.

The popup also holds the **DIAGNOSTICS switch**, which shows the overlay that used to be on all the time (depth bits and MSAA, the shader, light counts, 3D timings, the config status, the flashlight line, the sound line, the frame graph). It is off until switched on. Two of its lines are still taps: the shader line toggles the shader, the sound line plays the next sound in the bank.

`HUD_VITALS_ALWAYS_ON` in `hud.h` (0 by default) keeps a health bar and a stamina bar on screen as well, if you find you want them back. The weapon readout is not behind that switch: a gun's rounds matter mid-fight, so it is always on.

## Modules

### `hud_layout.h` and `hud_layout.c`
**What it does:** where the STATS button, the popup, its X, its diagnostics switch and the two tappable diagnostic rows sit, as plain `UiRect`s with no raylib. The drawing, the tap handling and `tests/input_headless.c` all read the same numbers.

### `hud.h` and `hud.c`
**What it does:** `Hud_Draw(view)`; the toast (`Hud_Toast`, shown for 1.8 s, fading); `Hud_HandleTap(tap, inventoryOpen)`, which uses a tap on the STATS button, the popup's X and switch, the popup body (it swallows taps on itself) and the diagnostic rows, and says whether it did; `Hud_BlockedRects`, the tap-only regions to give the input system (the STATS button, the popup while open, the diagnostic rows while on); open, close and toggle for the popup. Touch controls are drawn only when the touch scheme is in use (`docs/input.md`). `WeaponReadout` (static, in `hud.c`) is the always-on weapon line, bottom centre, right-aligned at x = 620 so it stays clear of the RELOAD, SLIDE and fire-pad circles: a gun shows loaded rounds large and the reserve after a slash (orange at a quarter of the magazine, red when empty, a blinking RELOAD or NO AMMO hint, a bar and RELOADING while it reloads); a pipe shows a condition bar (green, orange, red, FALLING APART blinking under a quarter); the hands show nothing. It is drawn on the keyboard scheme too, and hidden while the inventory is open or the player is dead. `HudView` carries `mag`, `magSize`, `reloading`, `reloadFrac`, `reloadShown`, `magEmpty`, `weaponWears`, `weaponCondition`, `durabilityLeft` (`ammo` is now the reserve).

### `hud_controls.c`
**What it does:** draws the touch controls from the touch state, laid out as in the owner's sketch (`input.md`, "The layout"; a placeholder until the HUD is customisable): the floating stick as an outer ring, an inner disc and a knob (a faint ring where it would appear when no finger is down), the sprint icon above it (two chevrons in a ring: white, a ring filling as the knob nears the rim, yellow while sprinting, orange with TIRED when the push is there but the player cannot), the fire pad as a ring with a button disc and a bullet (red while held; FIRE, SWING or PUNCH by weapon), and round buttons: JUMP, RELOAD (grey; maroon when the magazine is empty; amber with a ring filling round it, and not pressable, while a reload runs; drawn only when a reload can start or is running), the contextual USE (green, red when locked; its label is two lines, PICKUP over the item's name, DOOR over UP or DOWN, HOLD over UNLOCK at the stair clock's console), SWAP, SLIDE, and LIGHT with its charge as a ring round the button and the low-charge blink. A button's label is shrunk until it fits its disc.

### `hud_run.c`
**What it does:** the stair clock on the screen (`floor_run.md`), top middle: a bar while the console is being worked ("UNLOCKING THE STAIRS", orange when let go), then STAIRS UNLOCKED with the countdown as m:ss (red and blinking in the last ten seconds), then THE FLOOR IS COLLAPSING with a bar of how much floor is left and GET TO THE DOOR. `HudView` carries `stairPhase`, `consoleProgress`, `consoleHeld`, `stairCountdown`, `collapseLeft01`; the HUD asks the game for nothing.

### `hud_stats.c`
**What it does:** the popup. Reuses `ScreenFx_DrawHealthBar` and `ScreenFx_DrawStaminaBar`. Beside the word STATS it shows the chosen character's short name (`HudView.characterName`, NULL when none was chosen; `characters.md`), in the title line so the rows below did not have to move.

### `hud_debug.c`
**What it does:** the diagnostics overlay and its two tap rows (moved here from `main.c` with their state; the sound row's "next sound" is kept here). It reads the other systems' status directly (`GameConfig_Status`, `Audio_Status`, the lighting counts, the shader state) and the few values `main.c` owns (depth bits, MSAA, the 3D stage timings) from the view.

## Not verified

- The whole layout is a placeholder: the HUD is to become customisable (positions and sizes chosen by the player). The layout today is one table plus `hud_layout.c`; saved positions will replace the table.
- Not seen on a screen. The popup's size (330 x 270 of 800 x 450), the text sizes (11 to 20 px) and the button sizes were chosen from the numbers, not by eye on the phone. The layout test checks that everything is inside the screen, that nothing overlaps, and that the X and the buttons are big enough to hit; it cannot say whether the popup is comfortable to read.
- With the vitals off the screen, the player's only live health cues are the screen effects (the red flash, the low-health vignette) and the heartbeat sound. If that is too little, set `HUD_VITALS_ALWAYS_ON` to 1.

## Fixes and Problems

- None recorded yet.
