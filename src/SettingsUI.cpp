#include "SettingsUI.h"

#define COLOR_BG      0x0821 // Very dark blue/black
#define COLOR_TILE    0x18E3 // Navy tile
#define COLOR_ACCENT  0x03FF // Cyan highlight
#define COLOR_TEXT    0xFFFF // White
#define COLOR_DIM     0x7BEF // Gray text
#define COLOR_TOGGLE_ON 0x07E0 // Green

extern time_t targetAlarmTime;
extern unsigned long pomodoroEndTime;

SettingsUI::SettingsUI() {
    _display = nullptr;
    _sprite = nullptr;
    _prefs = nullptr;
    _needsRedraw = true;
    _wantsToClose = false;
    _currentMenu = LEVEL_GRID;
    
    _isClockMode = false;
    _sbEnable = false;
    _sbWifi = false;
    _sbTime = false;
    _notificationsEnabled = true;
    
    _timerMinutes = 25;
    _alarmHour = 7;
    _alarmMinute = 0;
    _isSettingAlarm = false;
    
    _wasTouched = false;
    _drumScrollY = 0;
    _drumVelocity = 0;
    _drumDragging = false;
    _drumSelectedIndex = 0;
    _drumMaxIndex = 60;
}

void SettingsUI::init(lgfx::LGFX_Device* display, Preferences* prefs) {
    _display = display;
    _prefs = prefs;
    
    _sprite = new LGFX_Sprite(_display);
    _sprite->setPsram(true);
    _sprite->createSprite(240, 320);
    _sprite->setTextDatum(middle_center);
    
    if (_prefs) {
        _sbEnable = _prefs->getBool("sb_en", false);
        _sbWifi = _prefs->getBool("sb_wifi", false);
        _sbTime = _prefs->getBool("sb_time", false);
        _notificationsEnabled = _prefs->getBool("notif_en", true);
    }
}

void SettingsUI::reset() {
    _wantsToClose = false;
    _needsRedraw = true;
    _wasTouched = false;
    _currentMenu = LEVEL_GRID;
    _drumScrollY = 0;
    _drumVelocity = 0;
    _drumDragging = false;
}

void SettingsUI::saveBool(const char* key, bool value) {
    if (_prefs) _prefs->putBool(key, value);
}

bool SettingsUI::update(bool isTouched, int16_t touchX, int16_t touchY) {
    bool updated = false;
    static int16_t firstTouchX = 0;
    static int16_t firstTouchY = 0;
    
    // Drum momentum (continuous)
    if (_currentMenu == LEVEL_TIMERS && !_drumDragging && abs(_drumVelocity) > 0.1f) {
        _drumScrollY += _drumVelocity;
        _drumVelocity *= 0.85f;
        
        float itemHeight = 40.0f;
        float maxScroll = _drumMaxIndex * itemHeight;
        
        if (_drumScrollY < 0) {
            _drumScrollY += (0 - _drumScrollY) * 0.2f;
            _drumVelocity *= 0.5f;
        } else if (_drumScrollY > maxScroll) {
            _drumScrollY += (maxScroll - _drumScrollY) * 0.2f;
            _drumVelocity *= 0.5f;
        } else if (abs(_drumVelocity) < 1.0f) {
            int targetIndex = round(_drumScrollY / itemHeight);
            float targetY = targetIndex * itemHeight;
            _drumScrollY += (targetY - _drumScrollY) * 0.2f;
            _drumSelectedIndex = targetIndex;
            
            if (_isSettingAlarm) {
                // Assuming setting minutes for simplicity in this prototype
                _alarmMinute = _drumSelectedIndex;
            } else {
                _timerMinutes = _drumSelectedIndex;
            }
        }
        _needsRedraw = true;
        updated = true;
    }

    if (isTouched) {
        if (!_wasTouched) {
            _wasTouched = true;
            firstTouchX = touchX;
            firstTouchY = touchY;
            _lastTouchX = touchX;
            _lastTouchY = touchY;
            
            // Check Header Back/Close button
            if (touchY < 40 && touchX < 60) {
                if (_currentMenu == LEVEL_GRID) _wantsToClose = true;
                else { _currentMenu = LEVEL_GRID; _needsRedraw = true; }
                return true;
            }
            
            if (_currentMenu == LEVEL_TIMERS && touchY > 80 && touchY < 240) {
                _drumDragging = true;
                _drumVelocity = 0;
            }
        } else {
            // Drag
            if (_drumDragging) {
                float dy = _lastTouchY - touchY;
                _drumScrollY += dy;
                _drumVelocity = dy * 0.5f;
                _lastTouchY = touchY;
                _needsRedraw = true;
                updated = true;
            } else if (touchX - _lastTouchX > 60) { // Right swipe to go back
                if (_currentMenu == LEVEL_GRID) _wantsToClose = true;
                else { _currentMenu = LEVEL_GRID; _needsRedraw = true; }
                _wasTouched = false;
                return true;
            }
            // Update last touch while dragging normally
            _lastTouchX = touchX;
            _lastTouchY = touchY;
        }
    } else {
        if (_wasTouched) {
            _wasTouched = false;
            // Tap detection (compare to initial touch down point)
            if (!_drumDragging && abs(_lastTouchX - firstTouchX) < 15 && abs(_lastTouchY - firstTouchY) < 15) {
                switch (_currentMenu) {
                    case LEVEL_GRID: handleGridTouch(firstTouchX, firstTouchY); break;
                    case LEVEL_DISPLAY: handleDisplayTouch(firstTouchX, firstTouchY); break;
                    case LEVEL_STATUS_BAR: handleStatusBarTouch(firstTouchX, firstTouchY); break;
                    case LEVEL_NOTIFICATIONS: handleNotificationsTouch(firstTouchX, firstTouchY); break;
                    case LEVEL_TIMERS: handleTimersTouch(firstTouchX, firstTouchY); break;
                }
            }
            _drumDragging = false;
        }
    }
    
    return updated;
}

void SettingsUI::handleGridTouch(int16_t x, int16_t y) {
    if (y > 60 && y < 140) {
        if (x < 120) _currentMenu = LEVEL_DISPLAY;
        else _currentMenu = LEVEL_STATUS_BAR;
    } else if (y > 150 && y < 230) {
        if (x < 120) _currentMenu = LEVEL_NOTIFICATIONS;
        else {
            _currentMenu = LEVEL_TIMERS;
            _drumMaxIndex = 60; // 60 minutes max
            _drumScrollY = _timerMinutes * 40.0f; // Snap to current
        }
    }
    _needsRedraw = true;
}

void SettingsUI::handleDisplayTouch(int16_t x, int16_t y) {
    if (y > 60 && y < 120) { _isClockMode = true; _needsRedraw = true; }
    else if (y > 130 && y < 190) { _isClockMode = false; _needsRedraw = true; }
}

void SettingsUI::handleStatusBarTouch(int16_t x, int16_t y) {
    if (y > 60 && y < 110) { _sbEnable = !_sbEnable; saveBool("sb_en", _sbEnable); }
    else if (y > 120 && y < 170) { _sbWifi = !_sbWifi; saveBool("sb_wifi", _sbWifi); }
    else if (y > 180 && y < 230) { _sbTime = !_sbTime; saveBool("sb_time", _sbTime); }
    _needsRedraw = true;
}

void SettingsUI::handleNotificationsTouch(int16_t x, int16_t y) {
    if (y > 60 && y < 110) { 
        _notificationsEnabled = !_notificationsEnabled; 
        saveBool("notif_en", _notificationsEnabled); 
        _needsRedraw = true;
    }
}

void SettingsUI::handleTimersTouch(int16_t x, int16_t y) {
    // Start button
    if (y > 250 && y < 300) {
        pomodoroEndTime = millis() + (_timerMinutes * 60 * 1000);
        _currentMenu = LEVEL_GRID; // Go back after starting
        _needsRedraw = true;
    }
}

void SettingsUI::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    
    _sprite->fillSprite(COLOR_BG);
    
    switch (_currentMenu) {
        case LEVEL_GRID: drawGrid(); break;
        case LEVEL_DISPLAY: drawDisplayMenu(); break;
        case LEVEL_STATUS_BAR: drawStatusBarMenu(); break;
        case LEVEL_NOTIFICATIONS: drawNotificationsMenu(); break;
        case LEVEL_TIMERS: drawTimersMenu(); break;
    }
    
    _sprite->pushSprite(0, 0);
}

void SettingsUI::drawHeader(String title) {
    _sprite->setTextSize(1.5);
    _sprite->setTextColor(COLOR_TEXT);
    _sprite->setTextDatum(middle_center);
    _sprite->drawString(title, 120, 20);
    drawBackButton();
}

void SettingsUI::drawBackButton() {
    _sprite->fillCircle(25, 20, 15, COLOR_TILE);
    _sprite->setTextColor(COLOR_TEXT);
    _sprite->drawString("<", 25, 20);
}

void SettingsUI::drawToggleItem(int y, String label, bool state) {
    _sprite->fillRoundRect(10, y, 220, 50, 8, COLOR_TILE);
    _sprite->setTextSize(1.2);
    _sprite->setTextColor(COLOR_TEXT);
    _sprite->setTextDatum(middle_left);
    _sprite->drawString(label, 20, y + 25);
    
    // Toggle switch
    uint16_t color = state ? COLOR_TOGGLE_ON : COLOR_DIM;
    _sprite->fillRoundRect(180, y + 15, 30, 20, 10, color);
    if (state) {
        _sprite->fillCircle(200, y + 25, 8, COLOR_TEXT); // Thumb right
    } else {
        _sprite->fillCircle(190, y + 25, 8, COLOR_TEXT); // Thumb left
    }
}

void SettingsUI::drawGrid() {
    drawHeader("SETTINGS");
    
    // Draw 4 tiles
    _sprite->fillRoundRect(10, 60, 105, 80, 8, COLOR_TILE);
    _sprite->setTextDatum(middle_center);
    _sprite->setTextColor(COLOR_TEXT);
    _sprite->drawString("Display", 62, 100);
    
    _sprite->fillRoundRect(125, 60, 105, 80, 8, COLOR_TILE);
    _sprite->drawString("Status Bar", 177, 100);
    
    _sprite->fillRoundRect(10, 150, 105, 80, 8, COLOR_TILE);
    _sprite->drawString("Alerts", 62, 190);
    
    _sprite->fillRoundRect(125, 150, 105, 80, 8, COLOR_TILE);
    _sprite->drawString("Timers", 177, 190);
}

void SettingsUI::drawDisplayMenu() {
    drawHeader("DISPLAY");
    _sprite->fillRoundRect(10, 60, 220, 60, 8, _isClockMode ? COLOR_ACCENT : COLOR_TILE);
    _sprite->setTextDatum(middle_center);
    _sprite->setTextColor(COLOR_TEXT);
    _sprite->drawString("Clock Mode", 120, 90);
    
    _sprite->fillRoundRect(10, 130, 220, 60, 8, !_isClockMode ? COLOR_ACCENT : COLOR_TILE);
    _sprite->drawString("Robot Eyes", 120, 160);
}

void SettingsUI::drawStatusBarMenu() {
    drawHeader("STATUS BAR");
    drawToggleItem(60, "Enable Bar", _sbEnable);
    if (_sbEnable) {
        drawToggleItem(120, "Show Wi-Fi", _sbWifi);
        drawToggleItem(180, "Show Time", _sbTime);
    }
}

void SettingsUI::drawNotificationsMenu() {
    drawHeader("NOTIFICATIONS");
    drawToggleItem(60, "Enable Alerts", _notificationsEnabled);
}

void SettingsUI::drawTimersMenu() {
    drawHeader("POMODORO");
    
    // Custom Drum Picker Viewport
    int cx = 120;
    int cy = 160;
    int itemH = 40;
    
    // Highlight box for selected item
    _sprite->drawRoundRect(60, cy - itemH/2, 120, itemH, 8, COLOR_ACCENT);
    _sprite->setTextDatum(middle_center);
    
    // Draw visible items
    for (int i = 0; i <= _drumMaxIndex; i++) {
        float yPos = cy + (i * itemH) - _drumScrollY;
        
        // Clip to viewport (80 to 240)
        if (yPos > 60 && yPos < 260) {
            float dist = abs(cy - yPos);
            float scale = map(dist, 0, 100, 20, 10) / 10.0f; // Scale font based on dist
            uint16_t color = (dist < itemH/2) ? COLOR_TEXT : COLOR_DIM;
            
            _sprite->setTextSize(scale);
            _sprite->setTextColor(color);
            _sprite->drawString(String(i) + " min", cx, yPos);
        }
    }
    
    // Start button
    _sprite->fillRoundRect(10, 260, 220, 50, 8, COLOR_ACCENT);
    _sprite->setTextSize(1.5);
    _sprite->setTextColor(COLOR_BG);
    _sprite->drawString("START", 120, 285);
}
