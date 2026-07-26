/**
 * @file main.cpp
 * @brief Clock Supervisor Node Firmware
 * 
 * ESP32-based central clock display and audio supervisor node utilizing 
 * the UDPNode framework and a MAX7219 LED matrix display module.
 * 
 * Features:
 *  - Real-time digital clock display powered by NTP time synchronization
 *  - Non-blocking MAX7219 LED matrix driver integration
 *  - Acts as an in-hardware Clock Supervisor listening for network alert triggers
 *  - Standardized 3-Level Status Taxonomy (Status 2 = Critical Latch)
 *  - On-the-fly timezone, DST, and parameter updates via LittleFS and UDP port 8888
 *  - Integrated Over-The-Air (OTA) flashing support
 * 
 * @version 1.0.0
 * @date July 2026
 * @author Jim McKeown
 * @license MIT License
 */

#include <Arduino.h>
#include "UDPNode.h"
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <time.h>
#include <LittleFS.h>
#include <Audio.h>
#include <queue>

// --- Multilingual Queue Setup ---
std::queue<String> audioQueue;
bool play_en = true;
bool play_th = true;
// --------------------------------

// ---------------------------------------------------------
// Hardware Configuration
// ---------------------------------------------------------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW 
#define MAX_DEVICES 4                     
#define CS_PIN 10 
#define DATA_PIN 11
#define CLK_PIN 12

// I2S Audio Pins
#define I2S_LRC  4
#define I2S_BCLK 5
#define I2S_DOUT 6

// --- Display State Machine ---
std::queue<String> displayQueue; // NEW: Queue for text alerts
enum DisplayMode { MODE_TIME, MODE_SCROLL };
DisplayMode currentDisplayMode = MODE_TIME;
char scrollMessage[128] = ""; 

// UPDATED: Now just pushes to the queue
void triggerScrollMessage(const char* message) {
    displayQueue.push(String(message));
}
// Instantiate Display, Audio, and Network Server
MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
//ParamServer server(8888);
UDPNode udpNode(8888); // Unified engine now listening strictly on port 8888
Audio audio;

// ---------------------------------------------------------
// System Parameters
// ---------------------------------------------------------
int display_brightness = 0;

int last_brightness = -1;  
int volume = 3;              // Master volume for alerts (msg)
int last_volume = -1; 
int tick_volume = 5;         // NEW: Separate volume for background ticks
int last_tick_volume = 5;    // NEW: Tracker for live updates
bool isPlayingTick = false;  // NEW: State flag for the audio engine
int last_second = -1;        // Tracks real-time second rollovers

void queueAudioMessages(int messageId) {
    char fileNumber[4];
    sprintf(fileNumber, "%03d", messageId); // Converts 1 to "001"
    String baseName = String(fileNumber) + ".wav";

    // Push requested languages to the queue
    if (play_en) audioQueue.push("/en/" + baseName);
    if (play_th) audioQueue.push("/th/" + baseName);

    Serial.printf("Queued %d files for message ID: %03d\n", audioQueue.size(), messageId);
}

// Callback for specific audio messages
void handleAudioMessage(int sensorId, int messageId) {
    Serial.printf("Trigger Received! Sensor: %02d | Message: %02d\n", sensorId, messageId);

    // Format the filename to be exactly 4 digits: [Sensor 2-digits][Message 2-digits]
    char baseFileName[16];
    sprintf(baseFileName, "%02d%02d.wav", sensorId, messageId);

    Serial.printf("Queued 2 files for target: %s\n", baseFileName);

    // Push the newly formatted file paths into the queue
    if (play_en) {
        audioQueue.push("/en/" + String(baseFileName));
    }
    if (play_th) {
        audioQueue.push("/th/" + String(baseFileName));
    }
}

// Callback for generic parameters
void handleGenericCommand(String key, String value) {
    if (key == "volume") {
        volume = value.toInt(); 
        Serial.println("UDPNode: Volume updated to " + String(volume));
    } 
    else if (key == "bright") {
        display_brightness = value.toInt(); 
        Serial.println("UDPNode: Brightness updated to " + String(display_brightness));
    }
    else if (key == "alert") {
        triggerScrollMessage(value.c_str());
    }    
    else if (key == "english") {
         
        if(value == "true") {
            play_en = true;
        }
        if(value == "false") {
            play_en = false;
        }
        Serial.println("UDPNode: English language updated to " + value);
    }
    else if (key == "thai") {
         
        if(value == "true") {
            play_th = true;
        }
        if(value == "false") {
            play_th = false;
        }
        Serial.println("UDPNode: Thai language updated to " + value);
    }        
}

// --- NEW: Callback for Link Status Changes ---
void handleLinkStatus(int sensorId, bool isAlive) {
    char alertMsg[64];
    if (!isAlive) {
        sprintf(alertMsg, "SENSOR %02d MISSED CHECK-IN", sensorId);
        Serial.println(alertMsg);
        triggerScrollMessage(alertMsg); // Triggers the MAX7219 scroll
        // queueAudioMessages(3);       // Queue "Supervision Lost" audio
    } else {
        sprintf(alertMsg, "SENSOR %02d RESTORED", sensorId);
        Serial.println(alertMsg);
        triggerScrollMessage(alertMsg);
        // queueAudioMessages(4);       // Queue "Network Restored" audio
    }
}

void setup() {
    Serial.begin(115200);

    // Audio Pin configurations (Keep your current layout)
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(15); 

    // 2. Register variables directly into the UDPNode parameter system
    udpNode.registerParam("bright", &display_brightness);
    udpNode.registerParam("volume", &volume);   
    udpNode.registerParam("tick_vol", &tick_volume); // NEW: Network control for ticks 

    // Initialize MAX7219 Display
    myDisplay.begin();
    myDisplay.setIntensity(display_brightness);
    myDisplay.displayClear();
    myDisplay.setTextAlignment(PA_CENTER);

    // 3. Pass timezone configurations straight to the unified node
    // UTC+7 (25200 seconds), 0 DST
    udpNode.configureTime(25200, 0, "pool.ntp.org");

    // 4. Boot Network Engine. This single call mounts LittleFS,
    // checks WiFiManager credentials, syncs time, and starts listening.
    if (!udpNode.begin("Clock_Supervisor_AP")) {
        Serial.println("Network Core Failure");
        myDisplay.print("NET ERR");
        while(1);
    }

    // 5. Attach the functional queues and timers
    udpNode.onMessage(handleAudioMessage);
    udpNode.onCommand(handleGenericCommand);
    udpNode.onLinkStatus(handleLinkStatus);    
}

void loop() {
    // 1. Maintain Network & UDP Listeners
    //server.update();
    udpNode.loop();

    // 2. Process Audio DMA Buffer (Non-Blocking)
    audio.loop();

    // 3. Process the Audio Queue (For Alerts/Messages)
    if (!audio.isRunning() && !audioQueue.empty()) {
        String nextFile = audioQueue.front();
        audioQueue.pop(); 

        if (LittleFS.exists(nextFile)) {
            Serial.println("Now playing: " + nextFile);
            isPlayingTick = false;          // Set state to Message
            audio.setVolume(volume);        // Crank up to Alert Volume
            audio.connecttoFS(LittleFS, nextFile.c_str());
        } else {
            Serial.println("Error: File not found - " + nextFile);
        }
    }

    // 3.5. Apply Volume Updates Dynamically (Live tweaking)
    volume = constrain(volume, 0, 21);
    tick_volume = constrain(tick_volume, 0, 21);

    if (isPlayingTick) {
        if (tick_volume != last_tick_volume) {
            audio.setVolume(tick_volume);
            last_tick_volume = tick_volume;
        }
    } else {
        if (volume != last_volume) {
            audio.setVolume(volume);
            last_volume = volume;
        }
    }

    // 4. Apply Brightness Updates Dynamically
    if (display_brightness != last_brightness) {
        display_brightness = constrain(display_brightness, 0, 21);
        myDisplay.setIntensity(display_brightness);
        last_brightness = display_brightness;
    }

    // 5. Apply Volume Updates Dynamically
    if (volume != last_volume) {
        volume = constrain(volume, 0, 21);
        audio.setVolume(volume);
        last_volume = volume;
    }

    // 6. Display State Machine & Clock Ticks
    if (currentDisplayMode == MODE_SCROLL) {
        // displayAnimate() returns true when the current scroll is finished
        if (myDisplay.displayAnimate()) { 
            if (!displayQueue.empty()) {
                // Instantly load and scroll the next queued text alert
                String nextMsg = displayQueue.front();
                displayQueue.pop();
                strncpy(scrollMessage, nextMsg.c_str(), sizeof(scrollMessage) - 1);
                scrollMessage[sizeof(scrollMessage) - 1] = '\0';
                
                myDisplay.displayText(scrollMessage, PA_CENTER, 40, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
            } else {
                // Text queue is empty. Reset to time mode.
                currentDisplayMode = MODE_TIME;
                myDisplay.displayClear();
                last_second = -1; // Force immediate time redraw
            }
        }
    } 
    else if (currentDisplayMode == MODE_TIME) {
        
        // If a new text alert arrived while we were displaying the time
        if (!displayQueue.empty()) {
            String nextMsg = displayQueue.front();
            displayQueue.pop();
            strncpy(scrollMessage, nextMsg.c_str(), sizeof(scrollMessage) - 1);
            scrollMessage[sizeof(scrollMessage) - 1] = '\0';
            
            currentDisplayMode = MODE_SCROLL;
            myDisplay.displayText(scrollMessage, PA_CENTER, 40, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        } else {
            
            // Exact-Second Synchronization Trigger
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 10)) { 
                if (timeinfo.tm_sec != last_second) {
                    last_second = timeinfo.tm_sec;
                    
                    // --- Audio Tick Trigger ---
                    // ONLY play ticks if no alerts are playing and the audio queue is empty
                    if (!audio.isRunning() && audioQueue.empty()) {
                        
                        isPlayingTick = true;          // Set state to Tick
                        audio.setVolume(tick_volume);  // Drop down to Tick Volume
                        
                        if (last_second == 0) {
                            audio.connecttoFS(LittleFS, "/minute.wav");
                        } else {
                            audio.connecttoFS(LittleFS, "/second.wav");
                        }
                    }
                    
                    // --- Visual Time Trigger ---
                    char timeString[6];
                    if (last_second % 2 == 0) {
                        strftime(timeString, sizeof(timeString), "%H:%M", &timeinfo);
                    } else {
                        strftime(timeString, sizeof(timeString), "%H %M", &timeinfo);
                    }
                    myDisplay.print(timeString);
                }
            } else {
                // Fallback if NTP sync drops
                static uint32_t lastFallbackUpdate = 0;
                if (millis() - lastFallbackUpdate > 1000) {
                    lastFallbackUpdate = millis();
                    myDisplay.print("SYNC...");
                }
            }
        }
    }
    
    yield(); 
}
