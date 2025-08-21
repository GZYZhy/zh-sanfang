#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <BluetoothSerial.h>

// ==================== WiFi配置 ====================
// 隐藏WiFi热点信息
#define AP_SSID "zhsftlk"   // 隐藏SSID
#define AP_PASS "zhsftlk"         // 密码
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
#define HFP_DEVICE_NAME "zhsf-3"  // 蓝牙名称
#define BT_PIN_CODE "0000"               // 配对码

// ==================== 硬件配置 ====================
#define MUTE_BUTTON 14                  // 静音按键（GPIO14，下拉触发）
#define BUILTIN_LED 22                  // ESP32内置LED（GPIO22）
#define CONTROL_LED_R 25                // 可控灯-红（GPIO25）
#define CONTROL_LED_G 26                // 可控灯-绿（GPIO26）

// ==================== 设备类型 ====================
enum DeviceType {
  DEV_MASTER,
  DEV_SLAVE1,
  DEV_SLAVE2
};

// 全局变量初始化（从设备2）
WebServer server(80);
WiFiUDP udp;
BluetoothSerial SerialBT;
bool isMuted = false;
bool isBtConnected = false;

DeviceType deviceType = DEV_SLAVE2;  // 从设备2

// LED控制变量
int controlLedState = 0;  // 0=灭, 1=红, 2=绿

// 音频缓冲区
uint8_t audioBuffer[MAX_AUDIO_LEN];
size_t audioBufferLen = 0;

void setup() {
  Serial.begin(115200);
  pinMode(MUTE_BUTTON, INPUT);  // 静音按键（按下高电平，释放低电平）

  // 初始化LED引脚
  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(CONTROL_LED_R, OUTPUT);
  pinMode(CONTROL_LED_G, OUTPUT);

  // 初始化LED状态（ESP32内置LED低电平点亮）
  digitalWrite(BUILTIN_LED, HIGH);  // 初始关闭
  digitalWrite(CONTROL_LED_R, LOW);
  digitalWrite(CONTROL_LED_G, LOW);

  // 初始化WiFi（从设备作为STA连接主设备AP）
  initWiFi(DEV_SLAVE2);

  // 初始化HFP蓝牙
  initHFP();

  // 初始化Web服务器
  initWebServer();

  // 初始化UDP
  if (!udp.begin(UDP_PORT)) {
    Serial.println("UDP F");
  } else {
    Serial.println("UDP");
  }

  Serial.println("S2");
}

void loop() {
  server.handleClient();       // 处理Web请求
  handleMuteButton();          // 处理静音按键
  receiveAudio();              // 接收主设备转发的音频

  // 更新蓝牙连接状态
  isBtConnected = SerialBT.connected();

  // 更新LED状态
  updateLedStatus();

  // 模拟音频采集和发送（实际项目中需要添加真实的音频采集代码）
  static unsigned long lastAudioSend = 0;
  if (millis() - lastAudioSend > 100) {  // 每100ms发送一次音频
    generateTestAudio();  // 模拟音频数据
    sendAudio(audioBuffer, audioBufferLen);
    lastAudioSend = millis();
  }
}

// ==================== WiFi初始化 ====================
void initWiFi(DeviceType type) {
  WiFi.mode(WIFI_STA);  // 从设备作为STA

  // 配置固定IP
  IPAddress localIP = (type == DEV_SLAVE1) ? SLAVE1_IP : SLAVE2_IP;
  WiFi.config(localIP, GATEWAY, SUBNET);

  // 连接隐藏AP（需手动指定SSID，因为不广播）
  WiFi.begin(AP_SSID, AP_PASS);

  // 等待连接
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("从设备IP: ");
  Serial.println(WiFi.localIP());
}

// ==================== Web服务器初始化 ====================
void initWebServer() {
  server.on("/scanBt", handleScanBt);
  server.on("/connectBt", handleConnectBt);
  server.on("/getStatus", handleGetStatus);
  server.on("/setMute", handleSetMute);
  server.on("/setLed", handleSetLed);
  server.begin();
  Serial.println("Web");
}

// ==================== 蓝牙初始化 ====================
void initHFP() {
  // 初始化BluetoothSerial
  String deviceName = deviceType == DEV_SLAVE1 ? "ESP32_Slave1" : "ESP32_Slave2";
  if (!SerialBT.begin(deviceName.c_str())) {
    Serial.println("蓝牙初始化失败");
    return;
  }
  Serial.println("BT OK");
}

// ==================== 音频发送（从设备发送到主设备） ====================
void sendAudio(const uint8_t* data, size_t len) {
  if (isMuted || !isBtConnected) return;

  // 发送到主设备
  udp.beginPacket(MASTER_IP, UDP_PORT);
  udp.write(data, len);
  udp.endPacket();
}

// ==================== 音频接收（从设备接收主设备） ====================
void receiveAudio() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;

  // 只接收主设备的音频
  if (udp.remoteIP() != MASTER_IP) return;

  uint8_t buffer[MAX_AUDIO_LEN];
  size_t len = udp.read(buffer, MAX_AUDIO_LEN);

  // 无论是否静音，都要播放主设备的音频（接收音频不受静音影响）
  if (len > 0 && isBtConnected) {
    SerialBT.write(buffer, len);  // 通过蓝牙发送音频数据到耳机
  }
}

// ==================== 模拟音频生成 ====================
void generateTestAudio() {
  // 生成简单的测试音频数据（正弦波，频率稍有不同以便区分设备）
  static int phase = 0;
  audioBufferLen = MAX_AUDIO_LEN;

  for (int i = 0; i < MAX_AUDIO_LEN; i++) {
    // 生成1000Hz的正弦波（与从设备1的800Hz不同）
    audioBuffer[i] = 128 + 127 * sin(2 * PI * 1000 * phase / 8000.0);
    phase++;
    if (phase >= 8000) phase = 0;
  }
}

// ==================== 静音按键处理 ====================
void handleMuteButton() {
  static unsigned long lastDebounce = 0;
  static bool lastState = LOW;
  static bool buttonPressed = false;
  const unsigned long debounceDelay = 200;

  int currentState = digitalRead(MUTE_BUTTON);

  if (currentState != lastState) {
    lastDebounce = millis();
  }

  if ((millis() - lastDebounce) > debounceDelay) {
    if (currentState == HIGH && !buttonPressed) {
      // 按键按下（高电平），切换静音状态
      buttonPressed = true;
      isMuted = !isMuted;
      Serial.println(isMuted ? "M1" : "M0");
    } else if (currentState == LOW && buttonPressed) {
      // 按键释放（低电平），重置按键状态
      buttonPressed = false;
    }
  }

  lastState = currentState;
}

// ==================== Web回调函数 ====================
// 纯JSON API，无HTML界面

void handleScanBt() {
  String devices = scanBtDevices();
  server.send(200, "application/json", devices);
}

void handleConnectBt() {
  if (server.hasArg("addr")) {
    String addr = server.arg("addr");
    bool success = connectBtDevice(addr.c_str());
    server.send(200, "text/plain", success ? "连接成功" : "连接失败");
  } else {
    server.send(400, "text/plain", "缺少设备地址");
  }
}

void handleGetStatus() {
  char json[50];
  sprintf(json, "{\"bt\":%d,\"mute\":%d}", isBtConnected ? 1 : 0, isMuted ? 1 : 0);
  server.send(200, "application/json", json);
}

// 设置静音状态
void handleSetMute() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    isMuted = (state == "1");
    Serial.println(isMuted ? "M1" : "M0");
    server.send(200, "text/plain", "OK");
  }
}

// 蓝牙工具函数
String scanBtDevices() {
  return "[{\"name\":\"H1\",\"addr\":\"00:1A:7D:DA:71:13\"},{\"name\":\"H2\",\"addr\":\"00:1B:2C:3D:4E:5F\"}]";
}

bool connectBtDevice(const char* addr) {
  Serial.print("C:");
  Serial.println(addr);
  delay(1000);
  return SerialBT.connected();
}

// ==================== LED状态更新 ====================
void updateLedStatus() {
  // 静音状态指示灯（内置LED）
  static unsigned long lastBlink = 0;
  if (isMuted) {
    digitalWrite(BUILTIN_LED, LOW);  // 常亮 - 静音
  } else {
    // 闪烁 - 讲话中
    if (millis() - lastBlink > 500) {
      digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
      lastBlink = millis();
    }
  }

  // 可控LED状态
  switch (controlLedState) {
    case 0:  // 灭
      digitalWrite(CONTROL_LED_R, LOW);
      digitalWrite(CONTROL_LED_G, LOW);
      break;
    case 1:  // 红
      digitalWrite(CONTROL_LED_R, HIGH);
      digitalWrite(CONTROL_LED_G, LOW);
      break;
    case 2:  // 绿
      digitalWrite(CONTROL_LED_R, LOW);
      digitalWrite(CONTROL_LED_G, HIGH);
      break;
  }
}

// 设置LED状态
void handleSetLed() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    if (state == "off") {
      controlLedState = 0;
      server.send(200, "text/plain", "OK");
    } else if (state == "red") {
      controlLedState = 1;
      server.send(200, "text/plain", "OK");
    } else if (state == "green") {
      controlLedState = 2;
      server.send(200, "text/plain", "OK");
    }
  }
}
