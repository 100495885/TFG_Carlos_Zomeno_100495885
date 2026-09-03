#include "sensors.h"
#include <math.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sensors, LOG_LEVEL_INF);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Tipos internos de simulación

typedef enum {
  SIM_FAULT_NONE = 0,
  SIM_FAULT_IMBALANCE,
  SIM_FAULT_MISALIGNMENT,
  SIM_FAULT_BEARING_WEAR
} sim_fault_t;

typedef struct {
  float rpm;
  float load_0_1;
  sim_fault_t fault;
  float severity_0_1;
} sim_profile_t;

typedef struct {
  uint32_t rng;
  sim_profile_t profile;
  float t;
  float temp_base_c;
  float temp_trend_c;
} sim_state_t;

static sim_state_t g;

// RNG (xorshift32)

static uint32_t xorshift32(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static float rand_u01(void) { return (xorshift32(&g.rng) / 4294967296.0f); }

static float rand_uniform(float a, float b) { return a + (b - a) * rand_u01(); }

static float rand_gauss_approx(float mean, float std) {
  float s = 0.0f;
  for (int i = 0; i < 6; i++)
    s += rand_u01();
  s = (s - 3.0f);
  return mean + std * s;
}

// Componente de fallo (firma de vibración)

static float fault_component(float t, float f_rot_hz, sim_fault_t fault,
                             float sev) {
  float x = 0.0f;

  switch (fault) {
  case SIM_FAULT_IMBALANCE:
    x += (0.35f * sev) * sinf(2.0f * (float)M_PI * f_rot_hz * t);
    break;

  case SIM_FAULT_MISALIGNMENT:
    x += (0.20f * sev) * sinf(2.0f * (float)M_PI * (2.0f * f_rot_hz) * t);
    x += (0.08f * sev) * sinf(2.0f * (float)M_PI * (3.0f * f_rot_hz) * t);
    break;

  case SIM_FAULT_BEARING_WEAR: {
    float f_def = 5.0f * f_rot_hz;
    float phase = fmodf(f_def * t, 1.0f);
    float spike = (phase < 0.02f) ? (1.0f - (phase / 0.02f)) : 0.0f;
    x += (0.6f * sev) * spike;
    x += rand_gauss_approx(0.0f, 0.08f * sev);
    break;
  }

  default:
    break;
  }

  return x;
}

// API pública

static const char *fault_to_str(sim_fault_t fault) {
  switch (fault) {
  case SIM_FAULT_NONE:
    return "NONE";
  case SIM_FAULT_IMBALANCE:
    return "IMBALANCE";
  case SIM_FAULT_MISALIGNMENT:
    return "MISALIGNMENT";
  case SIM_FAULT_BEARING_WEAR:
    return "BEARING_WEAR";
  default:
    return "UNKNOWN";
  }
}

void sensors_init(void) {
  // Se usa k_uptime_get() como semilla para que cada arranque dé datos
  // distintos
  uint32_t seed = (uint32_t)(k_uptime_get() & 0xFFFFFFFF);
  g.rng = (seed == 0 ? 0xA5A5A5A5u : seed);

  // Perfil de la simulación para obtener datos concretos
  g.profile.rpm = 1500.0f;
  g.profile.load_0_1 = 0.65f;
  g.profile.fault = SIM_FAULT_NONE;
  g.profile.severity_0_1 = 0.0f;

  g.t = 0.0f;
  g.temp_base_c = 45.0f;
  g.temp_trend_c = 0.0f;

  LOG_INF("Sensores inicializados (simulacion: %s sev=%.1f)",
          fault_to_str(g.profile.fault), (double)g.profile.severity_0_1);
}

int sensors_read_temperature(float *temp_c) {
  if (!temp_c)
    return -1;

  float load = g.profile.load_0_1;
  float sev = g.profile.severity_0_1;

  g.temp_trend_c += rand_gauss_approx(0.0f, 0.002f);
  if (g.temp_trend_c < -5.0f)
    g.temp_trend_c = -5.0f;
  if (g.temp_trend_c > 8.0f)
    g.temp_trend_c = 8.0f;

  float fault_heat = 0.0f;
  if (g.profile.fault == SIM_FAULT_BEARING_WEAR)
    fault_heat = 12.0f * sev;
  if (g.profile.fault == SIM_FAULT_MISALIGNMENT)
    fault_heat = 6.0f * sev;
  if (g.profile.fault == SIM_FAULT_IMBALANCE)
    fault_heat = 3.0f * sev;

  *temp_c = g.temp_base_c + 20.0f * load + fault_heat + g.temp_trend_c +
            rand_gauss_approx(0.0f, 0.15f);

  return 0;
}

int sensors_read_vibration_window(float *ax, float *ay, float *az,
                                  size_t n_samples, float sample_rate_hz) {
  if (!ax || !ay || !az || n_samples == 0 || sample_rate_hz <= 0.0f)
    return -1;

  float dt = 1.0f / sample_rate_hz;
  float f_rot_hz = g.profile.rpm / 60.0f;
  float load = g.profile.load_0_1;
  float sev = g.profile.severity_0_1;
  float base_amp = 0.08f + 0.25f * load;

  float ax_scale = rand_uniform(0.90f, 1.10f);
  float ay_scale = rand_uniform(0.90f, 1.10f);
  float az_scale = rand_uniform(0.90f, 1.10f);

  for (size_t i = 0; i < n_samples; i++) {
    float t = g.t + (float)i * dt;

    float normal =
        base_amp * sinf(2.0f * (float)M_PI * f_rot_hz * t) +
        (0.25f * base_amp) * sinf(2.0f * (float)M_PI * (2.0f * f_rot_hz) * t) +
        0.02f * sinf(2.0f * (float)M_PI * 180.0f * t);

    float noise = rand_gauss_approx(0.0f, 0.02f + 0.01f * load);
    float fault = fault_component(t, f_rot_hz, g.profile.fault, sev);
    float sample = normal + noise + fault;

    ax[i] = ax_scale * sample + rand_gauss_approx(0.0f, 0.01f);
    ay[i] = ay_scale * (0.85f * sample) + rand_gauss_approx(0.0f, 0.01f);
    az[i] = az_scale * (0.60f * sample) + rand_gauss_approx(0.0f, 0.01f);
  }

  g.t += (float)n_samples * dt;
  return 0;
}

void sensors_get_context(float *rpm, float *load) {
  if (rpm)
    *rpm = g.profile.rpm;
  if (load)
    *load = g.profile.load_0_1;
}
