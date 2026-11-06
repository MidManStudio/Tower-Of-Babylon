# Characters and the selection screen (`src/characters/`)

## Overview

At launch, before the game starts, a screen shows the playable characters and the player picks one. It is the first piece of the character system and deliberately the smallest: **information and the choice only**. There are no abilities, no stats and nothing about a character changes how the game plays yet. What it does do is keep the chosen character as an id (`"steve"`), so the game can show who you are (beside STATS in the stats popup) and a server can later be told in the handshake (`design/02-characters.md`, `design/07-roadmap.md`).

The words come from `docs/design/02-characters.md`: each character's overview and backstory as the owner wrote them, and the kit (passive, activatable, special) in the wording of that document's roster table. Nothing here was invented: no stats are shown because none are decided, and no ability names were made up (the kit lines carry only the names the design document gives, such as "Chemical Warfare" and "Smash").

**Elelelelele.** The design document flags that her record includes a crime that would cause trouble with app stores and age ratings, and recommends keeping the specific crime out of any text the game or a store page shows. The text here says her file lists violent crimes and nothing more specific. It is the owner's story and the owner's call; if you want it different, change `characters.mdix` and re-bake.

**The screen shows at launch only.** Not after dying, not between floors. It is a state of `main.c`: while it runs, none of the game does (no steps, no monsters, no clock, no 3D).

## What the screen looks like

800 by 450. Title at the top; a row of cards, one per character (the short name and the role), the highlighted one framed in yellow; under it a box with the highlighted character's full name and role, the overview and backstory on the left, and PASSIVE, ACTIVATABLE and SPECIAL on the right; a green PLAY AS (name) button at the bottom. Tap a card to highlight it, tap the button to choose. On a keyboard: A / D or the arrow keys to move, Enter or Space to choose (a hint line names them while the keyboard is the scheme in use).

## Modules

### `characters.h` and `characters.c`
**What it does:** the roster, read from `assets/config/characters.mdix` (below). `Characters_Count`, `Characters_Get(i)`, `Characters_IndexOfId`. `Characters_LoadFrom(doc, err, cap)` installs a roster only when all of it is valid: 1 to `CHARACTER_MAX` (8) characters, every text present and not empty, plain printable ASCII, within its length (id 23, name 31, short name 15, role 31, overview 239, backstory 639, each kit line 119), ids made of lowercase letters, digits and `_`, and no id twice. Otherwise nothing changes (it reads into a scratch table first) and the message names the field (`characters[2].backstory: missing`). The choice: `Characters_Select(id)` (refuses an id that is not in the roster), `Characters_Selected` (the index, or -1), `Characters_SelectedId`, `Characters_SelectedCall`.

**Decisions:** the choice is kept as the **id**, not the index, so it still means the same character if the roster is reloaded in another order (the web build swaps the real file in once the DixScript wasm is up) or a character is added. If the chosen id leaves the roster, the index says none and the id is kept. A character is words only: the struct has no numbers, and a test pins its size so a number cannot be added without noticing. The lengths are limits the screen's room allows, not tidy powers of two.

### `characters_register.c`
**What it does:** one line: `Characters_Register()` hands the baked table, the path of the file in the bundled assets and `Characters_LoadFrom` to `GameConfig_RegisterExtra`, like `FloorGame_Register` does. Called in `main()` before `GameConfig_Init`. It is a file of its own so that the tests can link `characters.c` without the whole config layer.

### `character_select.h` and `character_select.c`
**What it does:** the screen's logic and layout, with no raylib in it. `CharSelect` is three fields (count, focus, done). `CharSelect_Move(dir)` wraps round both ways; `CharSelect_Tap(x, y)` highlights a card or, on the button, chooses; a tap anywhere else does nothing; `CharSelect_Press` is the confirm key; once chosen nothing moves. `CharSelect_Resync(count)` keeps the focus on a card that exists when the roster changes under the screen. The layout is functions returning `UiRect`s: cards (their width follows the count, so any roster up to 8 fits in a row), the box, the button, the two text columns. `CharSelect_Wrap` is a word wrap that takes the measuring function as a parameter (raylib's `MeasureText` in the game, a stand-in in the tests), breaks a word wider than the line by letters, and says when text was cut.

**Decisions:** the budgets of lines (`CS_OVERVIEW_LINES` 4, `CS_BACKSTORY_LINES` 12, `CS_KIT_LINES` 4 at 12 px text, 14 px lines) are sized so that the longest shipped texts fit even when every letter is measured 20 percent wider than raylib's default font draws it; the test checks that. Text longer than its lines is cut and ends in "..." on screen rather than running over the next block. Tapping a card does not choose: a thumb that lands wrong must not start a game as the wrong character, so choosing is a deliberate tap on the button.

### `character_select_ui.h` and `character_select_ui.c`
**What it does:** draws the screen (`CharSelectUI_Draw(state, showKeys)`), clearing the background itself. Plain `DrawText` and rectangles, like the HUD. No test can run it (it needs a window); it is compiled for web, Android and desktop by `tests/platform_check.sh`.

### `Input_Menu` (`src/input/input.h`)
**What it does:** what a menu takes from the player: a step left or right (A, D, the arrows), a confirm (Enter, Space), and whether anything touched the device this frame (the browser opens the sound card only after a gesture, and the selection tap is the first one). Keyboard reads are behind `TOWER_INPUT_KEYBOARD` like the rest of `input.c`; taps still come from `Input_NewTap`. On desktop the mouse is locked for play, so the desktop selection screen is keyboard only (no desktop build exists to run it).

### `main.c`: `SelectFrame`
**What it does:** `Frame()` starts with `if (g_selecting) { SelectFrame(); return; }`. `SelectFrame` polls the config (so the real `characters.mdix` can arrive), reads the menu, applies the tap, keeps the audio's gesture unlock going, draws the screen, and when the player chose it stores the id, resets the fixed-step clock and the command latch (the game begins from a clean clock) and starts the game fading in. With no roster (`Characters_Count() == 0`) there is no screen and the game starts at once. `hv.characterName` carries the chosen short name to the stats popup, drawn beside "STATS".

## `assets/config/characters.mdix`

A group array, `characters::`, of `character(id, name, call, role, overview, backstory, passive, activatable, special)` rows, in the order shown on the screen. All strings, plain ASCII, no quote marks inside them. It is baked by `tools/mdix_bake.mjs` like the others (the baker checks that every field is there and is text and that there are 1 to 8 rows; the C loader checks everything else, and `tests/characters_headless.c` runs the loader on the baked table). To add a character: add a row, `node tools/mdix_bake.mjs`, and the screen has one more card (up to eight).

The ids and short names (`steve`, `brou`, `khan`, `lele` / Steve, Brou, Khan, Lele) are the proposal in `design/02-characters.md`, not a decision; they are one word each in the file if you want others.

## Tests

### `tests/characters_headless.c` (62 checks)
The shipped file loads: four characters, in the design document's order with their full names, every field present, a character is words only. The loader refuses, names the field and keeps the roster in use **byte for byte** after: a missing field, an empty text, an overview or a kit line over its limit, text that is not plain ASCII, a repeated id, an id with capitals or a space, no characters, more than eight, a number where text belongs, no document; and takes a valid shorter roster. The choice: nothing at first, an unknown id refused, Lele chosen, and the id surviving a roster that lacks her and one that has her again. The screen: move and wrap both ways, one character, a tap on a card highlights without choosing, taps on nothing or in a corner, the button, nothing moves once chosen, the confirm key, no characters, the roster shrinking and growing. The layout for every roster size from 1 to 8 (cards on screen, apart, above the box), the box and button on screen and apart, the button thumb-sized, the columns inside the box and apart and tall enough for their lines. The word wrap (words, spaces, a long word, running out of lines, a width too small for a letter). And that every shipped overview, backstory and kit line fits its lines and every short name and role fits its card, measured pessimistically.

**Mutation tests (22).** Each rule was broken on purpose and the test had to fail: no wrap-around, a card tap that chooses, a tap that works after choosing, a resync that does not clamp, a confirm with nothing to choose, a hard break that makes no progress (a hang), overlapping cards, cards running into the box or off the screen, a button over the box or too small, overlapping columns, a loader that allows a repeated id, non-ASCII, an empty text, any id, zero characters, or installs while reading (a half roster after a refusal), a choice of an unknown id, a selected name that ignores the choice, and line budgets that are too small. One survived at first (a uniform shift of the cards creates no overlap: the mutation was wrong, not the test; a negative gap is the real one and is caught), and one check was strengthened (the "refusal changes nothing" check compares the whole roster, not two fields).

## Not verified

- **Nothing here has been seen on a screen.** The drawing (`character_select_ui.c`) is compiled, not run: whether the cards, the text sizes and the green button look right on the phone, whether 12 px text is comfortable to read at that size on a 6.6 inch screen, and whether the real font's widths leave the margin the test assumes, need a person with the device. If the text is too small, the room is the limit: the box is 784 by 286 canvas pixels.
- `SelectFrame` and `Input_Menu` cannot run without raylib. The logic they call is tested; the glue (the order of the calls, the fade-in, the clock reset) is not. A finger still down from the confirming tap becomes a stick or camera finger when the game starts, as any new touch would; not seen.
- The web build's real-file path (the DixScript wasm reading `characters.mdix`) is the same machinery as the other files and is covered by their tests, but a changed file has not been tried on this one.
- Audio after the selection: the screen calls `Audio_Update` with the gesture so the sound card opens on the first tap or key; nobody has heard whether it does on a phone.

## Fixes and Problems

- **A backstory did not fit.** The test that measures every shipped text with pessimistic letter widths found that Elelelelele's backstory needs 12 lines and the first budget was 11 (and no overview needs more than 4, where the budget was 5). The budgets were moved (overview 4, backstory 12; the column still holds exactly). The texts were not cut.
- **A test expected the wrong number.** The wrap test counted the spaces inside "one   two" as two; there are three. The test was wrong, not the code.
- The loader's length limits are above what the screen's lines hold at the extreme (a 239 letter overview of narrow letters fits four lines, one of wide letters might not): the screen cuts such a text with "..." and the shipped texts are checked. Tighten `CHARACTER_OVERVIEW_LEN` if a longer one is ever wanted to be refused instead.
