# Agent Skills & Workflow Runbooks

## Skill: Add New Herb Profile
When requested to add a new medicinal herb to the system:
1. Define the botanical parameters:
   - Common and botanical name
   - Deficit trigger threshold (default: 35–40%)
   - Maximum safety saturation ceiling (default: 55–60%)
2. Add a telemetry tracking object to the state map in `index.html`.
3. Add a corresponding card component in the UI layout with:
   - Real-time moisture progress bar
   - Status badge (Optimal, Deficit, Saturated)
   - Independent manual dose button
4. Update speech parser regex patterns to recognize common synonyms and vernacular names.

---

## Skill: Calibrate Soil Moisture Sensor
When calibrating an analog capacitive soil probe:
1. Sample raw 12-bit ADC values in open air (`ADC_AIR_DRY`, typically 3100–3300).
2. Sample raw ADC values fully submerged in water up to probe line (`ADC_WATER_WET`, typically 1400–1600).
3. Compute the linear calibration mapping:
   - `Moisture% = constrain(map(rawADC, ADC_AIR_DRY, ADC_WATER_WET, 0, 100), 0, 100)`
4. Update firmware constants and document calibration bounds in project telemetry logs.

---

## Skill: Generate MQTT / WebSockets Bridge
When connecting the browser dashboard directly to the physical ESP32:
1. Implement a lightweight WebSocket client in `index.html`.
2. Format outgoing commands as compact JSON: `{"action": "DOSE", "herb": "tulsi"}`.
3. Parse incoming telemetry packets: `{"tulsi": 52.4, "mint": 47.8, "pump": false}`.
4. Update UI DOM nodes dynamically without refreshing the page.
