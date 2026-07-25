/*
Version 0.2.0
2026 July 19
*/

#ifndef UDP_NODE_H
#define UDP_NODE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <time.h>
#include <vector>

// Supported parameter datatypes for runtime configuration
enum ParamType {
    PARAM_INT,
    PARAM_FLOAT,
    PARAM_STRING
};

// Structure to bind a command string to a physical runtime variable
struct Parameter {
    String command;
    ParamType type;
    void* varPtr;
    String filename; 
    bool persistent; 
    bool readOnly;   
};

// Structure to hold active tracking targets for the watchdog engine
struct PollTarget {
    int id;
    String ipAddress;
    String deviceName;
    int missedChecks;
    int lastStatus;
};

// Callbacks types
typedef void (*MsgCallback)(int sensorId, int messageId);
typedef void (*CommandCallback)(String key, String value);
typedef void (*LinkCallback)(int sensorId, bool isAlive); 

class UDPNode {
private:
    WiFiUDP _udp;
    unsigned int _localPort;
    
    // Storage Vectors
    std::vector<Parameter> _params;
    std::vector<PollTarget> _pollList;
    std::vector<String> _targetIPs; // Dynamic unicast alert targets
    
    // Callbacks
    MsgCallback _onMessageReceived = nullptr;
    CommandCallback _onCommandReceived = nullptr;
    LinkCallback _onLinkStatusChange = nullptr; 

    // Time & NTP Settings
    const char* _ntpServer = "pool.ntp.org";
    long _gmtOffset_sec = 0;
    int _daylightOffset_sec = 0;

    // Watchdog Timer Engine
    unsigned long _lastPollTime = 0;
    const unsigned long _pollInterval = 10000; 

    // Internal Processing Engines
    void handlePacket(String packetText, IPAddress remoteIP, uint16_t remotePort);
    void reply(IPAddress ip, uint16_t port, String msg);
    
    // Persistence Handlers
    void saveParam(const Parameter& param);
    void loadParam(const Parameter& param);
    void savePollListToFS();
    void loadPollListFromFS();
    void saveIPList();
    void loadIPList();
    
    void pingTargets(); 

public:
    UDPNode(unsigned int port = 8888);
    ~UDPNode();

    // Initialization & Lifecycle
    bool begin(const char* apName = "Supervisor_Node_AP");
    void loop(); 

    // Parameter Registration Engine
    void registerParam(const String& command, int* varPtr, bool persistent = true, bool readOnly = false);
    void registerParam(const String& command, float* varPtr, bool persistent = true, bool readOnly = false);
    void registerParam(const String& command, String* varPtr, bool persistent = true, bool readOnly = false);
    void forceSaveParam(const String& command);

    // Callback Setters
    void onMessage(MsgCallback callback);
    void onCommand(CommandCallback callback);
    void onLinkStatus(LinkCallback callback); 

    // Manual Poll Target Controls
    bool addPollTarget(int id, String ip, String name = "Pending Name");
    bool removePollTarget(int id);

    // Time Utilities
    void configureTime(long gmtOffset_sec, int daylightOffset_sec, const char* ntpServer = "pool.ntp.org");
    String getFormattedTime();
    void sendAlert(const String& alertMessage);
};

#endif