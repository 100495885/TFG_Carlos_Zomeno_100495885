// llm_client.c — Cliente Gemini para Zephyr OS
// Sustituye libcurl por zsock (TLS) + http_client de Zephyr.
// La lógica cJSON (schema, prompt, parseo) es idéntica a la versión PC.
#include "llm_client.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/logging/log.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cJSON.h"

LOG_MODULE_REGISTER(llm_client, LOG_LEVEL_INF);

#define GEMINI_HOST "generativelanguage.googleapis.com"
#define GEMINI_PORT 443

// Buffers estáticos para no fragmentar el heap en cada llamada al LLM
static uint8_t http_recv_buf[4096];
static char    g_response_body[8192];
static size_t  g_response_len = 0;
static char    g_body[6144];

// ---------------------------------------------------------------------------
// Callback HTTP — sustituye write_cb de curl
// http_client llama a esta función cada vez que llega un fragmento del body.
// Lo concatenamos en g_response_body para procesarlo entero al final.
// ---------------------------------------------------------------------------

static int response_cb(struct http_response *rsp,
                       enum http_final_call final_data,
                       void *user_data)
{
    (void)user_data;
    (void)final_data;
    if (rsp->body_frag_len > 0) {
        size_t copy = MIN(rsp->body_frag_len,
                          sizeof(g_response_body) - g_response_len - 1);
        memcpy(g_response_body + g_response_len, rsp->body_frag_start, copy);
        g_response_len += copy;
        g_response_body[g_response_len] = '\0';
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Limpieza de markdown wrapping — sin cambios respecto a la versión PC
// ---------------------------------------------------------------------------

static char *strip_markdown_json(const char *text)
{
    if (!text) return NULL;

    const char *p = text;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    if (strncmp(p, "```json", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "```", 3) == 0) {
        p += 3;
    } else {
        char *_r = malloc(strlen(text) + 1);
        if (_r) { strcpy(_r, text); }
        return _r;
    }

    while (*p == '\n' || *p == '\r') p++;

    const char *end = strstr(p, "```");
    size_t len;
    if (end) {
        const char *e = end;
        while (e > p && (*(e-1) == ' ' || *(e-1) == '\t' ||
                          *(e-1) == '\n' || *(e-1) == '\r')) e--;
        len = (size_t)(e - p);
    } else {
        len = strlen(p);
    }

    char *result = (char*)malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, p, len);
    result[len] = '\0';
    return result;
}

// ---------------------------------------------------------------------------
// Socket TLS — sustituye curl_easy_init + CURLOPT_URL
// IPPROTO_TLS_1_2 le indica a Zephyr que use mbedTLS automáticamente.
// TLS_PEER_VERIFY_NONE omite la validación del certificado (válido para TFG).
// ---------------------------------------------------------------------------

static int open_tls_socket(void)
{
    int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
    if (sock < 0) {
        LOG_ERR("No se pudo crear socket TLS");
        return -1;
    }

    int verify = TLS_PEER_VERIFY_NONE;
    zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
    zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, GEMINI_HOST, strlen(GEMINI_HOST));

    struct zsock_addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct zsock_addrinfo *res = NULL;

    if (zsock_getaddrinfo(GEMINI_HOST, "443", &hints, &res) != 0 || !res) {
        LOG_ERR("DNS fallo para %s", GEMINI_HOST);
        zsock_close(sock);
        return -1;
    }

    if (zsock_connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        LOG_ERR("Conexion TLS fallida (errno=%d)", errno);
        zsock_freeaddrinfo(res);
        zsock_close(sock);
        return -1;
    }

    zsock_freeaddrinfo(res);
    return sock;
}

// ---------------------------------------------------------------------------
// Llamada a Gemini
// ---------------------------------------------------------------------------

static int gemini_call(const llm_config_t *cfg,
                       const char *report_json,
                       char **out_llm_json)
{
    const char *api_key = cfg->gemini_api_key;
    if (!api_key || strlen(api_key) == 0) {
        LOG_ERR("GEMINI_API_KEY no configurada");
        return -20;
    }
    if (!cfg->gemini_model || !cfg->gemini_url) {
        LOG_ERR("gemini_model o gemini_url no configurados");
        return -20;
    }

    // URL path — la API key va como query param (igual que en la versión PC)
    char url_path[512];
    snprintf(url_path, sizeof(url_path),
             "/v1beta/models/%s:generateContent?key=%s",
             cfg->gemini_model, api_key);

    const char *system_instruction =
        "Experto en mantenimiento predictivo de motores industriales. "
        "Analiza telemetria (temp_c, temp_z, RMS, peak, kurtosis, crest, vib_z) "
        "y diagnostica fallos. Responde SOLO en JSON con: "
        "summary(1-3 palabras), "
        "diagnosis(BEARING_WEAR|MISALIGNMENT|IMBALANCE|THERMAL_ISSUE|UNKNOWN), "
        "confidence(0.0-1.0), severity(OK|WARN|ALARM), "
        "actions([{action,priority(NOW|24H|1W),params}]), "
        "report(parrafo con metricas reales y justificacion).";

    size_t prompt_len = strlen(report_json) + 128;
    char *user_prompt = (char*)malloc(prompt_len);
    if (!user_prompt) return -21;
    snprintf(user_prompt, prompt_len,
             "Analiza este reporte de motor y genera el diagnostico:\n\n%s",
             report_json);

    // ── Construir request body (cJSON — idéntico a la versión PC) ──
    cJSON *req = cJSON_CreateObject();

    cJSON *sys_inst  = cJSON_AddObjectToObject(req, "system_instruction");
    cJSON *sys_parts = cJSON_AddArrayToObject(sys_inst, "parts");
    cJSON *sys_part  = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_part, "text", system_instruction);
    cJSON_AddItemToArray(sys_parts, sys_part);

    cJSON *contents = cJSON_AddArrayToObject(req, "contents");
    cJSON *turn     = cJSON_CreateObject();
    cJSON_AddStringToObject(turn, "role", "user");
    cJSON *parts  = cJSON_AddArrayToObject(turn, "parts");
    cJSON *part0  = cJSON_CreateObject();
    cJSON_AddStringToObject(part0, "text", user_prompt);
    cJSON_AddItemToArray(parts, part0);
    cJSON_AddItemToArray(contents, turn);

    cJSON *gen_config = cJSON_AddObjectToObject(req, "generationConfig");
    cJSON_AddStringToObject(gen_config, "responseMimeType", "application/json");

    cJSON_bool ok = cJSON_PrintPreallocated(req, g_body, sizeof(g_body), 0);
    cJSON_Delete(req);
    free(user_prompt);
    if (!ok) return -22;
    char *body = g_body;

    // ── Abrir socket TLS y enviar petición HTTP ──
    int sock = open_tls_socket();
    if (sock < 0) { return -23; }

    g_response_len = 0;

    const char *extra_headers[] = {
        "Content-Type: application/json\r\n",
        NULL
    };

    struct http_request http_req = {
        .method           = HTTP_POST,
        .url              = url_path,
        .host             = GEMINI_HOST,
        .protocol         = "HTTP/1.1",
        .payload          = body,
        .payload_len      = strlen(body),
        .response         = response_cb,
        .recv_buf         = http_recv_buf,
        .recv_buf_len     = sizeof(http_recv_buf),
        .optional_headers = extra_headers,
    };

    int ret = http_client_req(sock, &http_req, cfg->timeout_ms, NULL);
    zsock_close(sock);

    if (ret < 0) {
        LOG_ERR("http_client_req fallo: %d", ret);
        return -24;
    }
    if (g_response_len == 0) {
        LOG_ERR("Respuesta vacia de Gemini");
        return -24;
    }

    // ── Parsear respuesta — idéntico a la versión PC ──
    cJSON *root = cJSON_Parse(g_response_body);
    if (!root) {
        LOG_ERR("Respuesta no es JSON valido");
        return -25;
    }

    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg  = cJSON_GetObjectItem(error, "message");
        cJSON *code = cJSON_GetObjectItem(error, "code");
        LOG_ERR("Error API Gemini (HTTP %d): %s",
                code  && cJSON_IsNumber(code)  ? code->valueint  : 0,
                msg   && cJSON_IsString(msg)   ? msg->valuestring : "desconocido");
        cJSON_Delete(root);
        return -25;
    }

    cJSON *cands = cJSON_GetObjectItem(root, "candidates");
    if (!cands || !cJSON_IsArray(cands) || cJSON_GetArraySize(cands) == 0) {
        LOG_ERR("No 'candidates' en respuesta");
        cJSON_Delete(root);
        return -26;
    }

    cJSON *c0      = cJSON_GetArrayItem(cands, 0);
    cJSON *content = c0     ? cJSON_GetObjectItem(c0, "content")      : NULL;
    cJSON *parts2  = content ? cJSON_GetObjectItem(content, "parts")  : NULL;

    if (!parts2 || !cJSON_IsArray(parts2) || cJSON_GetArraySize(parts2) == 0) {
        cJSON *finish = c0 ? cJSON_GetObjectItem(c0, "finishReason") : NULL;
        if (finish && cJSON_IsString(finish)) {
            LOG_ERR("Candidate bloqueado: finishReason=%s", finish->valuestring);
        } else {
            LOG_ERR("No 'parts' en candidate[0]");
        }
        cJSON_Delete(root);
        return -26;
    }

    cJSON *p0  = cJSON_GetArrayItem(parts2, 0);
    cJSON *txt = p0 ? cJSON_GetObjectItem(p0, "text") : NULL;

    if (!txt || !cJSON_IsString(txt) || !txt->valuestring) {
        LOG_ERR("No 'text' en parts[0]");
        cJSON_Delete(root);
        return -26;
    }

    char *cleaned = strip_markdown_json(txt->valuestring);
    cJSON_Delete(root);
    if (!cleaned) return -27;

    cJSON *llm_obj = cJSON_Parse(cleaned);
    free(cleaned);
    if (!llm_obj) {
        LOG_ERR("El texto de Gemini no es JSON valido");
        return -27;
    }

    char *out_json = cJSON_PrintUnformatted(llm_obj);
    cJSON_Delete(llm_obj);
    if (!out_json) return -28;

    *out_llm_json = out_json;
    return 0;
}

// ---------------------------------------------------------------------------
// Punto de entrada público
// ---------------------------------------------------------------------------

int llm_analyze_report(const llm_config_t *cfg,
                       const char *report_json,
                       char **out_llm_json)
{
    if (!cfg || !report_json || !out_llm_json) return -100;
    *out_llm_json = NULL;
    return gemini_call(cfg, report_json, out_llm_json);
}
