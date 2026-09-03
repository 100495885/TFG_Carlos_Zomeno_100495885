// baseline.h
#pragma once

typedef struct {
    int initialized;
    float alpha;    // 0.01..0.2 (más pequeño = más lento)
    float mean;
    float var;      // varianza EWMA
} ewma_baseline_t;

void baseline_init(ewma_baseline_t *b, float alpha);
void baseline_update(ewma_baseline_t *b, float x);
float baseline_std(const ewma_baseline_t *b);
float baseline_zscore(const ewma_baseline_t *b, float x);