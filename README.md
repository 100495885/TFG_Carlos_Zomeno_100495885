# Microagente de mantenimiento predictivo sobre ESP32-S3

Agente embebido de mantenimiento predictivo para motores industriales, con
detección local determinista y diagnóstico asistido por un modelo de lenguaje
(Google Gemini), ejecutado sobre ESP32-S3 mediante Zephyr RTOS.

## Estructura del proyecto

```
agente_esp32/
├── src/            # Código fuente (baseline, features, policy, report,
│                   #   sensors, llm_client, main, cJSON)
├── CMakeLists.txt  # Definición de la compilación
├── Kconfig         # Opciones de configuración del proyecto
└── prj.conf        # Configuración del despliegue (WiFi, API key, etc.)
```

## Requisitos

- Entorno de desarrollo de Zephyr (`west` + SDK de Zephyr).
- `esptool` para el flasheo.
- Placa ESP32-S3 conectada por USB.
- Red WiFi de 2,4 GHz y una clave de acceso a la API de Google Gemini.

> **Entorno de compilación.** El proyecto se compila sobre Linux y se flashea desde el sistema donde esté el puerto USB de la placa. Si compilas y flasheas en la misma máquina, todas las rutas son locales; en un montaje con máquina virtual (p. ej. Lima sobre macOS), compila en la VM y flashea desde el anfitrión.

## Configuración

Antes de compilar, edita `agente_esp32/prj.conf` con tus credenciales:

```
CONFIG_WIFI_SSID="tu_red"
CONFIG_WIFI_PASSWORD="tu_contraseña"
CONFIG_GEMINI_API_KEY="tu_api_key"
```

## Uso

Define una única variable con la ruta a tu copia del proyecto y ajusta el puerto
serie; el resto de comandos no necesita cambios:

```bash
PROJ=~/ruta/al/repo/agente_esp32     # ruta a la carpeta del proyecto
PORT=/dev/cu.usbserial-120           # tu puerto serie (ver más abajo)
```

**1. Compilar** (reconstrucción limpia):

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu -d "$PROJ/build" "$PROJ"
```

**2. Flashear** la placa:

```bash
esptool --chip esp32s3 --port "$PORT" --baud 460800 \
    write-flash 0x0 "$PROJ/build/zephyr/zephyr.bin"
```

**3. Monitorizar** la salida (añade `-L` para guardar un log):

```bash
screen "$PORT" 115200
```

## Comandos útiles

Averiguar el puerto de la placa:

```bash
ls /dev/cu.*        # macOS
ls /dev/ttyUSB*     # Linux
```

Liberar el puerto si quedó ocupado por un monitor previo:

```bash
kill -9 $(lsof -t "$PORT" 2>/dev/null) 2>/dev/null
```

## Consulta de las alarmas

El agente guarda cada alarma (reporte + diagnóstico) en el sistema de ficheros
del dispositivo. Desde la consola del ESP32 (prompt `uart:~$`):

```bash
fs ls lfs/ALARM                    # listar alarmas
fs cat lfs/ALARM/<nombre>.json     # leer una alarma
```

> Las alarmas persisten durante la ejecución, pero cada reflasheo reinicia el
> sistema de ficheros: consúltalas antes de cargar un nuevo binario.

## Reproducir los escenarios de prueba

El perfil de fallo simulado se fija en `src/sensors.c`, en `sensors_init()`.
Modifica el tipo de fallo y la severidad, recompila y flashea:

```c
g.profile.fault        = SIM_FAULT_BEARING_WEAR;  // Puede ser SIM_FAULT_NONE, SIM_FAULT_IMBALANCE, SIM_FAULT_MISALIGNMENT o SIM_FAULT_BEARING_WEAR
                                                 
g.profile.severity_0_1 = 0.6f;                    // Varía entre 0.0 y 1.0
```
