// policy.h
#pragma once
#include "baseline.h"
#include "features.h"

typedef enum { MOTOR_OK = 0, MOTOR_WARN, MOTOR_ALARM } motor_state_t;

typedef struct {
  motor_state_t state;
  int raw_anomaly; // 1 si la evaluación cruda de ese ciclo fue warning o alarm
  float temp_z;
  float vib_z;
  float kurt_z;
  float crest_z;
  float score; // max(z) con signo (solo desviaciones por encima del baseline)
  const char *reason;
} motor_eval_t;

typedef struct {
  // Baselines
  ewma_baseline_t temp_base;
  ewma_baseline_t vib_base;   // Se usa vib_rms
  ewma_baseline_t kurt_base;  // curtosis de la magnitud de vibración
  ewma_baseline_t crest_base; // factor de cresta de la magnitud de vibración
  int warmup_samples;         // nº de reportes antes de confiar en zscore
  int seen;

  // Umbrales en z-score
  float warn_z;
  float alarm_z;

  // Umbrales absolutos de seguridad
  float temp_abs_warn;
  float temp_abs_alarm;
  float vib_abs_warn;
  float vib_abs_alarm;
  float kurt_abs_warn;
  float kurt_abs_alarm;
  float crest_abs_warn;
  float crest_abs_alarm;

  // Persistencia
  int persist_needed;
  int persist_count;
} policy_t;

void policy_init(policy_t *p);
motor_eval_t policy_evaluate(policy_t *p, float temp_c,
                             const vib_features_t *vib,
                             int allow_baseline_update);