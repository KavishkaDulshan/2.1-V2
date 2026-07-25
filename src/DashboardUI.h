#ifndef DASHBOARD_UI_H
#define DASHBOARD_UI_H

#include <Arduino.h>
#include <LovyanGFX.hpp>

class DashboardUI {
public:
    DashboardUI();
    
    // Initialize with the main display pointer
    void init(lgfx::LGFX_Device* display);
    
    // Update logic for touch interactions
    // Returns true if the UI was updated/touched
    bool update(bool isTouched, int16_t touchX, int16_t touchY);
    
    // Draw the dashboard to the screen
    void draw();
    
    // Reset state when opening
    void reset();
    
    // Check if the user wants to close the dashboard
    bool wantsToClose() const { return _wantsToClose; }

    // Setters for external state
    void setBrightness(uint8_t b) { _brightness = b; _needsRedraw = true; }
    void setVolume(uint8_t v) { _volume = v; _needsRedraw = true; }
    void setWifiStatus(bool connected, String ssid) { _wifiConnected = connected; _wifiSsid = ssid; _needsRedraw = true; }
    void setApiStatus(bool hasKey) { _hasApiKey = hasKey; _needsRedraw = true; }
    void setBatteryLevel(uint8_t percent) { _batteryPercent = percent; _needsRedraw = true; }

    // Getters for applied state changes
    uint8_t getBrightness() const { return _brightness; }
    uint8_t getVolume() const { return _volume; }

private:
    lgfx::LGFX_Device* _display;
    LGFX_Sprite* _sprite;
    
    bool _needsRedraw;
    bool _wantsToClose;
    
    // States
    uint8_t _brightness;
    uint8_t _volume;
    bool _wifiConnected;
    String _wifiSsid;
    bool _hasApiKey;
    uint8_t _batteryPercent;
    
    // Touch Interaction state
    bool _wasTouched;
    int16_t _lastTouchX;
    int16_t _lastTouchY;
    
    // Slider states
    bool _isDraggingBrightness;
    bool _isDraggingVolume;

    // Helper functions for drawing
    void drawTile(int x, int y, int w, int h, uint16_t color, String label, String value, bool highlight = false);
    void drawSlider(int x, int y, int w, int h, uint16_t color, String label, float percentage);
    void drawCloseButton();
};

#endif // DASHBOARD_UI_H
