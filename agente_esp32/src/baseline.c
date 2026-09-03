// baseline.c
#include "baseline.h"
#include <math.h>

void baseline_init(ewma_baseline_t *b, float alpha) {
    b->initialized = 0;
    b->alpha = alpha;
    b->mean = 0.0f;
    b->var = 0.0f;
}

void baseline_update(ewma_baseline_t *b, float x) {
    if (!b->initialized) {
        b->initialized = 1;
        b->mean = x;
        b->var = 1e-6f; // pequeña para evitar std=0
        return;
    }

    // EWMA de media
    float prev_mean = b->mean;
    b->mean = (1.0f - b->alpha) * b->mean + b->alpha * x;

    // EWMA de varianza (aprox): var <- (1-a)*var + a*(x-prev_mean)^2
    float d = x - prev_mean;
    b->var = (1.0f - b->alpha) * b->var + b->alpha * (d * d);

    if (b->var < 1e-8f) b->var = 1e-8f;
}

float baseline_std(const ewma_baseline_t *b) {
    return (b->initialized) ? sqrtf(b->var) : 0.0f;
}

float baseline_zscore(const ewma_baseline_t *b, float x) {
    if (!b->initialized) return 0.0f;
    float sd = baseline_std(b);
    if (sd < 1e-6f) return 0.0f;
    return (x - b->mean) / sd;
}