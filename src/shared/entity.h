#ifndef ENTITY_H
#define ENTITY_H

#include <stdint.h>

typedef enum {
    ENTITY_TYPE_PLAYER = 0,
    ENTITY_TYPE_RIVAL_INMATE,
    ENTITY_TYPE_ALIEN_HORROR,
    ENTITY_TYPE_SECURITY_DRONE
} EntityType;

typedef struct {
    uint32_t entityId;
    EntityType type;
    uint16_t squadId; // Used for rival inmate squads before the betrayal phase
    float aggroRadius;
} EntityDefinition;

#endif

