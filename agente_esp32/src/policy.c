// policy.c
#include "policy.h"

void policy_init(policy_t *p) {
  baseline_init(
      &p->temp_base,
      0.05f); // alpha: adapta “en minutos” si reportas cada pocos segundos
  baseline_init(&p->vib_base, 0.05f);
  baseline_init(&p->kurt_base, 0.05f);
  baseline_init(&p->crest_base, 0.05f);

  p->warmup_samples = 15; // ~15 reportes para baseline inicial
  p->seen = 0;

  p->warn_z = 1.5f;
  p->alarm_z = 2.5f;

  // Umbrales absolutos
  p->temp_abs_warn = 80.0f;
  p->temp_abs_alarm = 95.0f;

  p->vib_abs_warn = 0.35f;
  p->vib_abs_alarm = 0.50f;

  // Curtosis
  p->kurt_abs_warn = 5.0f;
  p->kurt_abs_alarm = 8.0f;

  // Crest factor típico
  p->crest_abs_warn = 6.0f;
  p->crest_abs_alarm = 9.0f;

  // Persistencia: se necesitan ciertos ciclos consecutivos de anomalías para
  // elevar una alarma
  p->persist_needed = 5;
  p->persist_count = 0;
}

motor_eval_t policy_evaluate(policy_t *p, float temp_c,
                             const vib_features_t *vib,
                             int allow_baseline_update) {
  motor_eval_t ev;
  ev.state = MOTOR_OK;
  ev.raw_anomaly = 0;
  ev.temp_z = 0.0f;
  ev.vib_z = 0.0f;
  ev.kurt_z = 0.0f;
  ev.crest_z = 0.0f;
  ev.score = 0.0f;
  ev.reason = "ok";

  p->seen++;

  // Warmup: durante los primeros reportes, solo se construye el baseline.
  if (allow_baseline_update) {
    baseline_update(&p->temp_base, temp_c);
    baseline_update(&p->vib_base, vib->rms);
    baseline_update(&p->kurt_base, vib->kurtosis);
    baseline_update(&p->crest_base, vib->crest);
  }

  if (p->seen <= p->warmup_samples) {
    ev.state = MOTOR_OK;
    ev.reason = "warmup_baseline";
    return ev;
  }

  // zscores (desviación respecto al baseline)
  float tz = baseline_zscore(&p->temp_base, temp_c);
  float vz = baseline_zscore(&p->vib_base, vib->rms);
  float kz = baseline_zscore(&p->kurt_base, vib->kurtosis);
  float cz = baseline_zscore(&p->crest_base, vib->crest);

  ev.temp_z = tz;
  ev.vib_z = vz;
  ev.kurt_z = kz;
  ev.crest_z = cz;

  float score = tz;
  if (vz > score)
    score = vz;
  if (kz > score)
    score = kz;
  if (cz > score)
    score = cz;
  ev.score = score;

  // Reglas de severidad
  int alarm = 0, warn = 0;

  if (tz >= p->alarm_z || vz >= p->alarm_z || kz >= p->alarm_z ||
      cz >= p->alarm_z)
    alarm = 1;
  else if (tz >= p->warn_z || vz >= p->warn_z || kz >= p->warn_z ||
           cz >= p->warn_z)
    warn = 1;

  if (temp_c >= p->temp_abs_alarm || vib->rms >= p->vib_abs_alarm ||
      vib->kurtosis >= p->kurt_abs_alarm || vib->crest >= p->crest_abs_alarm)
    alarm = 1;
  else if (temp_c >= p->temp_abs_warn || vib->rms >= p->vib_abs_warn ||
           vib->kurtosis >= p->kurt_abs_warn || vib->crest >= p->crest_abs_warn)
    warn = 1;

  motor_state_t raw_state;
  if (alarm) {
    raw_state = MOTOR_ALARM;
    ev.reason = (temp_c >= p->temp_abs_alarm)          ? "temp_abs_alarm"
                : (vib->rms >= p->vib_abs_alarm)       ? "vib_abs_alarm"
                : (vib->kurtosis >= p->kurt_abs_alarm) ? "kurt_abs_alarm"
                : (vib->crest >= p->crest_abs_alarm)   ? "crest_abs_alarm"
                                                       : "z_alarm";
  } else if (warn) {
    raw_state = MOTOR_WARN;
    ev.reason = (temp_c >= p->temp_abs_warn)          ? "temp_abs_warn"
                : (vib->rms >= p->vib_abs_warn)       ? "vib_abs_warn"
                : (vib->kurtosis >= p->kurt_abs_warn) ? "kurt_abs_warn"
                : (vib->crest >= p->crest_abs_warn)   ? "crest_abs_warn"
                                                      : "z_warn";
  } else {
    raw_state = MOTOR_OK;
    ev.reason = "ok";
  }

  ev.raw_anomaly = (raw_state != MOTOR_OK);

  if (raw_state == MOTOR_OK) {
    p->persist_count = 0;
    ev.state = MOTOR_OK;
  } else {
    p->persist_count++;
    if (p->persist_count < p->persist_needed) {
      ev.state = MOTOR_OK;
    } else {
      ev.state = raw_state;
    }
  }

  return ev;
}