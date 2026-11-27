// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/gvex_session.md, section "Flecs in the repository"
// ============================================================================
#ifndef TOB_FLECS_H
#define TOB_FLECS_H

// Flecs as Tower of Babylon builds it: a custom build with only the parts the game uses. The SAME macros must be defined for flecs.c and for
// every file that includes flecs.h, which is why nothing includes flecs.h directly: include this file instead.
#define FLECS_CUSTOM_BUILD
#define FLECS_OS_API_IMPL
#define FLECS_LOG
#define FLECS_SYSTEM
#define FLECS_PIPELINE
#define FLECS_TIMER
#define FLECS_MODULE
#define FLECS_PARSER
#define FLECS_QUERY_DSL

#include "flecs.h"

#endif
