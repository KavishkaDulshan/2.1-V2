#include "MqttManager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "RobotEyes.h"

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
                else eyes->setEmotion(NEUTRAL);
            }
        }
        // Could easily add other commands like {"color": "#FF0000"} here in the future
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
