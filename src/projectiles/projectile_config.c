// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/projectiles.md, section "projectile_config.h and projectile_config.c"
// ============================================================================
#include "projectile_config.h"

#include <string.h>

static ProjectileConfig g_configs[MAX_PROJECTILE_CONFIGS];
static int g_count = 0;

void ProjectileConfig_SetAll(const ProjectileConfig *configs, int count)
{
    if (count < 0) count = 0;
    if (count > MAX_PROJECTILE_CONFIGS) count = MAX_PROJECTILE_CONFIGS;
    for (int i = 0; i < count; ++i) g_configs[i] = configs[i];
    g_count = count;
}

int ProjectileConfig_Count(void) { return g_count; }

const ProjectileConfig *ProjectileConfig_Get(int index) { return (index >= 0 && index < g_count) ? &g_configs[index] : NULL; }

int ProjectileConfig_Find(const char *id)
{
    for (int i = 0; i < g_count; ++i) if (strcmp(g_configs[i].id, id) == 0) return i;
    return -1;
}
