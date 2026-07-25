#include "ClockUI.h"

void ClockUI::init(lgfx::LGFX_Device* display) {
    _display = display;
    _sprite = new LGFX_Sprite(_display);
    _sprite->setPsram(true);
    _sprite->createSprite(240, 320);
}

void ClockUI::draw(RobotEyes& eyes, String weatherCity) {
    if (!_bgInitialized) {
        _bgInitialized = true;
        for (int i = 0; i < MAX_CLOCK_STARS; i++) {
            clockStars[i].x = random(0, 240);
            clockStars[i].y = random(0, 320);
            clockStars[i].twinkleAngle = (float)(random(0, 628)) / 100.0f;
            clockStars[i].twinkleSpeed = 0.03f + (float)(random(0, 50)) / 1000.0f;
            clockStars[i].size = random(1, 3);
        }
        for (int i = 0; i < MAX_BG_CLOUDS; i++) {
            bgClouds[i].x = (float)random(0, 300);
            bgClouds[i].y = (float)random(20, 150);
            bgClouds[i].speed = 0.08f + (float)(random(0, 10)) / 100.0f;
            bgClouds[i].alpha = 60 + random(0, 40);
        }
        for (int i = 0; i < MAX_RAIN_DROPS; i++) {
            rainDrops[i].x = random(0, 240);
            rainDrops[i].y = random(-30, 320);
            rainDrops[i].speed = 1.8f + (float)(random(0, 120)) / 100.0f;
            rainDrops[i].len = random(4, 9);
            rainDrops[i].active = true;
        }
    }

    int h = eyes.clockHour;
    uint16_t skyColor;
    
    if (h >= 9 && h < 17) {
        for (int y = 0; y < 320; y++) {
            float t = (float)y / 320.0f;
            uint8_t r = 8 + (uint8_t)(t * (21 - 8));
            uint8_t g = 88 + (uint8_t)(t * (118 - 88));
            uint8_t b = 147 + (uint8_t)(t * (171 - 147));
            _sprite->drawFastHLine(0, y, 240, _sprite->color565(r, g, b));
        }
    } else {
        if (h >= 6 && h < 9) {
            skyColor = 0x034B; 
        } else if (h >= 17 && h < 20) {
            skyColor = 0x9203; 
        } else {
            skyColor = 0x000A; 
        }
        _sprite->fillScreen(skyColor);
    }

    bool isCloud = (eyes.weatherIcon == "cloud");
    bool isRain  = (eyes.weatherIcon == "rain");
    bool isSun   = (eyes.weatherIcon == "sun" || eyes.weatherIcon == "loading" || eyes.weatherIcon == "");

    bool isMorning = (h >= 6 && h < 9);
    bool isAfternoon = (h >= 9 && h < 17);
    bool isNight = (h < 6 || h >= 20);

    if (isNight || isMorning || (isCloud && !isAfternoon)) {
        for (int i = 0; i < MAX_CLOCK_STARS; i++) {
            clockStars[i].twinkleAngle += clockStars[i].twinkleSpeed;
            float brightness = 0.4f + 0.6f * (0.5f + 0.5f * sin(clockStars[i].twinkleAngle));
            uint8_t bv = (uint8_t)(brightness * 200.0f);
            uint16_t sc = ((bv & 0xF8) << 8) | ((bv & 0xFC) << 3) | (bv >> 3);
            int sx = (int)clockStars[i].x;
            int sy = (int)clockStars[i].y;
            if (clockStars[i].size > 1) {
                _sprite->fillCircle(sx, sy, 1, sc);
            } else {
                _sprite->drawPixel(sx, sy, sc);
            }
        }
    }

    if (!isNight || isCloud || isRain) {
        for (int i = 0; i < MAX_BG_CLOUDS; i++) {
            bgClouds[i].x += bgClouds[i].speed;
            if (bgClouds[i].x > 300) bgClouds[i].x = -60;
            int cx = (int)bgClouds[i].x;
            int cy = (int)bgClouds[i].y;
            uint16_t cColor = isNight ? 0x2104 : 0xB5F7;
            
            // Scaled up cartoon clouds
            int r1 = 18, r2 = 14, r3 = 12;
            _sprite->fillCircle(cx,      cy,      r1, cColor); 
            _sprite->fillCircle(cx - 22, cy + 6,  r2, cColor); 
            _sprite->fillCircle(cx + 22, cy + 6,  r2, cColor); 
            _sprite->fillCircle(cx - 12, cy - 10, r3, cColor); 
            _sprite->fillCircle(cx + 12, cy - 10, r3, cColor); 
            _sprite->fillRect(cx - 34, cy + 4, 68, 12, cColor);
        }
    }

    if (isRain) {
        uint16_t rainColor = 0x3EFF;
        for (int i = 0; i < MAX_RAIN_DROPS; i++) {
            rainDrops[i].y += rainDrops[i].speed;
            if (rainDrops[i].y > 330) {
                rainDrops[i].y = (float)random(-30, 0);
                rainDrops[i].x = (float)random(0, 240);
            }
            int rx = (int)rainDrops[i].x;
            int ry = (int)rainDrops[i].y;
            _sprite->drawLine(rx, ry, rx - 1, ry + rainDrops[i].len, rainColor);
        }
    }

    if (isSun && !isNight && !isMorning) {
        float sunAngle = (float)(millis() % 3600) * 0.001f;
        int sunX = 210, sunY = 40;
        int innerR = 25, outerR = 40;
        uint16_t sunYellow = 0xFFE0;
        uint16_t sunOrange = 0xFD20;
        _sprite->fillCircle(sunX, sunY, innerR + 3, sunOrange);
        _sprite->fillCircle(sunX, sunY, innerR,     sunYellow);
        for (int r = 0; r < 8; r++) {
            float ang = sunAngle + r * 0.7854f;
            int x1 = sunX + (int)(cos(ang) * (innerR + 6));
            int y1 = sunY + (int)(sin(ang) * (innerR + 6));
            int x2 = sunX + (int)(cos(ang) * outerR);
            int y2 = sunY + (int)(sin(ang) * outerR);
            _sprite->drawLine(x1, y1, x2, y2, sunYellow);
        }
    }

    // Huge Time in the center
    _sprite->setTextFont(7); // Largest 7-segment font
    _sprite->setTextSize(1);
    _sprite->setTextDatum(textdatum_t::middle_center);
    
    // Draw shadow
    _sprite->setTextColor(0x0000);
    _sprite->drawString(eyes.timeString, 122, 142);
    _sprite->setTextColor(eyes.clockColor);
    _sprite->drawString(eyes.timeString, 120, 140);

    // Weather Icon directly below time
    int iconX = 120;
    int iconY = 210;

    if (eyes.weatherIcon == "sun") {
        _sprite->fillCircle(iconX, iconY, 16, 0xFFE0);
        for (int ri = 0; ri < 8; ri++) {
            float ang = ri * 0.7854f;
            int x1 = iconX + (int)(cos(ang) * 20);
            int y1 = iconY + (int)(sin(ang) * 20);
            int x2 = iconX + (int)(cos(ang) * 26);
            int y2 = iconY + (int)(sin(ang) * 26);
            _sprite->drawLine(x1, y1, x2, y2, 0xFD20);
        }
    } else if (eyes.weatherIcon == "cloud") {
        _sprite->fillCircle(iconX,      iconY + 2, 14, 0xFFFF);
        _sprite->fillCircle(iconX - 14, iconY + 8, 10, 0xFFFF);
        _sprite->fillCircle(iconX + 14, iconY + 8, 10, 0xFFFF);
        _sprite->fillCircle(iconX - 8,  iconY - 6, 10, 0xFFFF);
        _sprite->fillCircle(iconX + 8,  iconY - 6, 10, 0xFFFF);
        _sprite->fillRect(iconX - 20, iconY + 2, 40, 14, 0xFFFF);
    } else if (eyes.weatherIcon == "rain") {
        uint16_t cloudC = 0xAD75;
        _sprite->fillCircle(iconX,      iconY - 6, 14, cloudC);
        _sprite->fillCircle(iconX - 14, iconY,     10, cloudC);
        _sprite->fillCircle(iconX + 14, iconY,     10, cloudC);
        _sprite->fillRect(iconX - 20, iconY - 4, 40, 14, cloudC);
        
        uint16_t dropC = 0x3EFF;
        _sprite->fillCircle(iconX - 10, iconY + 20, 3, dropC);
        _sprite->drawLine(iconX - 10, iconY + 14, iconX - 12, iconY + 18, dropC);
        _sprite->fillCircle(iconX + 4,  iconY + 22, 3, dropC);
        _sprite->drawLine(iconX + 4,  iconY + 16, iconX + 2,  iconY + 20, dropC);
        _sprite->fillCircle(iconX + 18, iconY + 20, 3, dropC);
        _sprite->drawLine(iconX + 18, iconY + 14, iconX + 16, iconY + 18, dropC);
    }

    // Condition Text
    _sprite->setTextFont(4);
    _sprite->setTextSize(1);
    _sprite->setTextDatum(textdatum_t::middle_center);
    _sprite->setTextColor(0xFFFF);
    _sprite->drawString(eyes.weatherCondition, 120, 250);

    // Temp & City at bottom
    _sprite->setTextFont(4);
    _sprite->setTextDatum(textdatum_t::bottom_center);
    _sprite->setTextColor(0xFFE0); // Yellow Temp
    String tempStr = String((int)eyes.weatherTemp) + (char)247 + "C";
    
    _sprite->drawString(tempStr + " | " + weatherCity, 120, 310);

    _sprite->pushSprite(0, 0);
}
