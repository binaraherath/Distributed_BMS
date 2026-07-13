# Distributed AI-Enhanced 16S Battery Management System

## Project Overview
This repository contains the firmware, network architecture, and cloud telemetry pipeline for a distributed 16S Battery Management System. Designed strictly around **Embedded Computer Networks and Software-Hardware Co-development**, this project solves complex battery management challenges using distributed computing nodes, isolated communication buses and software-controlled logic rather than pure analog circuitry.

The system is designed to scale from standard 18650 cell packs up to pouch cells (Nissan Leaf modules) and prismatic cells, leveraging a Master-Satellite topology to separate sensing, actuation, and cloud telemetry.

---

## Core Functionalities

1. **Distributed Data Acquisition:** The system reads 16 individual cell voltages, pack current and temperatures at the battery edge and transmits this data across a secure, priority-mapped network.
2. **Software-Controlled Passive Balancing:** Cell voltage deltas are calculated by the Master BMU, which dynamically commands the analog front-end to trigger external P-Channel MOSFETs and bleed resistors on a per-cell basis.
3. **Fail-Safe Power Distribution:** An independent Auxiliary node physically controls main charge/discharge contactors and thermal management systems based on network commands, providing physical isolation from the measurement logic.
4. **Edge-to-Cloud Telemetry Pipeline:** A dedicated IoT gateway sniffs local bus traffic, serializes the embedded data into JSON payloads, and pushes it to a cloud database via MQTT/Wi-Fi.
5. **AI-Driven State of Health (SOH) Monitoring:** Cloud-hosted Python backend scripts apply a Scikit-Learn machine learning model (trained on NASA Battery Datasets) to historical telemetry to predict battery degradation, visualized on a live Grafana dashboard.

---

## System Requirements

### Functional Requirements
* **FR1 - Cell Measurement:** Node 1 must measure up to 16 series cell voltages, aggregate pack current via a shunt, and read thermistor data using the TI BQ76952 AFE.
* **FR2 - Balancing Actuation:** Node 1 must calculate cell voltage imbalances and transmit I2C commands to the AFE to toggle external passive balancing circuits.
* **FR3 - Network Communication:** All nodes must communicate over a CAN 2.0B bus using standard 11-bit identifiers with predefined priority mapping.
* **FR4 - Contactor Control:** Node 2 must parse incoming CAN messages to control standard charge/discharge relays and precharge sequences.
* **FR5 - Telemetry Forwarding:** Node 3 must operate in CAN promiscuous mode to capture all bus traffic, format it into JSON, and transmit it to an external MQTT broker.

### Non-Functional Requirements
* **NFR1 - Galvanic Isolation:** The CAN bus must utilize isolated CAN transceivers to protect the microcontrollers from the 60V battery ground.
* **NFR2 - Microsecond Fault Response:** Node 1 must utilize hardware-level external interrupts tied to the AFE's ALERT pin to trigger immediate safety routines during short-circuit events.
* **NFR3 - Network Safety:** Node 2 must implement a software watchdog/heartbeat monitor. If network communication from Node 1 is lost, Node 2 must default to a safe state (drop all contactors) to prevent thermal runaway.
* **NFR4 - Scalability:** The network architecture must allow the addition of a second Master BMU (scaling to 32S) without requiring changes to the IoT Gateway or Aux Controller firmware.

---

## System Architecture

The project is built on a 3-node distributed architecture communicating over an **Isolated CAN 2.0B bus**:

* **Node 1: Master BMU (Sensing & Balancing)**
    * **Hardware:** STM32 + TI BQ76952 AFE.
    * **Function:** Sits directly on the battery pack. Executes software-controlled balancing and triggers hardware interrupts for short-circuit protection.
* **Node 2: Aux Controller (Power Distribution)**
    * **Hardware:** STM32.
    * **Function:** Listens to CAN traffic via Hardware Acceptance Filtering. Controls charge/discharge contactors and thermal management. 
* **Node 3: IoT Gateway (Telemetry & Edge-to-Cloud)**
    * **Hardware:** ESP32.
    * **Function:** Sniffs bus traffic, serializes telemetry into JSON, and publishes to an MQTT broker over Wi-Fi.

---

## Weekly Development Timeline

| Week | Phase | Tasks & Deliverables | Syllabus Mapping | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Week 1** | **Formulation** | Finalize distributed architecture, evaluate component feasibility (STM32/ESP32/BQ76952), and define network topology. | 1. Project Formulation | ✅ Completed |
| **Week 2** | **Planning** | Repo initialization, Requirement Engineering documentation (FRs & NFRs), and drafting the software-hardware co-development plan. | 2. Project Plans <br> 3. Requirement Eng. | ✅ Completed |
| **Week 3** | **Modeling** | Model the CAN bus matrix (11-bit IDs, priority mapping) and define the JSON data payloads for telemetry. | 4. High-level Simulation | ⏳ Pending |
| **Week 4** | **Firmware (Node 1)** | Develop STM32 I2C drivers for BQ76952; implement cell reading and external balancing logic. | 5. Architecture Ext. | ⏳ Pending |
| **Week 5** | **Networking** | Implement Isolated CAN 2.0B stack; establish core communication between Node 1 and Node 2. | 6. Prototype Dev. | ⏳ Pending |
| **Week 6** | **Firmware (Node 2)** | Develop Aux Controller CAN acceptance filtering, contactor logic, and heartbeat safety sequence. | 6. Prototype Dev. | ⏳ Pending |
| **Week 7** | **Firmware (Node 3)** | Configure ESP32 CAN promiscuous mode and setup Wi-Fi/Network stack. | 6. Prototype Dev. | ⏳ Pending |
| **Week 8** | **IoT Integration** | Implement JSON serialization on ESP32 and establish MQTT publishing to the backend. | 6. Prototype Dev. | ⏳ Pending |
| **Week 9** | **Cloud Backend** | Setup InfluxDB backend and Python MQTT subscriber script to log time-series data. | 6. Prototype Dev. | ⏳ Pending |
| **Week 10** | **AI Integration** | Integrate Scikit-Learn SOH prediction model into the Python backend pipeline. | 6. Prototype Dev. | ⏳ Pending |
| **Week 11** | **Visualization** | Connect InfluxDB to Grafana; build live dashboards for cell telemetry and AI predictions. | 6. Prototype Dev. | ⏳ Pending |
| **Week 12** | **Testing** | Execute subsystem and integrated testing (Fault injection, CAN bus load testing, heartbeat failure). | 7. Integrated Testing | ⏳ Pending |
| **Week 13** | **Deployment** | Code freeze, prepare production deployment environments, and finalize system configurations. | 8. Prod Environments | ⏳ Pending |
| **Week 14** | **Release** | Finalize all project documentation, produce required artifacts, and present the system. | 9. Artefacts <br> 10. Presentation | ⏳ Pending |* **NFR-4 (Real-Time Constraint):** The RTOS task managing CAN transmission on the Master BMU must not block or be preempted by non-critical tasks (e.g., debug printing) for more than 5ms.

---

## 🛠️ Technology Stack

**Embedded Hardware:**
* Microcontrollers: STM32, ESP32
* Networking: CAN Transceivers
* PCB Design: EasyEDA

**Embedded Software:**
* Firmware: C/C++, FreeRTOS, STM32 HAL
* Protocols: CAN 2.0B, I2C, UART, MQTT

**Cloud & AI Backend:**
* Broker: Eclipse Mosquitto (MQTT)
* Database: InfluxDB
* Dashboard: Grafana
* ML Pipeline: Python

```mermaid
flowchart TD
    subgraph Battery Level
        Pack[16S Battery Pack & Thermistors]
    end

    subgraph Distributed BMS Nodes
        Node1[Node 1: Master BMU<br>STM32 + BQ76952 AFE]
        Node2[Node 2: Aux Controller<br>STM32]
        Node3[Node 3: IoT Gateway<br>ESP32]
        CAN{Isolated CAN 2.0B Bus}
    end

    subgraph Power & Actuation
        Relays[Charge/Discharge Contactors<br>& Thermal Management]
    end

    subgraph Cloud & AI Backend
        MQTT[MQTT Broker]
        DB[(InfluxDB)]
        ML[Python AI SOH Model]
        Dash[Grafana Dashboard]
    end

    Pack <-->|Analog| Node1
    Node1 <-->|Telemetry & Commands| CAN
    CAN <--> Node2
    CAN <--> Node3
    
    Node2 -->|Physical Control| Relays
    Node3 -->|Wi-Fi / JSON| MQTT
    
    MQTT --> DB
    DB <--> ML
    DB --> Dash
