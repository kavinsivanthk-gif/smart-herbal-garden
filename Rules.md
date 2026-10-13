# Project Rules: Voice-Controlled Smart Herbal Garden

## 1. System Scope & Architecture
- **Target Application:** Dual-herb benchtop micro-irrigation controller with edge/browser voice processing.
- **Frontend Stack:** Single-page dashboard using Vanilla JavaScript, HTML5 Web Speech API, and Tailwind CSS.
- **Firmware Stack:** ESP32 (Arduino framework), utilizing ADC1 channels exclusively (Wi-Fi safe).
- **Core Principle:** Safety-gated dual arbitration. Voice commands are treated as requests, never direct actuator overrides.

## 2. Invariant Hardware Safety Rules
- **Moisture Ceiling (Root Hypoxia Guard):** Never actuate the micro-pump if soil volumetric water content exceeds 60%.
- **Dosing Restriction:** Actuation must strictly occur via fixed-duration pulses (default: 3 seconds / ~40 mL). Continuous pump runs are prohibited.
- **Absorption Cooldown:** Enforce a minimum 60-second cooldown lock between successive water pulses to allow capillary soil absorption.
- **Relay Polarity:** All relay logic must explicitly default to HIGH upon boot to prevent spurious activation on active-LOW optoisolated modules.

## 3. Code Standards & Constraints
- **Non-Blocking Execution:** Avoid `delay()` in firmware; use `millis()` or FreeRTOS timers.
- **No Heavy Frontend Frameworks:** Keep the web dashboard lightweight and dependency-free (use CDN-delivered Tailwind and browser-native APIs).
- **Graceful Speech Degradation:** Provide manual UI fallbacks whenever Web Speech API is blocked, unsupported, or fails to spot keywords.
