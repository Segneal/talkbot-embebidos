# Talkbot

Asistente de voz con ESP32 y push-to-talk. Firmware embebido + backend Node.js con servicios de AWS.

## Arquitectura

```
[Botón] → [ESP32 + MAX9814] → WiFi → [Backend Node.js]
                                         ├── Amazon Transcribe (STT)
                                         ├── Claude Bedrock (LLM)
                                         └── Amazon Polly (TTS)
[Speaker + MAX98357] ← WAV ← ──────────────┘
```

El ESP32 graba audio al mantener presionado el botón, lo envía al backend por HTTP, y reproduce la respuesta de audio por el parlante.

## Hardware

| Componente | Descripción |
|---|---|
| ESP32 Dev Board | Microcontrolador principal |
| MAX9814 | Micrófono con AGC |
| MAX98357 | Amplificador I2S |
| Parlante 8Ω 3W | Salida de audio |
| 3 LEDs (R/Y/G) | Indicadores de estado |
| Pulsador | Push-to-talk |
| 4x pilas AA / PowerBank 5V | Alimentación |

### Pinout

| Pin | GPIO | Función |
|---|---|---|
| MAX98357 BCLK | 26 | I2S bit clock |
| MAX98357 LRC | 25 | I2S word select |
| MAX98357 DIN | 22 | I2S data out |
| MAX9814 OUT | 34 | Entrada analógica (ADC1_CH6) |
| Botón PTT | 27 | INPUT_PULLUP, activo LOW |
| LED Rojo | 15 | Hablando / Error |
| LED Amarillo | 2 | Procesando |
| LED Verde | 4 | Escuchando |

El archivo `circuit_diagram.html` contiene el diagrama de circuito interactivo (abrir en navegador).

## Estructura del proyecto

```
src/                          # Firmware ESP32
├── main.cpp                  # Loop principal y máquina de estados
├── config.h                  # Pines, WiFi, configuración
├── audio_recorder.h/cpp      # Grabación I2S en modo ADC
├── audio_player.h/cpp        # Reproducción I2S con control de volumen
├── led_controller.h/cpp      # Control de LEDs por estado
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

| Estado | LED | Descripción |
|---|---|---|
| IDLE | Apagado | Esperando botón |
| LISTENING | Verde | Grabando audio |
| PROCESSING | Amarillo | Enviando al backend |
| SPEAKING | Rojo | Reproduciendo respuesta |
| ERROR | Rojo (parpadeo) | Error, vuelve a IDLE en 3s |

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

- **Grabación:** I2S_NUM_0 en modo ADC, 16kHz/16-bit/mono, máximo 3 segundos
- **Reproducción:** I2S_NUM_1 en modo TX, 22050Hz (frecuencia de salida de Polly)
- **Threading:** La grabación corre en un task de FreeRTOS (Core 1) para no bloquear el loop principal
- **Persistencia:** La URL del backend se guarda en NVS (Non-Volatile Storage)
- **mDNS:** El dispositivo se anuncia como `talkbot.local`
- **Agentes:** Cada agente tiene personalidad y voz distinta, habla en español rioplatense
