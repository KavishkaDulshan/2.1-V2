#ifndef CLOCK_UI_H
#define CLOCK_UI_H

#include <LovyanGFX.hpp>
#include "RobotEyes.h"

class ClockUI {
public:
    void init(lgfx::LGFX_Device* display);
    void draw(RobotEyes& eyes, String weatherCity);

private:
    LGFX_Sprite* _sprite;
    lgfx::LGFX_Device* _display;

    static const int MAX_RAIN_DROPS = 30;
    struct RainDrop {
        float x, y, speed;
        int len;
        bool active;
    } rainDrops[MAX_RAIN_DROPS];

    static const int MAX_CLOCK_STARS = 25;
    struct ClockStar {
        float x, y;
        float twinkleAngle;
        float twinkleSpeed;
        uint8_t size;
    } clockStars[MAX_CLOCK_STARS];

    static const int MAX_BG_CLOUDS = 5;
    struct BgCloud {
        float x, y, speed;
        uint8_t alpha;
    } bgClouds[MAX_BG_CLOUDS];

    bool _bgInitialized = false;
};

#endif
