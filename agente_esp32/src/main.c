// main.c — agent.c adaptado para Zephyr OS en ESP32-S3
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "features.h"
#include "llm_client.h"
#include "policy.h"
#include "report.h"
#include "sensors.h"

LOG_MODULE_REGISTER(agente, LOG_LEVEL_INF);

#define DEVICE_ID "motor-01"
#define ALARM_FOLDER "/lfs/ALARM"
#define FS_HZ 2000.0f
#define N_SAMPLES 512

static uint32_t alarm_counter = 0;

static float ax[N_SAMPLES];
static float ay[N_SAMPLES];
static float az[N_SAMPLES];
static float mag[N_SAMPLES];

// WiFi
// Semáforo que el callback sube cuando llega el evento de conexión.
// main() bloquea en k_sem_take() hasta entonces (máx. 30 s).

static K_SEM_DEFINE(wifi_connected, 0, 1);
static K_SEM_DEFINE(ip_obtained, 0, 1);
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback dhcp_cb;

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface) {
  if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
    const struct wifi_status *st = (const struct wifi_status *)cb->info;
    int conn_status = st ? (int)st->conn_status : -1;
    LOG_INF("CONNECT_RESULT status=%d", conn_status);
    if (conn_status == WIFI_STATUS_CONN_SUCCESS) {
      k_sem_give(&wifi_connected);
    } else {
      LOG_ERR("WiFi fallo (status=%d) — reintentando en 5s", conn_status);
    }
  }
}

static void dhcp_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface) {
  if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND) {
    struct in_addr *ip = &iface->config.dhcpv4.requested_ip;
    LOG_INF("DHCP BOUND: %d.%d.%d.%d", ip->s4_addr[0], ip->s4_addr[1],
            ip->s4_addr[2], ip->s4_addr[3]);
    k_sem_give(&ip_obtained);
  }
}

static void wifi_connect(void) {
  struct net_if *iface = net_if_get_default();

  net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
                               NET_EVENT_WIFI_CONNECT_RESULT);
  net_mgmt_add_event_callback(&wifi_cb);

  net_mgmt_init_event_callback(&dhcp_cb, dhcp_event_handler,
                               NET_EVENT_IPV4_DHCP_BOUND |
                                   NET_EVENT_IPV4_DHCP_STOP);
  net_mgmt_add_event_callback(&dhcp_cb);

  struct wifi_connect_req_params params = {
      .ssid = CONFIG_WIFI_SSID,
      .ssid_length = strlen(CONFIG_WIFI_SSID),
      .psk = CONFIG_WIFI_PASSWORD,
      .psk_length = strlen(CONFIG_WIFI_PASSWORD),
      .security = WIFI_SECURITY_TYPE_PSK,
      .channel = WIFI_CHANNEL_ANY,
  };

  net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));

  if (k_sem_take(&wifi_connected, K_SECONDS(30)) != 0) {
    LOG_ERR("No se pudo conectar al WiFi");
    return;
  }
  net_dhcpv4_stop(iface);
  net_dhcpv4_start(iface);

  if (k_sem_take(&ip_obtained, K_SECONDS(30)) != 0) {
    LOG_ERR("DHCP timeout — sin IP (dhcp_state=%d)",
            (int)iface->config.dhcpv4.state);
  }
}

// LittleFS

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t mp = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .mnt_point = "/lfs",
};

static void mount_lfs(void) {
  if (fs_mount(&mp) != 0) {
    LOG_ERR("No se pudo montar LittleFS");
    return;
  }
  LOG_INF("LittleFS montado en /lfs");
  fs_mkdir(ALARM_FOLDER);
}

// Helpers

static void compute_magnitude(const float *ax, const float *ay, const float *az,
                              float *mag, size_t n) {
  for (size_t i = 0; i < n; i++) {
    float x = ax[i], y = ay[i], z = az[i];
    mag[i] = sqrtf(x * x + y * y + z * z);
  }
}

// Función encargada de guardar las alarmas en LittleFS
static int save_alarm_zephyr(const char *report_json, const char *llm_json) {
  if (!report_json || !llm_json)
    return -1;

  cJSON *report_obj = cJSON_Parse(report_json);
  cJSON *llm_obj = cJSON_Parse(llm_json);

  if (!report_obj || !llm_obj) {
    if (report_obj)
      cJSON_Delete(report_obj);
    if (llm_obj)
      cJSON_Delete(llm_obj);
    return -1;
  }

  cJSON *root = cJSON_CreateObject();

  int64_t now = k_uptime_get();
  char alarm_id[64];
  snprintf(alarm_id, sizeof(alarm_id), "ALARM_%lld_%u", now, alarm_counter);
  alarm_counter++;

  cJSON_AddStringToObject(root, "id", alarm_id);
  cJSON_AddNumberToObject(root, "timestamp", (double)(now / 1000));
  cJSON_AddStringToObject(root, "type", "ALARM_TRIGGER");
  cJSON_AddItemToObject(root, "report", report_obj);
  cJSON_AddItemToObject(root, "llm", llm_obj);

  char *json_str = cJSON_Print(root);
  cJSON_Delete(root);
  if (!json_str)
    return -2;

  char filepath[128];
  snprintf(filepath, sizeof(filepath), "%s/%s.json", ALARM_FOLDER, alarm_id);

  struct fs_file_t f;
  fs_file_t_init(&f);
  if (fs_open(&f, filepath, FS_O_CREATE | FS_O_WRITE) != 0) {
    free(json_str);
    return -3;
  }

  fs_write(&f, json_str, strlen(json_str));
  fs_close(&f);
  free(json_str);
  return 0;
}

// main

int main(void) {
  // 1. Sistema de ficheros
  mount_lfs();

  // 2. WiFi — el agente necesita red para llamar a Gemini
  wifi_connect();

  // 3. Inicializar la simulación de los sensores
  sensors_init();

  // 4. Politica
  policy_t policy;
  policy_init(&policy);

  // 6. Config LLM desde Kconfig
  llm_config_t llm_cfg = {.gemini_url =
                              "https://generativelanguage.googleapis.com/"
                              "v1beta/models/%s:generateContent",
                          .gemini_model = "gemini-2.5-flash",
                          .gemini_api_key = CONFIG_GEMINI_API_KEY,
                          .timeout_ms = 90000};

  motor_state_t prev_state = MOTOR_OK;
  int prev_raw_anomaly = 0;

  while (1) {
    // a) Leer sensores
    float temp_c = 0.0f;
    sensors_read_temperature(&temp_c);
    sensors_read_vibration_window(ax, ay, az, N_SAMPLES, FS_HZ);

    // b) Features vibración
    compute_magnitude(ax, ay, az, mag, N_SAMPLES);
    vib_features_t fmag;
    vib_compute_features(mag, N_SAMPLES, &fmag);

    // c) Evaluar estado
    int allow_update = (policy.seen < policy.warmup_samples) ||
                       (prev_state == MOTOR_OK && !prev_raw_anomaly);
    motor_eval_t ev = policy_evaluate(&policy, temp_c, &fmag, allow_update);

    // d) Reporte JSON, usa k_uptime_get()/1000 como timestamp en segundos
    float rpm = 0.0f, load = 0.0f;
    sensors_get_context(&rpm, &load);

    char report_json[2048];
    long ts = (long)(k_uptime_get() / 1000);
    int ok = build_report_json(report_json, sizeof(report_json), DEVICE_ID, ts,
                               temp_c, &fmag, &ev, rpm, load);

    if (ok == 0) {
      LOG_INF("REPORT: %s", report_json);
    }

    // e) Flanco ALARM genera diagnóstico LLM
    int alarm_rising = (ev.state == MOTOR_ALARM) && (prev_state != MOTOR_ALARM);

    if (alarm_rising && ok == 0) {
      char *llm_json = NULL;
      int rc = llm_analyze_report(&llm_cfg, report_json, &llm_json);

      if (rc == 0 && llm_json) {
        int wr = save_alarm_zephyr(report_json, llm_json);
        if (wr != 0) {
          LOG_WRN("No pude guardar alarma (code=%d)", wr);
        } else {
          LOG_INF("ALARM -> diagnostico guardado en %s", ALARM_FOLDER);
        }
        free(llm_json);
      } else {
        const char *err_desc = "desconocido";
        if (rc == -20)
          err_desc = "GEMINI_API_KEY no configurada";
        else if (rc == -24)
          err_desc = "Error HTTP";
        else if (rc == -25)
          err_desc = "API key invalida";
        else if (rc == -26)
          err_desc = "Respuesta sin contenido";
        else if (rc == -27)
          err_desc = "JSON invalido";
        LOG_WRN("ALARM -> fallo Gemini (rc=%d: %s)", rc, err_desc);
        if (llm_json)
          free(llm_json);
      }
    }

    prev_state = ev.state;
    prev_raw_anomaly = ev.raw_anomaly;
    k_msleep(500); // 0.5 s entre reportes
  }

  return 0;
}
