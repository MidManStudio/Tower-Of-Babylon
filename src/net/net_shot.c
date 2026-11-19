// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/net_shots.md, section "net_shot.h and net_shot.c"
// ============================================================================
#include "net_shot.h"

uint32_t NetShot_Seed(uint8_t playerId, uint32_t tick, uint8_t shotIndex)
{
    uint32_t x = tick * 0x9E3779B1u ^ ((uint32_t)playerId + 1u) * 0x85EBCA6Bu ^ ((uint32_t)shotIndex + 1u) * 0xC2B2AE35u;
    x ^= x >> 15; x *= 0x2C1B3C6Du;
    x ^= x >> 12; x *= 0x297A2D39u;
    x ^= x >> 15;
    return x ? x : 0x9E3779B9u;
}
