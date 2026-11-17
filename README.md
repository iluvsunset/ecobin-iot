# ECOBIN - IoT (ESP32 Smart Trash Bin)

IoT firmware for **ECOBIN**, an automated smart waste bin powered by an ESP32 microcontroller, dual ultrasonic sensors, servo motor lid control, OLED status display, DFPlayer Mini audio module, and Blynk Cloud integration.

---

## 📌 Pinout & Hardware Connections

| Component | Pin Type | ESP32 GPIO | Description |
| :--- | :--- | :--- | :--- |
| **Lid Sensor (HC-SR04)** | TRIG | **GPIO 5** | Hand/motion detection |
| **Lid Sensor (HC-SR04)** | ECHO | **GPIO 18** | Echo distance reading (via 5V $\rightarrow$ 3.3V voltage divider) |
| **Fill Sensor (HC-SR04)** | TRIG | **GPIO 26** | Measures trash depth |
| **Fill Sensor (HC-SR04)** | ECHO | **GPIO 27** | Echo fill percentage calculation |
| **Servo Motor** | PWM | **GPIO 19** | Lid opening (85°) / closing (0°) |
| **OLED Display (SSD1306)** | SDA | **GPIO 21** | I2C Data line |
| **OLED Display (SSD1306)** | SCL | **GPIO 22** | I2C Clock line |
| **DFPlayer Mini (Voice)** | RX | **GPIO 17** | Serial TX2 $\rightarrow$ DFPlayer RX (via 1kΩ resistor) |
| **DFPlayer Mini (Voice)** | TX | **GPIO 16** | Serial RX2 $\leftarrow$ DFPlayer TX |

---

## ⚡ Voltage Divider for GPIO 18 (Echo Protection)
To protect the ESP32 3.3V GPIO from the 5V HC-SR04 Echo signal:
- **$R_1$ ($1000\,\Omega$):** Connected from HC-SR04 `ECHO` to junction node (`GPIO 18`).
- **$R_2 + R_3$ ($1000\,\Omega + 1000\,\Omega = 2000\,\Omega$):** Connected in series from junction node to `GND`.
$$V_{\text{in}} = 5\text{V} \times \frac{2000\,\Omega}{1000\,\Omega + 2000\,\Omega} \approx 3.33\text{V}$$

---

## 🌐 Wi-Fi & Simulation Settings

In `src/main.cpp`:
- **Physical ESP32 Flashing:** Set `#define USE_WOKWI 0` to use `"THE INTERNATIONAL SCHOOL"` Wi-Fi and enable DFPlayer voice audio.
- **Wokwi Simulation:** Set `#define USE_WOKWI 1` to run in Wokwi simulator with `"Wokwi-GUEST"`.

---

## 🚀 How to Build & Flash with VS Code / PlatformIO

1. Open this repository in **VS Code**.
2. Make sure the **PlatformIO IDE** extension is installed.
3. Connect your ESP32 via USB.
4. Click **PlatformIO: Upload** (or run `pio run -t upload`).
