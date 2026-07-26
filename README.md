# Clock Supervisor v1.0.0
A decentralized, ultra-lightweight UDP monitoring engine designed to act as the central alert receiver and audio dispatcher for a local sensor network.
## Overview
The Clock Supervisor framework listens asynchronously for UDP broadcasts from remote sensor nodes, tracks network health, and queues local .wav audio file playback for triggered alerts. It is designed to operate on minimal hardware (such as an Android device running Termux, a Raspberry Pi, or an ESP32 with LittleFS) without the need for centralized MQTT brokers or home automation servers.
## Core Features
 * **Zero-Lag Event Parsing:** Utilizes a non-blocking network buffer to instantly intercept pushed msg dd01 alert triggers.
 * **Synchronous Multilingual Audio Queueing:** Automatically maps sensor IDs to local audio files, queuing sequential playback across multiple language directories (e.g., en/0101.wav followed by th/0101.wav) while accurately calculating file durations.
 * **Resilient Health Polling:** Automatically queries all tracked nodes every 10 seconds. If a node drops off the network, the supervisor logs a local infrastructure fault and auto-recovers tracking the moment the node reconnects.
 * **Redundant Status Backup:** If a real-time UDP broadcast is lost due to Wi-Fi congestion, the supervisor's backup polling engine detects the sensor's unacknowledged status 2 latch and triggers the critical alarm natively.
 * **Automated Matrix Reset:** Instantly dispatches a status 0 UDP command back to the triggering node to clear its hardware latch once the audio sequence is engaged.
## Directory Structure
Audio files must be structured in directories corresponding to their language codes. The filename format is <Device_ID><Message_ID>.wav.
```text
/clock-supervisor
├── supervisor.py
├── en/
│   ├── 0101.wav   (e.g., "Bed Scale 01 Critical")
│   └── 0201.wav   (e.g., "Motion Sensor 02 Critical")
└── th/
    ├── 0101.wav
    └── 0201.wav

```
## Network Protocol
 * **Listen/Command Port:** 8888 (UDP)
 * **Accepted Triggers:** msg <ID>01
 * **Matrix Polling:** status ?
 * **Clear Command:** status 0
# UDPNode Class v1.0.0
A lightweight, robust C++ firmware class for ESP32 microcontrollers. It provides a standardized framework for creating persistent, self-healing IoT sensor nodes that communicate directly with the Clock Supervisor over UDP.
## Overview
The UDPNode class standardizes how remote sensors handle network connectivity, Over-The-Air (OTA) updates, parameter storage, and critical event dispatching. By enforcing a strict 3-level status taxonomy, it allows disparate hardware—from load cells to ultrasonic motion detectors—to communicate flawlessly with a single, generalized supervisor.
## Core Features
 * **Standardized 3-Level Status Taxonomy:**
   * 0 (Nominal / Ready): System idle.
   * 1 (Informational): Passive state reporting (e.g., steady rain, patient safely in bed).
   * 2 (Critical Latch): Unacknowledged alert requiring immediate supervisor intervention.
 * **Unified Event Dispatcher:** Calling dispatchCritical(1) instantly locks the node to Status 2, formats the standardized msg dd01 packet matching the device's ID, and broadcasts it over UDP.
 * **LittleFS Parameter Binding:** Easily register variables (like distance thresholds, timezone offsets, or alarm durations) to be saved persistently in flash memory and updated on-the-fly via UDP commands.
 * **Self-Healing Wi-Fi Engine:** Automatically monitors network connection health in the background and initiates non-blocking reconnects without interrupting high-speed local sensor loops.
 * **Integrated Time & OTA Management:** Built-in NTP time syncing with LittleFS-configurable time zone and daylight saving time (DST) adjustments, coupled with native ArduinoOTA hooks.
## API Usage Summary
**1. Instantiation & Setup**
```cpp
#include "UDPNode.h"

UDPNode node(8888); 
int device_number = 2;
int system_status = 0;
float distance_threshold = 180.0;

void setup() {
    // Register persistent parameters to LittleFS and UDP engine
    node.registerParam("device_number", &device_number);
    node.registerParam("status", &system_status);
    node.registerParam("threshold", &distance_threshold);

    // Start network AP fallback and parameter server
    node.begin("Proximity_Node_AP");
    node.setDeviceNumber(device_number);
}

```
**2. Dispatching Alerts**
```cpp
void loop() {
    node.update(); // Service background tasks

    if (sensorTriggered()) {
        system_status = 2;             // Lock local state
        node.dispatchCritical(1);      // Broadcasts "msg 0201" to Supervisor
    }
}

```
