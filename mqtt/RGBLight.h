#ifndef _RGBLIGHT_
#define _RGBLIGHT_

#include <stdint.h>

// RGB灯引脚定义 - 根据实际硬件连接修改
#define RGB_R_PIN 12
#define RGB_G_PIN 14  
#define RGB_B_PIN 27

// 灯模式枚举
enum LightMode {
    MODE_OFF = 0,
    MODE_GREEN = 1,     // 开机默认绿色
    MODE_YELLOW_FLASH = 2,
    MODE_YELLOW = 3,
    MODE_RED_FLASH = 4,
    MODE_RED = 5,
    MODE_RED_BLUE_ALTERNATE = 6
};

void RGBLightInit();
void setLightMode(LightMode mode);
void updateLight();
LightMode getCurrentMode();

#endif
