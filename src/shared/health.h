#ifndef HEALTH_H
#define HEALTH_H

#include <stdbool.h>

typedef struct {
    float hp;
    float maxHp;
    float invulnerabilityTimer;
    bool isAlive;
} HealthComponent;

static inline void InitHealth(HealthComponent *h, float maxHp) {
    h->maxHp = maxHp;
    h->hp = maxHp;
    h->invulnerabilityTimer = 0.0f;
    h->isAlive = true;
}

static inline void ApplyDamage(HealthComponent *h, float amount) {
    if (!h->isAlive || h->invulnerabilityTimer > 0.0f) return;
    
    h->hp -= amount;
    if (h->hp <= 0.0f) {
        h->hp = 0.0f;
        h->isAlive = false;
    }
}

static inline void UpdateHealthTimer(HealthComponent *h, float dt) {
    if (h->invulnerabilityTimer > 0.0f) {
        h->invulnerabilityTimer -= dt;
        if (h->invulnerabilityTimer < 0.0f) h->invulnerabilityTimer = 0.0f;
    }
}

#endif

