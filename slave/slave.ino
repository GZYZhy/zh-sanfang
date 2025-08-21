#include "common.h"

// 全局变量初始化（从设备1）
WebServer server(80);
WiFiUDP udp;
bool isMuted = false;
bool isBtConnected = false;
String btConnectedDevice = "未连接";
DeviceType deviceType = DEV_SLAVE1;  // 从设备1（从设备2改为DEV_SLAVE2）

void setup() {
  Serial.begin(115200);
  pinMode(MUTE_BUTTON, INPUT_PULLUP);  // 静音按键

  // 初始化WiFi（从设备作为STA连接主设备AP）
  initWiFi(DEV_SLAVE1);

  // 初始化HFP蓝牙
  initHFP();

  // 初始化Web服务器
  initWebServer();

  // 初始化UDP
  if (!udp.begin(UDP_PORT)) {
    Serial.println("UDP启动失败！");
  } else {
    Serial.println("UDP启动成功");
  }

  Serial.println("从设备1初始化完成");
}

void loop() {
  server.handleClient();       // 处理Web请求
  handleMuteButton();          // 处理静音按键
  receiveAudio();              // 接收主设备转发的音频
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
  server.on("/", handleRoot);
  server.on("/scanBt", handleScanBt);
  server.on("/connectBt", handleConnectBt);
  server.on("/getStatus", handleGetStatus);
  server.begin();
  Serial.print("Web服务器启动，访问 http://");
  Serial.println(WiFi.localIP());
}

// ==================== 蓝牙HFP初始化（同主设备） ====================
void initHFP() {
  // 与主设备initHFP()相同
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
    Serial.println("蓝牙控制器初始化失败");
    return;
  }
  if (esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK) {
    Serial.println("启用经典蓝牙失败");
    return;
  }
  if (esp_bluedroid_init() != ESP_OK || esp_bluedroid_enable() != ESP_OK) {
    Serial.println("Bluedroid初始化失败");
    return;
  }
  esp_bt_dev_set_device_name(deviceType == DEV_SLAVE1 ? "ESP32_Slave1" : "ESP32_Slave2");
  Serial.println("HFP蓝牙初始化完成");
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

  // 播放音频到蓝牙耳机（需根据HFP库实现）
  if (len > 0 && !isMuted && isBtConnected) {
    // hfp_send_audio(buffer, len);  // 伪代码，替换为实际库函数
  }
}

// ==================== 静音按键处理（同主设备） ====================
void handleMuteButton() {
  static unsigned long lastDebounce = 0;
  static bool lastState = HIGH;
  const unsigned long debounceDelay = 200;

  int currentState = digitalRead(MUTE_BUTTON);

  if (currentState != lastState) {
    lastDebounce = millis();
  }

  if ((millis() - lastDebounce) > debounceDelay && currentState == LOW) {
    isMuted = !isMuted;
    Serial.print("静音状态: ");
    Serial.println(isMuted ? "已静音" : "未静音");
  }

  lastState = currentState;
}

// ==================== Web回调函数（同主设备，仅页面标题区分） ====================
void handleRoot() {
  String html = R"(
    <html>
      <head><title>从设备1 - 蓝牙对讲控制</title></head>
      <body>
        <h1>ESP32从设备1控制</h1>
        <p>蓝牙状态: <span id="btStatus">)" + btConnectedDevice + R"(</span></p >
        <p>静音状态: <span id="muteStatus">)" + String(isMuted ? "已静音" : "未静音") + R"(</span></p >
        <button onclick="scanBtDevices()">扫描蓝牙设备</button>
        <div id="btDevices"></div>
        <script>
          function scanBtDevices() {
            fetch('/scanBt')
              .then(res => res.json())
              .then(devices => {
                let list = '<h3>发现设备:</h3><ul>';
                devices.forEach(dev => {
                  list += `<li>${dev.name} (${dev.addr}) 
                    <button onclick="connectBt('${dev.addr}')">连接</button></li>`;
                });
                list += '</ul>';
                document.getElementById('btDevices').innerHTML = list;
              });
          }
          function connectBt(addr) {
            fetch('/connectBt?addr=' + addr)
              .then(res => res.text())
              .then(msg => alert(msg));
          }
          setInterval(() => {
            fetch('/getStatus')
              .then(res => res.json())
              .then(status => {
                document.getElementById('btStatus').innerText = status.btDevice;
                document.getElementById('muteStatus').innerText = status.isMuted ? '已静音' : '未静音';
              });
          }, 1000);
        </script>
      </body>
    </html>
  )";
  server.send(200, "text/html", html);
}

// 以下Web回调函数与主设备相同
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
  String json = "{\"btDevice\":\"" + btConnectedDevice + "\",\"isMuted\":" + String(isMuted) + "}";
  server.send(200, "application/json", json);
}

// 蓝牙工具函数（同主设备）
String scanBtDevices() {
  String json = "[";
  json += "{\"name\":\"Headset-1\",\"addr\":\"00:1A:7D:DA:71:13\"},";
  json += "{\"name\":\"Headset-2\",\"addr\":\"00:1B:2C:3D:4E:5F\"}";
  json += "]";
  return json;
}

bool connectBtDevice(const char* addr) {
  Serial.print("尝试连接蓝牙设备: ");
  Serial.println(addr);
  isBtConnected = true;
  btConnectedDevice = "Headset (" + String(addr) + ")";
  return true;
}
