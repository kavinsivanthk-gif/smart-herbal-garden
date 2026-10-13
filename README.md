# Voice-Controlled Smart Herbal Garden Controller

Dual/tri-herb benchtop micro-irrigation controller with edge/browser voice processing, dual-safety arbitration, and ESP32 hardware bridge.

---

## 🌿 Implemented System Architecture

### 1. Frontend Web Hub (`index.html`)
- **Single-Page Dashboard:** Vanilla JavaScript, HTML5 Web Speech API, Tailwind CSS, FontAwesome.
- **Botanical Profiles:**
  - **Tulsi (Holy Basil / *Ocimum sanctum*):** Safe bounds: 40%–60%, Synonyms: *tulsi, holy basil, basil, vrinda, surasa*.
  - **Mint (Pudina / *Mentha spicata*):** Safe bounds: 40%–60%, Synonyms: *mint, pudina, spearmint, peppermint*.
  - **Rosemary (*Salvia rosmarinus*):** Safe bounds: 35%–55%, Synonyms: *rosemary, gulmehndi, salvia, mary's mantle*.
  - **Dynamic Herb Profile Builder:** Modal to register any additional medicinal herbs and custom speech triggers.
- **Hardware Safety Invariants:**
  - **Root Hypoxia Guard:** Irrigation locked out if moisture &ge; 60%.
  - **Fixed Pulse Dosing:** Strictly limited to 3.0s pulses (~40 mL).
  - **Capillary Absorption Cooldown:** 60-second lockout timer with live countdown on UI & voice rejection.
  - **Speech Degradation Fallback:** Manual command input field for noisy or unsupported environments.
- **Capacitive Sensor Calibration Tool:**
  - Interactive probe calibration with formula:
    `Moisture% = constrain(map(rawADC, ADC_AIR_DRY, ADC_WATER_WET, 0, 100), 0, 100)`
- **ESP32 WebSocket Client Bridge:**
  - Connects to `ws://[ESP32_IP]:81` (default: `ws://192.168.4.1:81`).
  - Sends commands: `{"action": "DOSE", "herb": "tulsi"}`.
  - Receives telemetry packets dynamically without reloading.
  - Automatic fallback to soil physics simulation mode when offline.

---

### 2. ESP32 Arduino Firmware (`esp32_firmware.ino`)
- **ADC1 Pin Assignments (Wi-Fi Safe):**
  - Tulsi Sensor: `GPIO 32` (ADC1_CH4)
  - Mint Sensor: `GPIO 33` (ADC1_CH5)
  - Rosemary Sensor: `GPIO 34` (ADC1_CH6)
- **Active-LOW Relay Outputs:**
  - Pump 1 (Tulsi): `GPIO 25`
  - Pump 2 (Mint): `GPIO 26`
  - Pump 3 (Rosemary): `GPIO 27`
  - **Invariant:** Pre-set to `HIGH` before `pinMode(OUTPUT)` to eliminate boot glitches.
- **Non-blocking State Machine:**
  - Uses `millis()` timers for 3000 ms pulse and 60,000 ms cooldown.
  - No `delay()` calls in the main loop.
- **Embedded WebSocket Server (Port 81):**
  - Serves JSON telemetry at 1 Hz.
  - Handles incoming dosing commands and calibration updates.

---

## 🚀 Getting Started

### Running the Web Dashboard
Open [index.html](file:///c:/Users/kavin/Downloads/New%20folder/index.html) directly in any modern browser (Chrome, Edge, or Firefox).

### Flashing the ESP32
1. Open [esp32_firmware.ino](file:///c:/Users/kavin/Downloads/New%20folder/esp32_firmware.ino) in Arduino IDE or VS Code PlatformIO.
2. Install dependencies:
   - `WebSockets` by Markus Sattler
   - `ArduinoJson` (v6.x)
3. Select board: **ESP32 Dev Module**.
4. Flash the sketch to your board.
5. Connect your computer to the ESP32's Wi-Fi Access Point (`HerbalGarden_ESP32`, password: `herbalgarden123`).
6. In the web dashboard, click **Sim Mode (Offline)** &rarr; **Connect to ESP32** (`ws://192.168.4.1:81`).
