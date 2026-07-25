/*
Version 0.2.0
2026 July 19
*/

#include "UDPNode.h"
#include <WiFiManager.h>

UDPNode::UDPNode(unsigned int port) : _localPort(port) {}
UDPNode::~UDPNode() {}

bool UDPNode::begin(const char* apName) {
    // 1. Fire up the File System
    if (!LittleFS.begin(true)) {
        Serial.println("[-] LittleFS Mount Failed");
        return false;
    }

    // 2. Manage Captive Portal WiFi Connection
    WiFiManager wm;
    wm.setConnectTimeout(15); 
    
    Serial.println("[*] Connecting to WiFi...");
    if (!wm.autoConnect(apName)) {
        Serial.println("[-] WiFi Connection timeout reached. Restarting Portal...");
        ESP.restart();
    }
    WiFi.setSleep(false); 
    Serial.print("[+] Connected! IP address: ");
    Serial.println(WiFi.localIP());

    // 3. Sync System Clock with NTP
    configTime(_gmtOffset_sec, _daylightOffset_sec, _ntpServer);
    
    // 4. Load Registered Parameters from Flash
    for (const auto& param : _params) {
        if (param.persistent) {
            loadParam(param);
        }
    }

    // 5. Load Watchdog Lists from Flash
    loadPollListFromFS();
    loadIPList();

    // 6. Bind Socket
    _udp.begin(_localPort);
    Serial.printf("[+] UDP Node listening on single port %d\n", _localPort);
    return true;
}

void UDPNode::loop() {
    // Watchdog Outgoing Timer (Every 10 seconds)
    if (millis() - _lastPollTime >= _pollInterval) {
        _lastPollTime = millis();
        pingTargets();
    }

    // Handle Incoming UDP Traffic
    int packetSize = _udp.parsePacket();
    if (packetSize) {
        String remoteIP = _udp.remoteIP().toString();
        
        // Watchdog Activity Reset: Any traffic from an IP proves it's alive
        for (auto& target : _pollList) {
            if (target.ipAddress == remoteIP) {
                if (target.missedChecks >= 3) {
                    if (_onLinkStatusChange) _onLinkStatusChange(target.id, true); 
                }
                target.missedChecks = 0; 
                break;
            }
        }

        char packetBuffer[255];
        int len = _udp.read(packetBuffer, 254);
        if (len > 0) packetBuffer[len] = 0;
        
        String request = String(packetBuffer);
        request.trim();
        handlePacket(request, _udp.remoteIP(), _udp.remotePort());
    }
}

void UDPNode::handlePacket(String packetText, IPAddress remoteIP, uint16_t remotePort) {
    int spaceIndex = packetText.indexOf(' ');
    String cmd = (spaceIndex == -1) ? packetText : packetText.substring(0, spaceIndex);
    String valStr = (spaceIndex == -1) ? "" : packetText.substring(spaceIndex + 1);
    cmd.trim(); valStr.trim();

    static String staged_ssid = "";
    static String staged_pass = "";    

    // Automatically add interacting nodes to baseline communication list
    if (cmd != "status ?" && cmd != "list_poll") {
        // Deduplicate and track target IPs for general alerts if necessary
        bool known = false;
        for (const auto& ip : _targetIPs) { if (ip == remoteIP.toString()) known = true; }
        if (!known && remoteIP.toString() != "0.0.0.0") {
            _targetIPs.push_back(remoteIP.toString());
            saveIPList();
        }
    }

    // --- Command Group A: Polling Engine Commands ---
    if (cmd == "list_poll") {
        reply(remoteIP, remotePort, _pollList.empty() ? "Poll list empty" : "");
        for (const auto& t : _pollList) {
            reply(remoteIP, remotePort, String(t.id) + "," + t.ipAddress + "," + t.deviceName + "\n");
        }
        return;
    }

    if (cmd == "add_poll") {
        int valSpaceIdx = valStr.indexOf(' ');
        if (valSpaceIdx > 0) {
            int id = valStr.substring(0, valSpaceIdx).toInt();
            String ip = valStr.substring(valSpaceIdx + 1);
            ip.trim();
            addPollTarget(id, ip);
            reply(remoteIP, remotePort, "OK: Added poll target " + String(id));
        } else {
            reply(remoteIP, remotePort, "ERR: Expected 'add_poll ID IP'");
        }
        return;
    }

    // --- Command Group B: Audio Message Engine ---
    if (cmd == "msg") {
        if (valStr == "?") {
            reply(remoteIP, remotePort, "ERR: msg is write-only");
            return;
        }
        int fullId = valStr.toInt();
        if (fullId > 0 && _onMessageReceived != nullptr) {
            int sensorId = fullId / 100;
            int messageId = fullId % 100;
            _onMessageReceived(sensorId, messageId);
            reply(remoteIP, remotePort, "OK: Queued msg " + valStr);
        }
        return; 
    }

    // --- Command Group C: Legacy Network / WiFi Admin ---
    if (cmd.equalsIgnoreCase("commit_wifi")) {
        if (staged_ssid == "") {
            reply(remoteIP, remotePort, "OK clearing_credentials_and_opening_portal");
            delay(1000); WiFiManager wm; wm.resetSettings(); ESP.restart();
            return;
        } else {
            reply(remoteIP, remotePort, "OK migrating_to_new_network: " + staged_ssid);
            delay(1000); WiFi.begin(staged_ssid.c_str(), staged_pass.c_str());
            delay(2000); ESP.restart(); return;
        }
    }
    if (cmd.equalsIgnoreCase("new_ssid")) { staged_ssid = valStr; reply(remoteIP, remotePort, "OK SSID staged"); return; }
    if (cmd.equalsIgnoreCase("new_pass")) { staged_pass = valStr; reply(remoteIP, remotePort, "OK Pass staged"); return; }
    if (cmd.equalsIgnoreCase("forget_wifi")) { reply(remoteIP, remotePort, "OK resetting"); delay(1000); WiFiManager wm; wm.resetSettings(); ESP.restart(); return; }

    // --- Command Group D: Unified Core Parameter Get / Set Engine ---
    for (auto& param : _params) {
        if (param.command.equalsIgnoreCase(cmd)) {
            if (valStr == "?") {
                String response = "OK " + param.command + " ";
                if (param.type == PARAM_INT) response += *(int*)param.varPtr;
                if (param.type == PARAM_FLOAT) response += String(*(float*)param.varPtr, 4);
                if (param.type == PARAM_STRING) response += *(String*)param.varPtr;
                reply(remoteIP, remotePort, response);
                return;
            }

            if (param.readOnly) {
                reply(remoteIP, remotePort, "ERR " + param.command + " is read-only");
                return;
            }

            if (param.type == PARAM_INT) *(int*)param.varPtr = valStr.toInt();
            else if (param.type == PARAM_FLOAT) *(float*)param.varPtr = valStr.toFloat();
            else if (param.type == PARAM_STRING) *(String*)param.varPtr = valStr;

            if (param.persistent) saveParam(param); 
            reply(remoteIP, remotePort, "OK " + param.command + " " + valStr);
            return;
        }
    }

    // --- Fallback Group E: Forward custom strings to the main sketch ---
    if (_onCommandReceived != nullptr) {
        _onCommandReceived(cmd, valStr);
    } else {
        reply(remoteIP, remotePort, "ERR Unknown command");
    }
}

void UDPNode::pingTargets() {
    for (auto& target : _pollList) {
        target.missedChecks++;
        if (target.missedChecks == 3 && _onLinkStatusChange) {
            _onLinkStatusChange(target.id, false); 
        }
        _udp.beginPacket(target.ipAddress.c_str(), 8888); // Pings sensors on single standard port
        _udp.print("status ?");
        _udp.endPacket();
    }
}

// Parameter Binding Overloads
void UDPNode::registerParam(const String& command, int* varPtr, bool persistent, bool readOnly) { _params.push_back({command, PARAM_INT, varPtr, "/" + command + ".txt", persistent, readOnly}); }
void UDPNode::registerParam(const String& command, float* varPtr, bool persistent, bool readOnly) { _params.push_back({command, PARAM_FLOAT, varPtr, "/" + command + ".txt", persistent, readOnly}); }
void UDPNode::registerParam(const String& command, String* varPtr, bool persistent, bool readOnly) { _params.push_back({command, PARAM_STRING, varPtr, "/" + command + ".txt", persistent, readOnly}); }

// Callback wire attachments
void UDPNode::onMessage(MsgCallback callback) { _onMessageReceived = callback; }
void UDPNode::onCommand(CommandCallback callback) { _onCommandReceived = callback; }
void UDPNode::onLinkStatus(LinkCallback callback) { _onLinkStatusChange = callback; }

void UDPNode::reply(IPAddress ip, uint16_t port, String msg) { if(msg.length() == 0) return; _udp.beginPacket(ip, port); _udp.print(msg); _udp.endPacket(); }
void UDPNode::configureTime(long gmtOffset_sec, int daylightOffset_sec, const char* ntpServer) { _gmtOffset_sec = gmtOffset_sec; _daylightOffset_sec = daylightOffset_sec; _ntpServer = ntpServer; }

String UDPNode::getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return "NTP_ERR_TIME_NOT_SET";
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

void UDPNode::sendAlert(const String& alertMessage) {
    String formattedMsg = "[" + getFormattedTime() + "] ALERT: " + alertMessage;
    for (const String& ipStr : _targetIPs) {
        IPAddress target;
        if (target.fromString(ipStr)) { _udp.beginPacket(target, 9999); _udp.print(formattedMsg); _udp.endPacket(); }
    }
}

bool UDPNode::addPollTarget(int id, String ip, String name) {
    for (auto& target : _pollList) {
        if (target.id == id) { target.ipAddress = ip; target.deviceName = name; savePollListToFS(); return true; }
    }
    _pollList.push_back({id, ip, name, 0, -1});
    savePollListToFS();
    return true;
}

// File System Disk Operations
void UDPNode::saveParam(const Parameter& param) { File file = LittleFS.open(param.filename, "w"); if (!file) return; if (param.type == PARAM_INT) file.print(*(int*)param.varPtr); if (param.type == PARAM_FLOAT) file.print(*(float*)param.varPtr, 6); if (param.type == PARAM_STRING) file.print(*(String*)param.varPtr); file.close(); }
void UDPNode::loadParam(const Parameter& param) { if (!LittleFS.exists(param.filename)) return; File file = LittleFS.open(param.filename, "r"); if (!file) return; String content = file.readString(); content.trim(); file.close(); if (param.type == PARAM_INT) *(int*)param.varPtr = content.toInt(); if (param.type == PARAM_FLOAT) *(float*)param.varPtr = content.toFloat(); if (param.type == PARAM_STRING) *(String*)param.varPtr = content; }
void UDPNode::savePollListToFS() { File file = LittleFS.open("/poll_list.txt", "w"); if (!file) return; for (const auto& t : _pollList) { file.printf("%02d,%s,%s\n", t.id, t.ipAddress.c_str(), t.deviceName.c_str()); } file.close(); }
void UDPNode::loadPollListFromFS() { _pollList.clear(); File file = LittleFS.open("/poll_list.txt", "r"); if (!file) return; while (file.available()) { String line = file.readStringUntil('\n'); line.trim(); if (line.length() == 0) continue; int first = line.indexOf(','); int second = line.indexOf(',', first + 1); if (first > 0 && second > 0) { _pollList.push_back({line.substring(0, first).toInt(), line.substring(first + 1, second), line.substring(second + 1), 0, -1}); } } file.close(); }
void UDPNode::saveIPList() { File file = LittleFS.open("/iplist.cfg", "w"); if (!file) return; for (const auto& ip : _targetIPs) file.println(ip); file.close(); }
void UDPNode::loadIPList() { if (!LittleFS.exists("/iplist.cfg")) return; File file = LittleFS.open("/iplist.cfg", "r"); if (!file) return; while (file.available()) { String ip = file.readStringUntil('\n'); ip.trim(); if (ip.length() >= 7) _targetIPs.push_back(ip); } file.close(); }
void UDPNode::forceSaveParam(const String& command) { for (const auto& param : _params) { if (param.command.equalsIgnoreCase(command)) { if (param.persistent && !param.readOnly) saveParam(param); return; } } }