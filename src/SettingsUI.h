#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Preferences.h>

enum MenuLevel {
    LEVEL_GRID = 0,
    LEVEL_DISPLAY,
    LEVEL_STATUS_BAR,
    LEVEL_NOTIFICATIONS,
    LEVEL_TIMERS
};

class SettingsUI {
public:
    SettingsUI();
    
    void init(lgfx::LGFX_Device* display, Preferences* prefs);
    bool update(bool isTouched, int16_t touchX, int16_t touchY);
    void draw();
    void reset();
    
    bool wantsToClose() const { return _wantsToClose; }

    // External accessors to get applied state
    bool isClockMode() const { return _isClockMode; }
    bool isNotificationEnabled() const { return _notificationsEnabled; }
    
    // External states pushed in
    void setClockMode(bool mode) { _isClockMode = mode; _needsRedraw = true; }

private:
    lgfx::LGFX_Device* _display;
    LGFX_Sprite* _sprite;
    Preferences* _prefs;
    
    bool _needsRedraw;
    bool _wantsToClose;
    MenuLevel _currentMenu;
    
    // States
    bool _isClockMode;
    bool _sbEnable;
    bool _sbWifi;
    bool _sbTime;
    bool _notificationsEnabled;
    
    // Timer/Alarm states
    int _timerMinutes;
    int _alarmHour;
    int _alarmMinute;
    bool _isSettingAlarm; // true = setting alarm, false = setting timer

    // Touch tracking
    bool _wasTouched;
    int16_t _lastTouchX;
    int16_t _lastTouchY;
    
    // Drum Picker state
    float _drumScrollY;
    float _drumVelocity;
    bool  _drumDragging;
    int   _drumSelectedIndex;
    int   _drumMaxIndex;

    // Helpers
    void handleGridTouch(int16_t x, int16_t y);
    void handleDisplayTouch(int16_t x, int16_t y);
    void handleStatusBarTouch(int16_t x, int16_t y);
    void handleNotificationsTouch(int16_t x, int16_t y);
    void handleTimersTouch(int16_t x, int16_t y);

    void drawGrid();
    void drawDisplayMenu();
    void drawStatusBarMenu();
    void drawNotificationsMenu();
    void drawTimersMenu();
    void drawDrumPicker(int x, int y, int w, int h, int maxVal);
    
    void drawHeader(String title);
    void drawBackButton();
    void drawToggleItem(int y, String label, bool state);
    void saveBool(const char* key, bool value);
};

#endif // SETTINGS_UI_H
