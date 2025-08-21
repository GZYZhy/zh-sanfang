#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <BluetoothSerial.h>


#define A "zhsftlk"
#define B "zhsftlk"
#define C 6
#define D IPAddress(192,168,5,1)
#define E IPAddress(192,168,5,2)
#define F IPAddress(192,168,5,3)
#define G IPAddress(192,168,5,1)
#define H IPAddress(255,255,255,0)
#define I 8000
#define J 256
#define K "zhsf-3"
#define L "0000"
#define M 14
#define N 22
#define O 25
#define P 26
#define Qq 25
#define Rr 26


enum Q{R,S,T};
WebServer U(80);
WiFiUDP V;
BluetoothSerial W;
bool X=false,Y=false;
Q Z=T;

int a=0;
uint8_t b[J];
size_t d=0;

void setup() {

  pinMode(M,INPUT);

  pinMode(N,OUTPUT);
  pinMode(O,OUTPUT);
  pinMode(P,OUTPUT);

  digitalWrite(N,HIGH);
  digitalWrite(O,LOW);
  digitalWrite(P,LOW);

  f(T);
  g();
  h();

}

void loop(){
  U.handleClient();
  i();
  j();
  Y=W.connected();
  k();
  static unsigned long n=0;
  if(millis()-n>100){l();m(b,d);n=millis();}
}

void f(Q r){
  WiFi.mode(WIFI_STA);
  IPAddress s=(r==S)?E:F;
  WiFi.config(s,G,H);
  WiFi.begin(A,B);

}

// ==================== Web服务器初始化 ====================
void h(){
  U.on("/scanBt",o);
  U.on("/connectBt",p);
  U.on("/getStatus",q);
  U.on("/setMute",r);
  U.on("/setLed",s);
  U.begin();

}

void g(){
  String t=Z==S?"zhsf-2":"zhsf-3";
  if(!W.begin(t.c_str()))return;
}

void m(const uint8_t* u,size_t v){
  if(X||!Y)return;
  V.beginPacket(D,I);
  V.write(u,v);
  V.endPacket();
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

    server.send(200, "text/plain", "OK");
  }
}

// 蓝牙工具函数
String scanBtDevices() {
  return "[{\"name\":\"H1\",\"addr\":\"00:1A:7D:DA:71:13\"},{\"name\":\"H2\",\"addr\":\"00:1B:2C:3D:4E:5F\"}]";
}

bool connectBtDevice(const char* addr) {


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
