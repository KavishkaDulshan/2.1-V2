#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>

// Standard UUIDs for provisioning
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

class BleManager {
public:
    static void init();
    static void stop();
    static bool isConnected();
    static bool hasNewCredentials();
    static String getSsid();
    static String getPassword();
    static void clearCredentials();

private:
    static bool deviceConnected;
    static bool credentialsReceived;
    static String wifiSsid;
    static String wifiPassword;

    class ServerCallbacks : public NimBLEServerCallbacks {
        void onConnect(NimBLEServer* pServer) override;
        void onDisconnect(NimBLEServer* pServer) override;
    };

    class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
        void onWrite(NimBLECharacteristic* pCharacteristic) override;
    };
};
