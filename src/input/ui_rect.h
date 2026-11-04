// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/input.md, section "ui_rect.h"
// ============================================================================
#ifndef UI_RECT_H
#define UI_RECT_H

#include <stdbool.h>

// A rectangle and a circle in canvas pixels (the canvas is 800 x 450). Plain structs with no raylib in them, so the touch
// logic, the HUD layout and their tests need nothing but a C compiler. raylib's Rectangle converts with one line.
typedef struct { float x, y, w, h; } UiRect;
typedef struct { float x, y, r; } UiCircle;

static inline bool UiRect_Contains(UiRect r, float px, float py) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; }
static inline bool UiCircle_Contains(UiCircle c, float px, float py) { const float dx = px - c.x, dy = py - c.y; return dx * dx + dy * dy <= c.r * c.r; }
static inline bool UiRect_Overlaps(UiRect a, UiRect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
static inline UiRect UiCircle_Bounds(UiCircle c) { return (UiRect){ c.x - c.r, c.y - c.r, c.r * 2.0f, c.r * 2.0f }; }

#endif
