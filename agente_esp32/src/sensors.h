#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa el módulo de sensores.

void sensors_init(void);

int sensors_read_temperature(float *temp_c);

int sensors_read_vibration_window(float *ax, float *ay, float *az,
                                  size_t n_samples, float sample_rate_hz);

void sensors_get_context(float *rpm, float *load);

#ifdef __cplusplus
}
#endif
