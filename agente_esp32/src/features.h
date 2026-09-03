// features.h
#pragma once
#include <stddef.h>

typedef struct {
    float mean;
    float rms;
    float peak;
    float crest;
    float kurtosis; // kurtosis (no exceso): ~3 en señal "gaussiana"
} vib_features_t;

// Calcula features básicas sobre un vector x[0..n-1]
int vib_compute_features(const float *x, size_t n, vib_features_t *out);