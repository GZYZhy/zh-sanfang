#ifndef COMMON_H
#define COMMON_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <BluetoothSerial.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_hfp_ag_api.h>

// ==================== WiFi配置 ====================
// 隐藏WiFi热点信息
#define AP_SSID "zhang-sanfang-talk"   // 隐藏SSID
#define AP_PASS "zhang-sanfang-talk"         // 密码
#define AP_CHANNEL 6                   // 信道（1-11）

// 固定IP地址（主设备AP + 从设备STA）
#define MASTER_IP   IPAddress(192, 168, 5, 1)  // 主设备IP
#define SLAVE1_IP   IPAddress(192, 168, 5, 2)  // 从设备1IP
#define SLAVE2_IP   IPAddress(192, 168, 5, 3)  // 从设备2IP
#define GATEWAY     IPAddress(192, 168, 5, 1)
#define SUBNET      IPAddress(255, 255, 255, 0)

// ==================== 网络通信 ====================
#define UDP_PORT 8000                  // 音频传输端口
#define MAX_AUDIO_LEN 256              // 音频包大小

// ==================== 蓝牙配置 ====================
#define HFP_DEVICE_NAME "zhang-sanfang-talk"  // 蓝牙名称
#define BT_PIN_CODE "0000"               // 配对码

// ==================== 硬件配置 ====================
#define MUTE_BUTTON 14                  // 静音按键（GPIO14，下拉触发）

// ==================== 设备类型 ====================
enum DeviceType {
  DEV_MASTER,
  DEV_SLAVE1,
  DEV_SLAVE2
};

// ==================== 全局变量 ====================
extern WebServer server;                // Web服务器
extern WiFiUDP udp;                     // UDP通信
extern bool isMuted;                    // 静音状态
extern bool isBtConnected;              // 蓝牙连接状态
extern String btConnectedDevice;        // 已连接蓝牙设备名称
extern DeviceType deviceType;           // 当前设备类型

// ==================== 函数声明 ====================
// WiFi初始化
void initWiFi(DeviceType type);

// Web服务器初始化
void initWebServer();

// 蓝牙HFP初始化（作为音频网关AG）
void initHFP();

// 静音按键处理
void handleMuteButton();

// 音频发送（UDP）
void sendAudio(const uint8_t* data, size_t len);

// 音频接收（UDP）
void receiveAudio();

// 蓝牙设备扫描
String scanBtDevices();

// 蓝牙连接设备
bool connectBtDevice(const char* addr);

// 网页回调函数
void handleRoot();
void handleScanBt();
void handleConnectBt();
void handleGetStatus();

#endif
