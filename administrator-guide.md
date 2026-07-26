# Clock Supervisor Administrator's Guide
This guide covers the network topology, command syntax, and configuration standards for the Clock Supervisor system and its associated UDP sensor nodes.
## 1. System Architecture & Networking
The system operates on a decentralized, peer-to-peer UDP broadcasting model. The Python-based Clock Supervisor acts as the central listener, while ESP32 endpoints operate as autonomous state machines.
 * **Primary Protocol:** UDP
 * **Universal Operating Port:** 8888 (Tx/Rx for both Supervisor and Nodes)
 * **Time Synchronization:** NTP (Default: pool.ntp.org) via local Node configuration.
## 2. UDP Command Reference
Communication occurs via lightweight text strings. The UDPNode/ParamServer framework on the ESP32 automatically parses incoming strings formatted as <parameter> <value> to update LittleFS variables.
### Supervisor to Node Commands (Control & Polling)
| Command Syntax | Action | Example Usage | Description |
|---|---|---|---|
| status ? | **Poll State** | status ? | Queries the node's current status matrix. The node will reply with OK status <value>. |
| status 0 | **Clear Latch** | status 0 | Resets a triggered node from Critical (2) back to Ready (0). |
| status <value> | **Force State** | status 2 | Manually forces the node into a specific status state. |
| <param> ? | **Query Parameter** | threshold ? | Requests the current value of any registered parameter. |
| <param> <value> | **Set Parameter** | alarm_dur 1500 | Updates a registered parameter on the node and saves it to LittleFS. |
### Node to Supervisor Messages (Triggers & Telemetry)
| Message Syntax | Action | Example (from Node 02) | Description |
|---|---|---|---|
| msg <dd><id> | **Critical Alert** | msg 0201 | Pushed immediately when a node enters Status 2. dd is the Device ID; id is the Message ID. |
| OK <param> <value> | **Poll Response** | OK status 2 | The node's standard response to a Supervisor query. |
| alert <text> | **Soft Alert** | alert Low Battery | Informational text string sent to the Supervisor for terminal logging without triggering audio. |
## 3. Node & Parameter Configuration
Nodes running the ParamServer/UDPNode framework support dynamic, on-the-fly configuration without requiring OTA reflashing.
### Standard Registered Parameters
You can query (?) or set any of these over UDP port 8888:
 * device_number: The logical ID (e.g., 1 for Bed Scale, 2 for Proximity Node).
 * device_name: Human-readable string identifier.
 * status: The current position in the 3-level taxonomy.
 * time_zone: Hourly offset from UTC.
 * dst: Daylight Saving Time offset (in hours).
### Device-Specific Parameters (Example: Proximity Node)
 * threshold: Distance trigger limit in centimeters.
 * alarm_dur: Local buzzer pulse duration in milliseconds.
 * distance: Read-only transient telemetry. (Query using distance ?).
## 4. System Standards
### Standardized Status Taxonomy
All nodes must adhere to this 3-level state machine:
| Status | Meaning | Supervisor Action |
|---|---|---|
| **0** | **Nominal / Ready** | Standard health tracking. |
| **1** | **Informational** | Passive reporting (e.g., steady rain, patient in bed). Logged but no audio. |
| **2** | **Critical Latch** | Unacknowledged alert. Supervisor instantly plays audio and dispatches status 0. |
### Device ID Assignments
| ID (dd) | Device Name | Audio File Trigger |
|---|---|---|
| **01** | Bed Weight Scale | 0101.wav |
| **02** | Proximity Node (Motion) | 0201.wav |
| **03** | Rain Sensor | 0301.wav |
| **04** | Earthquake Alarm | 0401.wav |
| **05** | Doorbell | 0501.wav |
## 5. Audio Subsystem
The Supervisor utilizes a synchronous queueing engine using native OS media players. Audio files are mapped to the Device ID and Message ID parsed from the msg dd01 UDP packet.
**Directory Structure Requirement:**
The Python supervisor expects local folders matching the active language codes. Files must be named according to the trigger code.
 * English Directory: en/
 * Thai Directory: th/
If multiple languages are passed via command line flags (e.g., --en --th), the supervisor parses the WAV header to calculate the exact duration, plays the first file, waits the precise millisecond count, and seamlessly fires the second file.
## 6. Maintenance & Troubleshooting
 * **Android/Termux Network Dropping (Errno 101):** When a mobile device running the Supervisor loses its Wi-Fi connection, the OS aggressively destroys the network interface, which can crash standard Python sockets. The Supervisor is built with explicit OSError catch blocks to survive this. It will automatically re-bind and recover nodes once Wi-Fi is restored.
 * **Asynchronous Select Loop:** The Python engine uses select.select() with a 0.5-second timeout. If you are dispatching manual UDP commands via a terminal (e.g., using netcat or PacketSender), ensure your tool does not hold the port open and block the supervisor from receiving background alerts.
 * **OTA Partition Mismatches:** When migrating a Node from Arduino IDE to PlatformIO for OTA updates, ensure board_build.partitions = default.csv is set in platformio.ini to prevent flash partition layout conflicts and subsequent boot loops.
