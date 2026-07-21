#include "BleManager.h"
#include <ArduinoJson.h> // We will use JSON to pass {"ssid":"...", "pass":"..."}
#include <Preferences.h>

bool BleManager::deviceConnected = false;
bool BleManager::credentialsReceived = false;
String BleManager::wifiSsid = "";
String BleManager::wifiPassword = "";

void BleManager::ServerCallbacks::onConnect(NimBLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE Client Connected");
    // Change connection parameters for faster transmission
    pServer->updateConnParams(pServer->getPeerIDInfo(0).getConnHandle(), 24, 48, 0, 60);
}

void BleManager::ServerCallbacks::onDisconnect(NimBLEServer* pServer) {
    deviceConnected = false;
    Serial.println("BLE Client Disconnected");
    // Restart advertising
    NimBLEDevice::startAdvertising();
}

void BleManager::CharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    
    if (rxValue.length() > 0) {
        Serial.println("*********");
        Serial.print("Received Value: ");
        
        String receivedStr = "";
        for (int i = 0; i < rxValue.length(); i++) {
            receivedStr += rxValue[i];
        }
        Serial.println(receivedStr);

        // Expecting JSON: {"ssid":"network_name", "pass":"password123"} or {"groq_api_key":"gsk_..."}
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, receivedStr);
        
        if (!error) {
            if (doc.containsKey("ssid") && doc.containsKey("pass")) {
                wifiSsid = doc["ssid"].as<String>();
                wifiPassword = doc["pass"].as<String>();
                credentialsReceived = true;
                Serial.println("Wi-Fi Credentials Parsed Successfully!");
            }
            
            if (doc.containsKey("groq_api_key")) {
                String apiKey = doc["groq_api_key"].as<String>();
                if (apiKey.length() > 0) {
                    extern Preferences preferences;
                    preferences.putString("groq_key", apiKey);
                    Serial.println("Groq API Key Saved to NVS Successfully!");
                }
            }
        } else {
            Serial.println("Failed to parse JSON credentials.");
        }
    }
}

void BleManager::init() {
    NimBLEDevice::init("Robot 2.1");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max power
    
    NimBLEServer* pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    
    NimBLECharacteristic* pCharacteristic = pService->createCharacteristic(
                                                CHARACTERISTIC_UUID,
                                                NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ
                                            );
                                            
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());
    
    pService->start();
    
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // helps with iPhone connection issues
    pAdvertising->setMaxPreferred(0x12);
    NimBLEDevice::startAdvertising();
    
    Serial.println("BLE Provisioning Server Started. Waiting for connections...");
}

void BleManager::stop() {
    NimBLEDevice::deinit();
}

bool BleManager::isConnected() { return deviceConnected; }
bool BleManager::hasNewCredentials() { return credentialsReceived; }
String BleManager::getSsid() { return wifiSsid; }
String BleManager::getPassword() { return wifiPassword; }

void BleManager::clearCredentials() {
    credentialsReceived = false;
    wifiSsid = "";
    wifiPassword = "";
}
