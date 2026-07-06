# AI-Enhanced Distributed Battery Management System (BMS)

![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white)
![STM32](https://img.shields.io/badge/STM32-03234B?style=for-the-badge&logo=stmicroelectronics&logoColor=white)
![ESP32](https://img.shields.io/badge/ESP32-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![MQTT](https://img.shields.io/badge/MQTT-660066?style=for-the-badge&logo=mqtt&logoColor=white)
![Python](https://img.shields.io/badge/Python-3776AB?style=for-the-badge&logo=python&logoColor=white)

## 📌 Project Overview
The increasing demand for reliable energy storage in Electric Vehicles (EVs) and microgrids requires highly robust, fault-tolerant monitoring systems. Traditional centralized Battery Management Systems (BMS) suffer from single points of failure, severe EMI noise susceptibility, and a lack of predictive diagnostics. 

This project implements a **Distributed Intelligent BMS** using a 3-node embedded architecture communicating over a deterministic **CAN 2.0B network**. It integrates an IoT gateway (ESP32) to stream real-time battery telemetry via MQTT to a cloud backend, where an edge-to-cloud Machine Learning pipeline predicts the battery's State of Health (SOH) and detects early degradation anomalies.

---

## 🏗️ System Architecture (3-Node Distributed Topology)

1. **Node 1: Master Battery Monitoring Unit (BMU)**
   * **Hardware:** STM32F103 / STM32G4
   * **Role:** Interfaces with cell voltage/temperature sensors via SPI/I2C. Executes safety-critical fault logic (Over-Voltage, Under-Voltage, Over-Current) and broadcasts telemetry to the CAN bus.
2. **Node 2: Auxiliary Controller**
   * **Hardware:** STM32F103
   * **Role:** Listens to CAN bus traffic to manage peripheral actuators, including PWM-based thermal management (cooling fans) and charging/discharging contactor relays.
3. **Node 3: IoT & AI Gateway**
   * **Hardware:** ESP32
   * **Role:** Bridges the local CAN network to the cloud. Sniffs CAN frames, parses payload data, and publishes JSON payloads via MQTT over Wi-Fi. 

---

## ⚙️ Core Functionalities

* **Distributed Networking:** Real-time, noise-immune communication between independent Microcontroller Units (MCUs) using CAN bus.
* **Precise Cell Monitoring:** Continuous acquisition of individual cell voltages, pack current, and thermal gradients.
* **Intelligent Thermal Management:** Dynamic PWM cooling control based on real-time temperature data broadcasted over the network.
* **Hardware-Level Protection:** Immediate physical isolation of the battery pack via relays upon detecting critical thresholds (OVP, UVP, OCP, OTP).
* **Cloud Telemetry:** Live streaming of battery metrics to a Grafana digital twin dashboard via an MQTT broker.
* **AI-Driven SOH Prediction:** Backend processing of historical time-series data using Machine Learning (Linear Regression/Random Forest) to estimate battery degradation and State of Health (SOH).

---

## 📋 Requirements Specification

### Functional Requirements (FR)
* **FR-1:** The Master BMU shall measure individual cell voltages with an accuracy of ±10mV and broadcast this data over the CAN bus every 100ms.
* **FR-2:** The Auxiliary Controller shall activate the cooling fan via PWM when the broadcasted pack temperature exceeds 35°C, scaling fan speed proportionally up to 45°C.
* **FR-3:** The protection logic shall open the main contactor relay within 50ms of receiving a critical fault CAN frame.
* **FR-4:** The IoT Gateway shall translate standard CAN 2.0B frames into MQTT JSON payloads and publish them to the cloud broker at a frequency of 1Hz.
* **FR-5:** The backend ML pipeline shall calculate an updated State of Health (SOH) percentage at the end of every complete charge/discharge cycle.

### Non-Functional Requirements (NFR)
* **NFR-1 (Reliability):** The local control network must use differential signaling (CAN bus) to reject electromagnetic interference from high-current switching.
* **NFR-2 (Scalability):** The software architecture shall allow new auxiliary nodes to be added to the CAN bus without requiring modification to the Master BMU's firmware.
* **NFR-3 (Fault Tolerance):** If the IoT Gateway loses Wi-Fi connection, the local CAN network and Master/Aux protection nodes must continue to operate independently and safely.
* **NFR-4 (Real-Time Constraint):** The RTOS task managing CAN transmission on the Master BMU must not block or be preempted by non-critical tasks (e.g., debug printing) for more than 5ms.

---

## 🛠️ Technology Stack

**Embedded Hardware:**
* Microcontrollers: STM32 (ARM Cortex-M), ESP32 (Xtensa Dual-Core)
* Networking: MCP2515 CAN Controllers / TJA1050 CAN Transceivers
* PCB Design: EasyEDA

**Embedded Software:**
* Firmware: C/C++, FreeRTOS, STM32 HAL
* Protocols: CAN 2.0B, SPI, I2C, UART, MQTT

**Cloud & AI Backend:**
* Broker: Eclipse Mosquitto (MQTT)
* Database: InfluxDB (Time-series data)
* Dashboard: Grafana
* ML Pipeline: Python (Scikit-Learn, Pandas)
