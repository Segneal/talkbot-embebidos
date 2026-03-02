# Talkbot

Asistente de voz con ESP32 y push-to-talk. Firmware embebido + backend Node.js con servicios de AWS.

## Arquitectura

```
[Botón PTT] → [ESP32 + MAX9814] → WiFi → [Backend Node.js]
                                            ├── Amazon Transcribe (STT)
                                            ├── Claude Bedrock (LLM)
                                            └── Amazon Polly (TTS)
[Speaker + PAM8403] ← WAV ← ───────────────┘
[Display ST7789]
```

El ESP32 graba audio al mantener presionado el botón, lo envía al backend por HTTP, y reproduce la respuesta de audio por el parlante. Un display TFT muestra el estado, volumen, información de red y estadísticas.

## Hardware

| Componente | Descripción |
|---|---|
| ESP32 DevKit v1 | Microcontrolador principal |
| MAX9814 | Micrófono con AGC |
| PAM8403 | Amplificador analógico 3W (clase D) |
| Parlante 8Ω | Salida de audio |
| Portapilas 2x AA (3V) | Alimentación del PAM8403 |
| ST7789 240x240 | Display TFT color SPI |
| 4 pulsadores | PTT, Vol+, Vol-, Pantalla |
| Resistencia 10KΩ | Pulldown en GPIO25 (DAC) |
| Breadboard + cables dupont | Prototipado |

### Pinout

| Componente | GPIO | Función |
|---|---|---|
| MAX9814 OUT | 34 | Entrada analógica (ADC1_CH6) |
| DAC (→ PAM8403) | 25 | Salida analógica DAC interno |
| Botón PTT | 27 | INPUT_PULLUP, activo LOW |
| Botón Vol+ | 32 | INPUT_PULLUP, activo LOW |
| Botón Vol- | 33 | INPUT_PULLUP, activo LOW |
| Botón Pantalla | 13 | INPUT_PULLUP, activo LOW |
| ST7789 MOSI | 23 | Datos SPI |
| ST7789 SCK | 18 | Reloj SPI |
| ST7789 CS | 5 | Chip select |
| ST7789 DC | 2 | Data/Command |
| ST7789 RST | 4 | Reset |
| ST7789 BL | 15 | Backlight (PWM) |

El archivo `circuit_diagram.html` contiene el diagrama de circuito interactivo (abrir en navegador).

## Estructura del proyecto

```
src/                          # Firmware ESP32
├── main.cpp                  # Loop principal y máquina de estados
├── config.h                  # Pines, WiFi, configuración
├── audio_recorder.h/cpp      # Grabación I2S en modo ADC
├── audio_player.h/cpp        # Reproducción DAC con control de volumen
├── display_controller.h/cpp  # Display TFT ST7789 (6 pantallas)
├── api_client.h/cpp          # Cliente HTTP al backend
└── web_server.h/cpp          # Servidor web + API REST

data/                         # Web UI (servida por LittleFS)
├── index.html
├── style.css
└── app.js

backend/                      # Servidor Node.js
├── server.js                 # Express, endpoints, middleware
├── stt_service.js            # Amazon Transcribe Streaming
├── llm_service.js            # Claude via AWS Bedrock
├── tts_service.js            # Amazon Polly (neural)
├── search_service.js         # Búsqueda web via Tavily
└── logger.js                 # Logging estructurado
```

## Máquina de estados

```
IDLE → LISTENING → PROCESSING → SPEAKING → IDLE
  └──────────────── ERROR ←──────────┘
```

| Estado | Display | Descripción |
|---|---|---|
| IDLE | Icono reposo (cyan) | Esperando botón |
| LISTENING | Icono micrófono (verde) | Grabando audio |
| PROCESSING | Animación rotativa (amarillo) | Enviando al backend |
| SPEAKING | Icono parlante (naranja) | Reproduciendo respuesta |
| ERROR | Icono error (rojo) | Error, vuelve a IDLE en 3s |
| WIFI_CONFIG | Icono WiFi (azul) | Portal cautivo activo |

### Pantallas del display

El botón de pantalla navega entre 6 vistas:

| # | Pantalla | Contenido |
|---|---|---|
| 0 | Estado | Estado actual con icono animado |
| 1 | Volumen | Barra de volumen y nivel de pico |
| 2 | WiFi | SSID, IP, RSSI, calidad de señal |
| 3 | Conversación | Última pregunta y respuesta |
| 4 | Estadísticas | Conversaciones, latencia, heap, uptime |
| 5 | VU Meter | Medidor de nivel de audio en tiempo real |

## Instalación

### 1. Firmware ESP32

Requiere [PlatformIO](https://platformio.org/).

```bash
# Compilar y flashear firmware
pio run -t upload

# Subir filesystem (web UI)
pio run -t uploadfs
```

### 2. Configurar WiFi

Al iniciar por primera vez, el ESP32 crea un access point:

- **SSID:** `Talkbot-Setup`
- **Contraseña:** `talkbot123`

Conectarse a esa red y configurar las credenciales WiFi desde el portal cautivo.

### 3. Backend

```bash
cd backend
npm install

# Configurar variables de entorno
cp .env.example .env
# Editar .env con la región AWS, modelo, etc.

# Requiere AWS CLI configurado
aws configure

# Iniciar servidor
npm start
```

El servidor inicia en `http://0.0.0.0:8000`.

### 4. Configurar URL del backend

Desde la web UI del ESP32 (`http://talkbot.local` o la IP del dispositivo):

1. Ir a la pestaña **Red**
2. Ingresar la URL del backend (ej: `http://192.168.0.100:8000`)
3. Click en **Guardar URL** (se aplica inmediatamente)

## Variables de entorno del backend

```bash
AWS_REGION=us-east-1
BEDROCK_MODEL_ID=us.anthropic.claude-haiku-4-5-20251001-v1:0
POLLY_VOICE_ID=Lupe
TAVILY_API_KEY=           # Para búsqueda web en tiempo real
PORT=8000
API_KEY=                  # Opcional, dejar vacío para dev local
```

Requiere una cuenta AWS con acceso a Transcribe, Bedrock (Claude) y Polly.

## Web UI

Accesible desde `http://talkbot.local` o la IP del ESP32.

**Pestaña Bot:**
- Estado del dispositivo en tiempo real
- Selector de agente (Lupe / Pedro)
- Control de volumen

**Pestaña Red:**
- Configuración de URL del backend
- Botón de reinicio remoto
- Info del sistema (IP, WiFi RSSI, uptime, heap libre)

## API del backend

| Método | Endpoint | Descripción |
|---|---|---|
| GET | `/api/health` | Health check |
| POST | `/api/chat` | Pipeline completo: STT → LLM → TTS |
| POST | `/api/clear` | Limpiar historial de conversación |

### POST /api/chat

- **Body:** archivo WAV (audio/wav)
- **Headers:** `x-voice` (voz TTS), `x-agent` (agente LLM), `x-api-key` (si está configurado)
- **Response:** archivo WAV con la respuesta hablada

## Detalles técnicos

- **Grabación:** I2S_NUM_0 en modo ADC, muestreo a 44.1kHz con downsampling a 16kHz, mono 16-bit, máximo 3 segundos
- **Reproducción:** DAC interno del ESP32 (GPIO25), salida analógica al PAM8403, 22050Hz
- **Display:** ST7789 via SPI con TFT_eSPI, dirty-flag pattern para updates parciales, TFT_eSprite anti-flicker
- **Threading:** La grabación corre en un task de FreeRTOS (Core 1) para no bloquear el loop principal
- **Persistencia:** La URL del backend se guarda en NVS (Non-Volatile Storage)
- **mDNS:** El dispositivo se anuncia como `talkbot.local`
- **Agentes:** Cada agente tiene personalidad y voz distinta, habla en español rioplatense
