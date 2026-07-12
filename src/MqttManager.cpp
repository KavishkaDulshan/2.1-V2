#include "MqttManager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "RobotEyes.h"
#include "RobotEyes.h"

extern time_t targetAlarmTime;
extern unsigned long pomodoroEndTime;
extern bool alarmTriggered;
extern String weatherCity;
extern TaskHandle_t weatherTaskHandle;
extern bool weatherTimezoneReady;

// We use a free public MQTT broker for prototyping
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic_command = "robot21/commands/master";
const char* mqtt_topic_state = "robot21/state/master";

WiFiClient espClient;
PubSubClient client(espClient);
RobotEyes* MqttManager::eyes = nullptr;

void MqttManager::init(RobotEyes* robotEyesInstance) {
    eyes = robotEyesInstance;
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(MqttManager::callback);
}

void MqttManager::callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println(message);

    // Expecting JSON like: {"emotion": "angry"}
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (!error) {
        if (doc.containsKey("emotion")) {
            String emotionStr = doc["emotion"].as<String>();
            if (eyes != nullptr) {
                if (emotionStr == "happy") eyes->setEmotion(HAPPY);
                else if (emotionStr == "angry") eyes->setEmotion(ANGRY);
                else if (emotionStr == "sad") eyes->setEmotion(SAD);
                else if (emotionStr == "sleepy") eyes->setEmotion(SLEEPY);
                else if (emotionStr == "dizzy") eyes->setEmotion(DIZZY);
                else if (emotionStr == "panic") eyes->setEmotion(PANIC);
                else if (emotionStr == "innocent") eyes->setEmotion(INNOCENT);
                else if (emotionStr == "clock") { eyes->setEmotion(CLOCK_MODE); eyes->baseEmotion = CLOCK_MODE; }
                else { eyes->setEmotion(NEUTRAL); eyes->baseEmotion = NEUTRAL; }
            }
        }
        
        if (doc.containsKey("mode")) {
            String modeStr = doc["mode"].as<String>();
            if (eyes != nullptr) {
                if (modeStr == "clock") { eyes->setEmotion(CLOCK_MODE); eyes->baseEmotion = CLOCK_MODE; }
                else { eyes->setEmotion(NEUTRAL); eyes->baseEmotion = NEUTRAL; }
            }
        }

        if (doc.containsKey("timer")) {
            int mins = doc["timer"].as<int>();
            if (mins == 0) {
                pomodoroEndTime = 0; // stop timer
                if (eyes != nullptr) eyes->timerActive = false;
            } else {
                pomodoroEndTime = millis() + (mins * 60 * 1000);
            }
        }

        if (doc.containsKey("alarm_h") && doc.containsKey("alarm_m")) {
            int h = doc["alarm_h"].as<int>();
            int m = doc["alarm_m"].as<int>();
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 10)) {
                timeinfo.tm_hour = h;
                timeinfo.tm_min = m;
                timeinfo.tm_sec = 0;
                targetAlarmTime = mktime(&timeinfo);
                
                time_t now;
                time(&now);
                if (targetAlarmTime < now) {
                    targetAlarmTime += 24 * 3600; // Next day if already passed
                }
                alarmTriggered = false;
                Serial.printf("Alarm set for %02d:%02d\n", h, m);
            }
        }
        
        if (doc.containsKey("clock_color")) {
            String hexStr = doc["clock_color"].as<String>();
            if (hexStr.startsWith("#")) hexStr = hexStr.substring(1);
            long rgb = strtol(hexStr.c_str(), NULL, 16);
            uint8_t r = (rgb >> 16) & 0xFF;
            uint8_t g = (rgb >> 8) & 0xFF;
            uint8_t b = rgb & 0xFF;
            if (eyes != nullptr) {
                eyes->clockColor = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                Serial.printf("Clock color updated to: #%06X\n", (uint32_t)rgb);
            }
        }
        
        if (doc.containsKey("city")) {
            weatherCity = doc["city"].as<String>();
            Serial.println("Weather city updated to: " + weatherCity);
            weatherTimezoneReady = false; // Reset until new timezone is fetched
            if (eyes != nullptr) {
                eyes->weatherIcon = "loading";
            }
            if (weatherTaskHandle != NULL) {
                xTaskNotifyGive(weatherTaskHandle);
            }
        }
    } else {
        Serial.println("Failed to parse MQTT JSON");
    }
}

void MqttManager::reconnect() {
    // Loop until we're reconnected, but don't block forever
    if (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = "Robot2.1-Client-";
        clientId += String(random(0xffff), HEX);
        
        if (client.connect(clientId.c_str())) {
            Serial.println("connected");
            client.publish(mqtt_topic_state, "{\"status\": \"online\"}");
            client.subscribe(mqtt_topic_command);
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again later");
        }
    }
}

void MqttManager::loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!client.connected()) {
            reconnect();
        }
        client.loop();
    }
}

void MqttManager::publishState(const String& state) {
    if (client.connected()) {
        client.publish(mqtt_topic_state, state.c_str());
    }
}
