// report.h
#pragma once
#include "features.h"
#include "policy.h"
#include <stddef.h>

const char* motor_state_to_str(motor_state_t s);

// Construye un JSON en out_buf (sin librerías externas).
// Devuelve 0 si OK.
int build_report_json(char *out_buf, size_t out_sz,
                      const char *device_id,
                      long ts,
                      float temp_c,
                      const vib_features_t *vib_mag,
                      const motor_eval_t *ev,
                      float rpm,
                      float load);