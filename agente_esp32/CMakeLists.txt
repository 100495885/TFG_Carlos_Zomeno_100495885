cmake_minimum_required(VERSION 3.20.0)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/secrets.conf)
    set(EXTRA_CONF_FILE "secrets.conf")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(agente_predictivo)

target_sources(app PRIVATE
    src/main.c
    src/cJSON.c
    src/features.c
    src/baseline.c
    src/policy.c
    src/report.c
    src/llm_client.c
    src/sensors.c
)

target_include_directories(app PRIVATE src)
