// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/characters.md, section "characters.h and characters.c"
// ============================================================================
#ifndef CHARACTERS_H
#define CHARACTERS_H

#include <stdbool.h>

#include "../config/mdix_doc.h"

// The roster of playable characters: who they are and what their kit does, as words, read from assets/config/characters.mdix.
// There are no numbers in a character yet (no health, speed or cooldowns: none are decided), and the abilities are not built; this
// is what the selection screen shows and what the chosen id will be sent to a server as.
#define CHARACTER_MAX        8
#define CHARACTER_ID_LEN     24
#define CHARACTER_NAME_LEN   32
#define CHARACTER_CALL_LEN   16
#define CHARACTER_ROLE_LEN   32
#define CHARACTER_OVERVIEW_LEN  240
#define CHARACTER_BACKSTORY_LEN 640
#define CHARACTER_KIT_LEN    120

typedef struct {
    char id[CHARACTER_ID_LEN];                // "steve": lowercase letters, digits and _, unique
    char name[CHARACTER_NAME_LEN];            // "Sinless Steve"
    char call[CHARACTER_CALL_LEN];            // "Steve": the short name for cards and voice chat
    char role[CHARACTER_ROLE_LEN];            // "Stealth saboteur"
    char overview[CHARACTER_OVERVIEW_LEN];
    char backstory[CHARACTER_BACKSTORY_LEN];
    char passive[CHARACTER_KIT_LEN];
    char activatable[CHARACTER_KIT_LEN];
    char special[CHARACTER_KIT_LEN];
} CharacterDef;

// How many characters there are (0 until a document has been read), and one of them. Characters_Get is NULL for an index out of range.
int                 Characters_Count(void);
const CharacterDef *Characters_Get(int index);
int                 Characters_IndexOfId(const char *id);       // -1 when there is no such character

// Reads the document. Installs it only when every character is complete and valid (1 to CHARACTER_MAX of them, unique ids, every text
// present, plain ASCII, within its length); otherwise nothing changes and err says what is wrong and where.
bool Characters_LoadFrom(const MdixDoc *doc, char *err, int errCap);

// Hands characters.mdix to the config layer (the baked copy at once, the real file when the wasm is up). Before GameConfig_Init.
void Characters_Register(void);

// The player's choice, kept as the id so it still means the same character if the roster is reloaded in another order. -1 / NULL / ""
// when nothing is chosen. Characters_Select refuses an id that is not in the roster.
bool        Characters_Select(const char *id);
int         Characters_Selected(void);
const char *Characters_SelectedId(void);
const char *Characters_SelectedCall(void);    // NULL when nothing is chosen

#endif
