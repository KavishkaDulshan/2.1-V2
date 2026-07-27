#include "DashboardUI.h"
#include <WiFi.h>

// Color Palette
#define COLOR_BG      0x18E3 // Dark Gray/Blue background
#define COLOR_TILE    0x2124 // Lighter Gray for tiles
#define COLOR_ACCENT  0x03FF // Cyan highlight
#define COLOR_WARN    0xF800 // Red for errors/disconnects
#define COLOR_SUCCESS 0x07E0 // Green for connected/OK
#define COLOR_TEXT    0xFFFF // White

DashboardUI::DashboardUI() {
    _display = nullptr;
    _sprite = nullptr;
    _needsRedraw = true;
    _wantsToClose = false;
    
    _brightness = 128;
    _volume = 50;
    _wifiConnected = false;
    _wifiSsid = "Disconnected";
    _hasApiKey = false;
    _batteryPercent = 100;
    
    _wasTouched = false;
    _isDraggingBrightness = false;
    _isDraggingVolume = false;
}

void DashboardUI::init(lgfx::LGFX_Device* display) {
    _display = display;
    _sprite = new LGFX_Sprite(_display);
    
    // 240x320 full screen sprite
    _sprite->setPsram(true);
    _sprite->createSprite(240, 320);
    _sprite->setTextDatum(middle_center);
}

void DashboardUI::reset(bool isTouched, int16_t touchX, int16_t touchY) {
    _wantsToClose = false;
    _needsRedraw = true;
    _wasTouched = isTouched;
    _lastTouchX = touchX;
    _lastTouchY = touchY;
    _isDraggingBrightness = false;
    _isDraggingVolume = false;
}

bool DashboardUI::update(bool isTouched, int16_t touchX, int16_t touchY) {
    bool updated = false;

    if (isTouched) {
        if (!_wasTouched) {
            // Just touched down
            _lastTouchX = touchX;
            _lastTouchY = touchY;
            _wasTouched = true;
            
            // Check Close button (Top right corner, 40x40 area)
            if (touchX > 200 && touchY < 40) {
                _wantsToClose = true;
                return true;
            }
            
            // Check Brightness Slider (Y: 50-90)
            if (touchY > 50 && touchY < 90) {
                _isDraggingBrightness = true;
            }
            // Check Volume Slider (Y: 100-140)
            else if (touchY > 100 && touchY < 140) {
                _isDraggingVolume = true;
            }
            
            // Check Tile Buttons
            if (touchY > 150 && touchY < 230) {
                if (touchX < 120) {
                    // Wi-Fi Tile tapped
                    WiFi.reconnect();
                }
            } else if (touchY > 240 && touchY < 320) {
                if (touchX < 120) {
                    // Reboot Tile tapped
                    ESP.restart();
                } else {
                    // Sleep Tile tapped (Placeholder for sleep mode)
                    _wantsToClose = true;
                }
            }
        }
        
        // Handle dragging
        if (_isDraggingBrightness) {
            int mappedX = constrain(touchX - 20, 0, 200);
            uint8_t newB = (uint8_t)((mappedX / 200.0f) * 255.0f);
            if (newB != _brightness) {
                _brightness = newB;
                _display->setBrightness(_brightness); // Apply immediately
                _needsRedraw = true;
                updated = true;
            }
        }
        else if (_isDraggingVolume) {
            int mappedX = constrain(touchX - 20, 0, 200);
            uint8_t newV = (uint8_t)((mappedX / 200.0f) * 100.0f);
            if (newV != _volume) {
                _volume = newV;
                _needsRedraw = true;
                updated = true;
            }
        }
        else {
            // Detect right-to-left swipe to close
            if (_wasTouched && (touchX - _lastTouchX < -50)) {
                _wantsToClose = true;
                updated = true;
            }
        }
    } else {
        _wasTouched = false;
        _isDraggingBrightness = false;
        _isDraggingVolume = false;
    }
    
    return updated;
}

void DashboardUI::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    
    _sprite->fillSprite(COLOR_BG);
    
    // Header
    _sprite->setTextSize(1.5);
    _sprite->setTextColor(COLOR_TEXT);
    _sprite->setTextDatum(middle_center);
    _sprite->drawString("CONTROL CENTER", 120, 20);
    
    // Close button
    drawCloseButton();
    
    // Sliders
    drawSlider(20, 50, 200, 40, COLOR_ACCENT, "BRIGHTNESS", _brightness / 255.0f);
    drawSlider(20, 100, 200, 40, 0xFCA0, "VOLUME", _volume / 100.0f); // Orange for volume
    
    // Grid Tiles (2x2)
    // Row 1
    uint16_t wifiColor = _wifiConnected ? COLOR_SUCCESS : COLOR_WARN;
    drawTile(10, 150, 105, 80, wifiColor, "Wi-Fi", _wifiConnected ? _wifiSsid : "Offline");
    
    uint16_t apiColor = _hasApiKey ? COLOR_SUCCESS : COLOR_WARN;
    drawTile(125, 150, 105, 80, apiColor, "API Key", _hasApiKey ? "Ready" : "Missing");
    
    // Row 2
    drawTile(10, 240, 105, 70, COLOR_WARN, "System", "REBOOT");
    drawTile(125, 240, 105, 70, COLOR_TILE, "Battery", String(_batteryPercent) + "%");
    
    // Push to display
    _sprite->pushSprite(0, 0);
}

void DashboardUI::drawTile(int x, int y, int w, int h, uint16_t color, String label, String value, bool highlight) {
    _sprite->fillRoundRect(x, y, w, h, 8, COLOR_TILE);
    
    if (highlight) {
        _sprite->drawRoundRect(x, y, w, h, 8, color);
    }
    
    _sprite->setTextSize(1);
    _sprite->setTextColor(color);
    _sprite->drawString(label, x + (w/2), y + 25);
    
    _sprite->setTextColor(COLOR_TEXT);
    _sprite->drawString(value, x + (w/2), y + 50);
}

void DashboardUI::drawSlider(int x, int y, int w, int h, uint16_t color, String label, float percentage) {
    _sprite->fillRoundRect(x, y, w, h, h/2, COLOR_TILE);
    
    int fillW = (int)(w * percentage);
    if (fillW > h) {
        _sprite->fillRoundRect(x, y, fillW, h, h/2, color);
    } else if (fillW > 0) {
        _sprite->fillCircle(x + h/2, y + h/2, h/2, color);
    }
    
    _sprite->setTextSize(1);
    _sprite->setTextColor(COLOR_TEXT);
    _sprite->drawString(label, x + (w/2), y + (h/2));
}

void DashboardUI::drawCloseButton() {
    _sprite->fillCircle(215, 20, 15, COLOR_TILE);
    _sprite->setTextColor(COLOR_TEXT);
    _sprite->drawString("X", 215, 20);
}
