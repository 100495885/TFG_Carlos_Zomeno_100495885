// llm_client.h
#pragma once
#include <stddef.h>

typedef struct {
    // Google Gemini API
    const char *gemini_url;      // e.g. "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent"
    const char *gemini_model;    // e.g. "gemini-2.5-flash"
    const char *gemini_api_key;  // o NULL para leer env GEMINI_API_KEY

    long timeout_ms;             // HTTP timeout
} llm_config_t;

/**
 * Llama a Gemini con el reporte del motor.
 * Devuelve en out_llm_json un JSON (string) que es un objeto, o NULL si error.
 * El caller debe free(out_llm_json).
 *
 * return 0 OK, <0 error
 */
int llm_analyze_report(const llm_config_t *cfg,
                       const char *report_json,
                       char **out_llm_json);