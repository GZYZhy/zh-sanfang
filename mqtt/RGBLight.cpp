#include "RGBLight.h"
#include <Arduino.h>

static LightMode currentMode = MODE_GREEN;
static unsigned long lastUpdateTime = 0;
static bool flashState = false;

void RGBLightInit() {
    pinMode(RGB_R_PIN, OUTPUT);
    pinMode(RGB_G_PIN, OUTPUT);
    pinMode(RGB_B_PIN, OUTPUT);
    
    // 高电平点亮，低电平熄灭 - 初始化为熄灭状态
    digitalWrite(RGB_R_PIN, LOW);
    digitalWrite(RGB_G_PIN, LOW);
    digitalWrite(RGB_B_PIN, LOW);
    
    setLightMode(MODE_GREEN); // 开机默认绿色
}

void setLightMode(LightMode mode) {
    currentMode = mode;
    lastUpdateTime = millis();
    flashState = false;
    
    // 立即设置非闪烁模式的固定颜色
    switch(mode) {
        case MODE_OFF:
            digitalWrite(RGB_R_PIN, LOW);
            digitalWrite(RGB_G_PIN, LOW);
            digitalWrite(RGB_B_PIN, LOW);
            break;
        case MODE_GREEN:
            digitalWrite(RGB_R_PIN, LOW);
            digitalWrite(RGB_G_PIN, HIGH);
            digitalWrite(RGB_B_PIN, LOW);
            break;
        case MODE_YELLOW:
            digitalWrite(RGB_R_PIN, HIGH);
            digitalWrite(RGB_G_PIN, HIGH);
            digitalWrite(RGB_B_PIN, LOW);
            break;
        case MODE_RED:
            digitalWrite(RGB_R_PIN, HIGH);
            digitalWrite(RGB_G_PIN, LOW);
            digitalWrite(RGB_B_PIN, LOW);
            break;
        default:
            // 闪烁模式在update中处理
            break;
    }
}

void updateLight() {
    unsigned long currentTime = millis();
    
    switch(currentMode) {
        case MODE_YELLOW_FLASH:
            if (currentTime - lastUpdateTime > 500) { // 500ms闪烁
                flashState = !flashState;
                digitalWrite(RGB_R_PIN, flashState ? HIGH : LOW);
                digitalWrite(RGB_G_PIN, flashState ? HIGH : LOW);
                digitalWrite(RGB_B_PIN, LOW);
                lastUpdateTime = currentTime;
            }
            break;
            
        case MODE_RED_FLASH:
            if (currentTime - lastUpdateTime > 500) { // 500ms闪烁
                flashState = !flashState;
                digitalWrite(RGB_R_PIN, flashState ? HIGH : LOW);
                digitalWrite(RGB_G_PIN, LOW);
                digitalWrite(RGB_B_PIN, LOW);
                lastUpdateTime = currentTime;
            }
            break;
            
        case MODE_RED_BLUE_ALTERNATE:
            if (currentTime - lastUpdateTime > 500) { // 500ms交替
                flashState = !flashState;
                digitalWrite(RGB_R_PIN, flashState ? HIGH : LOW);
                digitalWrite(RGB_G_PIN, LOW);
                digitalWrite(RGB_B_PIN, flashState ? LOW : HIGH);
                lastUpdateTime = currentTime;
            }
            break;
            
        default:
            // 其他模式不需要更新
            break;
    }
}

LightMode getCurrentMode() {
    return currentMode;
}