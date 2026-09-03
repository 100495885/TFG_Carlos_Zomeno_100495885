// features.c
#include "features.h"
#include <math.h>

int vib_compute_features(const float *x, size_t n, vib_features_t *out) {
    if (!x || !out || n == 0) return -1;

    // 1) media
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) sum += (double)x[i];
    double mean = sum / (double)n;

    // 2) RMS y peak
    double sum2 = 0.0;
    float peak = 0.0f;
    for (size_t i = 0; i < n; i++) {
        double v = (double)x[i];
        sum2 += v * v;
        float a = fabsf(x[i]);
        if (a > peak) peak = a;
    }
    double rms = sqrt(sum2 / (double)n);

    // 3) kurtosis = E[(x-mean)^4] / (E[(x-mean)^2]^2)
    // (Esto sube mucho con impactos tipo rodamiento)
    double m2 = 0.0;
    double m4 = 0.0;
    for (size_t i = 0; i < n; i++) {
        double d = (double)x[i] - mean;
        double d2 = d * d;
        m2 += d2;
        m4 += d2 * d2;
    }
    m2 /= (double)n;
    m4 /= (double)n;

    double kurt = 0.0;
    if (m2 > 1e-12) kurt = m4 / (m2 * m2);

    // crest factor = peak / rms
    float crest = 0.0f;
    if (rms > 1e-9) crest = (float)((double)peak / rms);

    out->mean = (float)mean;
    out->rms = (float)rms;
    out->peak = peak;
    out->crest = crest;
    out->kurtosis = (float)kurt;
    return 0;
}