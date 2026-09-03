// report.c
#include "report.h"
#include <stdio.h>

const char *motor_state_to_str(motor_state_t s) {
  switch (s) {
  case MOTOR_OK:
    return "OK";
  case MOTOR_WARN:
    return "WARN";
  case MOTOR_ALARM:
    return "ALARM";
  default:
    return "UNKNOWN";
  }
}

int build_report_json(char *out_buf, size_t out_sz, const char *device_id,
                      long ts, float temp_c, const vib_features_t *vib_mag,
                      const motor_eval_t *ev, float rpm, float load) {
  if (!out_buf || out_sz == 0 || !device_id || !vib_mag || !ev)
    return -1;

  int n = snprintf(out_buf, out_sz,
                   "{"
                   "\"device_id\":\"%s\","
                   "\"ts\":%ld,"
                   "\"context\":{"
                   "\"rpm\":%.1f,"
                   "\"load\":%.3f"
                   "},"
                   "\"sensors\":{"
                   "\"temp_c\":%.2f"
                   "},"
                   "\"vibration\":{"
                   "\"mag\":{"
                   "\"rms\":%.6f,"
                   "\"peak\":%.6f,"
                   "\"crest\":%.4f,"
                   "\"kurtosis\":%.4f"
                   "}"
                   "},"
                   "\"health\":{"
                   "\"state\":\"%s\","
                   "\"temp_z\":%.3f,"
                   "\"vib_z\":%.3f,"
                   "\"kurt_z\":%.3f,"
                   "\"crest_z\":%.3f,"
                   "\"score\":%.3f,"
                   "\"reason\":\"%s\""
                   "}"
                   "}",
                   device_id, ts, (double)rpm, (double)load, (double)temp_c,
                   (double)vib_mag->rms, (double)vib_mag->peak,
                   (double)vib_mag->crest, (double)vib_mag->kurtosis,
                   motor_state_to_str(ev->state), (double)ev->temp_z,
                   (double)ev->vib_z, (double)ev->kurt_z, (double)ev->crest_z,
                   (double)ev->score, ev->reason);

  return (n < 0 || (size_t)n >= out_sz) ? -2 : 0;
}