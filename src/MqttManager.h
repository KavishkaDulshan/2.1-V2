#pragma once

#include <Arduino.h>

class RobotEyes; // Forward declaration

class MqttManager {
public:
    static void init(RobotEyes* robotEyesInstance);
    static void loop();
    static void publishState(const String& state);

private:
    static void reconnect();
    static void callback(char* topic, byte* payload, unsigned int length);
    static RobotEyes* eyes;
};
