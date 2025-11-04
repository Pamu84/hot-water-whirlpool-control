// ---------------- STATES ----------------
let pumpState = false;        // Actual device state
let heaterState = false;      // Actual device state
let uvFilterState = false;    // Actual device state
let airState = false;         // Actual device state
let lightsState = false;      // Actual device state
let heaterAutoState = false;  // Auto mode selection
let setTemp = 40.0;
let tempTolerance = 1.0;

let pumpManualSelected = false;
let heaterManualSelected = false;
let uvFilterManualSelected = false;
let airManualSelected = false;
let lightsManualSelected = false;

// ---------------- WEBSOCKET ----------------
// Establish WebSocket connection to server. Handle incoming messages to update UI.
// Note: WebSocket URL uses the same host as the webpage.
// ws:// for secure (wss) if served over HTTPS, ws:// for HTTP.
const ws = new WebSocket(`ws://${window.location.host}/ws`);
ws.onopen = function () {
  console.log("WebSocket connected");
};
// Handle incoming messages. Expecting JSON data with device states and sensor readings.
ws.onmessage = function (event) {
  try {
    const data = JSON.parse(event.data);
    console.log("WebSocket data:", data);

    // Temperatures
    document.getElementById("poolTemperature").textContent =
      data.pool_temp !== undefined ? `${data.pool_temp.toFixed(1)} °C` : "N/A";

    document.getElementById("outsideTemperature").textContent =
      data.outside_temp !== undefined ? `${data.outside_temp.toFixed(1)} °C` : "N/A";

    // RSSI
    document.getElementById("rssi").textContent =
      data.rssi !== undefined ? data.rssi : "N/A";

    // Pressure
    document.getElementById("pressure").textContent =
      data.pressure !== undefined ? `${data.pressure.toFixed(2)} kPa` : "N/A";

    // Total Energy
    document.getElementById("energy_total").textContent =
      data.energy_total !== undefined ? data.energy_total.toFixed(2) : "0.0";

    // Energy
    document.getElementById("energy_current_w").textContent =
      data.energy_current_w !== undefined ? data.energy_current_w.toFixed(2) : "0.0";

    // Settings
    document.getElementById("setTemp").value =
      data.setTemp !== undefined ? data.setTemp : "40.0";

    document.getElementById("tempTolerance").value =
      data.tempTolerance !== undefined ? data.tempTolerance : "1.0";

    // Update button states (both manual and actual state are reflected by the button's class)
    updateButtonState("pump", data.pump || false, data.pumpManual || false);
    updateButtonState("heater", data.heater || false, data.heaterManual || false);
    updateButtonState("uvFilter", data.uvFilter || false, data.uvFilterManual || false);
    updateButtonState("air", data.air || false, data.airManual || false);
    updateButtonState("lights", data.lights || false, data.lightsManual || false);
    updateButtonState("heaterAuto", data.heater_auto || false, data.heater_auto || false);

    // Sync local states
    // data.pump, data.heater, etc. are expected to be booleans
    // || = logical OR to default to false if undefined
    pumpState = data.pump || false;
    heaterState = data.heater || false;
    uvFilterState = data.uvFilter || false;
    airState = data.air || false;
    lightsState = data.lights || false;
    heaterAutoState = data.heater_auto || false;

  } catch (e) {
    console.error("Error parsing WebSocket data:", e);
  }
};

// ---------------- CONTROL ----------------
function sendControl(param, value) {
  if (ws.readyState === WebSocket.OPEN) {
    const msg = { param: param, value: value };
    ws.send(JSON.stringify(msg));
    console.log("WS sent:", msg);
  } else {
    console.warn("WebSocket not open. Cannot send:", param, value);
  }
}

// ---------------- BUTTON STATE HELPER ----------------
// Updates button appearance based on device state and manual selection.
function updateButtonState(device, state, manual) {
  const buttonMap = {
    pump: "pumpButton",
    heater: "heaterButton",
    uvFilter: "uvFilterButton",
    air: "airButton",
    lights: "lightsButton",
    heaterAuto: "heaterAutoButton"
  };

  const buttonElement = document.getElementById(buttonMap[device]);
  if (buttonElement) {
    // Update button class to reflect state (on/off)
    buttonElement.className = `device-btn ${state ? "on" : "off"} ${manual && device !== "heaterAuto" ? "manual" : ""} ${device === "heaterAuto" && state ? "auto" : ""}`;

    // Update manual selection state
    if (device === "pump") pumpManualSelected = manual;
    else if (device === "heater") heaterManualSelected = manual;
    else if (device === "uvFilter") uvFilterManualSelected = manual;
    else if (device === "air") airManualSelected = manual;
    else if (device === "lights") lightsManualSelected = manual;
    else if (device === "heaterAuto") heaterAutoState = manual;
  }
}

// ---------------- BUTTON HANDLERS ----------------
// Generic toggle function for buttons. The actual state is determined by the button's class.
function toggleButtonState(device, buttonId, param) {
  const buttonElement = document.getElementById(buttonId);
  if (!buttonElement) {
    console.error(`Button element not found: ${buttonId}`);
    return;
  }
  // Determine new state based on the button's current class (reflects server state)
  const isOn = buttonElement.classList.contains("on") || (device === "heaterAuto" && buttonElement.classList.contains("auto"));
  const newState = !isOn; // Toggle based on button state
  sendControl(param, newState);
  // Optimistically update button state
  buttonElement.className = `device-btn ${newState ? (device === "heaterAuto" ? "auto" : "on") : "off"}`;
}

// ---------------- BUTTON LISTENERS ----------------
// Button listeners for toggling states. The actual state is determined by the button's class.
document.getElementById("pumpButton").addEventListener("click", () => {
  toggleButtonState("pump", "pumpButton", "pump");
});

document.getElementById("heaterButton").addEventListener("click", () => {
  toggleButtonState("heater", "heaterButton", "heater");
});

document.getElementById("uvFilterButton").addEventListener("click", () => {
  toggleButtonState("uvFilter", "uvFilterButton", "uvFilter");
});

document.getElementById("airButton").addEventListener("click", () => {
  toggleButtonState("air", "airButton", "air");
});

document.getElementById("lightsButton").addEventListener("click", () => {
  toggleButtonState("lights", "lightsButton", "lights");
});

document.getElementById("heaterAutoButton").addEventListener("click", () => {
  toggleButtonState("heaterAuto", "heaterAutoButton", "heater_auto");
});

// ---------------- SETTINGS ----------------
// Input validation and sending new settings to server.
document.getElementById("setTemp").addEventListener("change", (e) => {
  const value = parseFloat(e.target.value);
  if (!isNaN(value) && value >= 1 && value <= 40) {
    sendControl("setTemp", value);
    setTemp = value;
  } else {
    e.target.value = setTemp.toFixed(1);
  }
});

document.getElementById("tempTolerance").addEventListener("change", (e) => {
  const value = parseFloat(e.target.value);
  if (!isNaN(value) && value >= 0.1 && value <= 5) {
    sendControl("tempTolerance", value);
    tempTolerance = value;
  } else {
    e.target.value = tempTolerance.toFixed(1);
  }
});
