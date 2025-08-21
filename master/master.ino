#include "common.h"

// 全局变量初始化
WebServer server(80);
WiFiUDP udp;
bool isMuted = false;
bool isBtConnected = false;
String btConnectedDevice = "未连接";
DeviceType deviceType = DEV_MASTER;

// 从设备音频缓冲区（用于混合）
uint8_t slave1Audio[MAX_AUDIO_LEN] = {0};
uint8_t slave2Audio[MAX_AUDIO_LEN] = {0};
size_t slave1Len = 0;
size_t slave2Len = 0;

void setup() {
  Serial.begin(115200);
  pinMode(MUTE_BUTTON, INPUT_PULLUP);  // 静音按键（上拉，按下低电平）

  // 初始化WiFi（主设备作为隐藏AP）
  initWiFi(DEV_MASTER);

  // 初始化HFP蓝牙（音频网关）
  initHFP();

  // 初始化Web服务器
  initWebServer();

  // 初始化UDP
  if (!udp.begin(UDP_PORT)) {
    Serial.println("UDP启动失败！");
  } else {
    Serial.println("UDP启动成功");
  }

  Serial.println("主设备初始化完成");
}

void loop() {
  server.handleClient();       // 处理Web请求
  handleMuteButton();          // 处理静音按键
  receiveAudio();              // 接收从设备音频

  // 混合从设备音频并播放（如果蓝牙已连接且未静音）
  if (isBtConnected && !isMuted && slave1Len > 0 && slave2Len > 0) {
    uint8_t mixedAudio[MAX_AUDIO_LEN];
    // 简单混合（取平均值，防止溢出）
    for (int i = 0; i < MAX_AUDIO_LEN; i++) {
      mixedAudio[i] = (slave1Audio[i] + slave2Audio[i]) / 2;
    }
    // 发送到蓝牙耳机（实际需根据HFP库实现音频输出）
    // hfp_send_audio(mixedAudio, MAX_AUDIO_LEN);  // 伪代码，需替换为实际库函数
    slave1Len = 0;
    slave2Len = 0;
  }
}

// ==================== WiFi初始化 ====================
void initWiFi(DeviceType type) {
  WiFi.mode(WIFI_AP);  // 主设备作为AP

  // 配置隐藏AP（不广播SSID）
  WiFi.softAPConfig(MASTER_IP, GATEWAY, SUBNET);
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0);  // 最后一个参数0=隐藏SSID

  if (!apStarted) {
    Serial.println("AP启动失败！");
    while (1) delay(1000);
  }

  Serial.print("主设备AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("隐藏WiFi已启动（需手动输入SSID连接）");
}

// ==================== Web服务器初始化 ====================
void initWebServer() {
  server.on("/", handleRoot);                   // 主页
  server.on("/scanBt", handleScanBt);           // 扫描蓝牙设备
  server.on("/connectBt", handleConnectBt);     // 连接蓝牙设备
  server.on("/getStatus", handleGetStatus);     // 获取设备状态
  server.begin();
  Serial.println("Web服务器启动，访问 http://192.168.5.1");
}

// ==================== 蓝牙HFP初始化 ====================
void initHFP() {
  // 初始化蓝牙控制器
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
    Serial.println("蓝牙控制器初始化失败");
    return;
  }
  if (esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK) {
    Serial.println("启用经典蓝牙失败");
    return;
  }

  // 初始化Bluedroid
  if (esp_bluedroid_init() != ESP_OK || esp_bluedroid_enable() != ESP_OK) {
    Serial.println("Bluedroid初始化失败");
    return;
  }

  // 设置设备名称
  esp_bt_dev_set_device_name(HFP_DEVICE_NAME);

  // 注册HFP AG回调（需实现回调函数处理连接/音频事件）
  // esp_hfp_ag_register_callback(hfp_ag_callback);  // 需自定义回调
  // esp_hfp_ag_init();
  // esp_hfp_ag_enable();

  Serial.println("HFP蓝牙初始化完成（等待连接）");
}

// ==================== 音频发送（主设备发送到从设备） ====================
void sendAudio(const uint8_t* data, size_t len) {
  if (isMuted || !isBtConnected) return;

  // 发送到从设备1和2
  udp.beginPacket(SLAVE1_IP, UDP_PORT);
  udp.write(data, len);
  udp.endPacket();

  udp.beginPacket(SLAVE2_IP, UDP_PORT);
  udp.write(data, len);
  udp.endPacket();
}

// ==================== 音频接收（主设备接收从设备） ====================
void receiveAudio() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;

  IPAddress senderIP = udp.remoteIP();
  uint8_t buffer[MAX_AUDIO_LEN];
  size_t len = udp.read(buffer, MAX_AUDIO_LEN);

  // 根据IP判断从设备1/2
  if (senderIP == SLAVE1_IP && len > 0) {
    memcpy(slave1Audio, buffer, len);
    slave1Len = len;
  } else if (senderIP == SLAVE2_IP && len > 0) {
    memcpy(slave2Audio, buffer, len);
    slave2Len = len;
  }
}

// ==================== 静音按键处理 ====================
void handleMuteButton() {
  static unsigned long lastDebounce = 0;
  static bool lastState = HIGH;
  const unsigned long debounceDelay = 200;

  int currentState = digitalRead(MUTE_BUTTON);

  // 按键防抖
  if (currentState != lastState) {
    lastDebounce = millis();
  }

  if ((millis() - lastDebounce) > debounceDelay) {
    if (currentState == LOW) {  // 按键按下
      isMuted = !isMuted;
      Serial.print("静音状态: ");
      Serial.println(isMuted ? "已静音" : "未静音");
    }
  }

  lastState = currentState;
}

// ==================== Web回调函数 ====================
// 主页HTML
void handleRoot() {
  String html = R"(
    <html>
      <head><title>主设备 - 蓝牙对讲控制</title></head>
      <body>
        <h1>ESP32主设备控制</h1>
        <p>蓝牙状态: <span id="btStatus">)" + btConnectedDevice + R"(</span></p >
        <p>静音状态: <span id="muteStatus">)" + String(isMuted ? "已静音" : "未静音") + R"(</span></p >
        <button onclick="scanBtDevices()">扫描蓝牙设备</button>
        <div id="btDevices"></div>
        <script>
          // 扫描蓝牙设备
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

          // 连接蓝牙设备
          function connectBt(addr) {
            fetch('/connectBt?addr=' + addr)
              .then(res => res.text())
              .then(msg => alert(msg));
          }

          // 定时刷新状态
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

// 扫描蓝牙设备（返回JSON）
void handleScanBt() {
  Serial.println("开始扫描蓝牙设备...");
  String devices = scanBtDevices();  // 实现见下方
  server.send(200, "application/json", devices);
}

// 连接蓝牙设备
void handleConnectBt() {
  if (server.hasArg("addr")) {
    String addr = server.arg("addr");
    bool success = connectBtDevice(addr.c_str());
    server.send(200, "text/plain", success ? "连接成功" : "连接失败");
  } else {
    server.send(400, "text/plain", "缺少设备地址");
  }
}

// 获取设备状态（JSON）
void handleGetStatus() {
  String json = "{\"btDevice\":\"" + btConnectedDevice + "\",\"isMuted\":" + String(isMuted) + "}";
  server.send(200, "application/json", json);
}

// ==================== 蓝牙工具函数 ====================
// 扫描蓝牙设备（返回JSON数组）
String scanBtDevices() {
  // 实际需调用ESP32蓝牙扫描API，这里模拟返回
  // 格式: [{"name":"耳机1","addr":"00:1A:7D:DA:71:13"},...]
  String json = "[";
  
  // 模拟扫描结果（实际需替换为真实扫描逻辑）
  json += "{\"name\":\"Headset-1\",\"addr\":\"00:1A:7D:DA:71:13\"},";
  json += "{\"name\":\"Headset-2\",\"addr\":\"00:1B:2C:3D:4E:5F\"}";
  
  json += "]";
  return json;
}

// 连接蓝牙设备（返回是否成功）
bool connectBtDevice(const char* addr) {
  // 实际需调用HFP配对/连接API，这里模拟
  Serial.print("尝试连接蓝牙设备: ");
  Serial.println(addr);
  
  // 模拟连接成功
  isBtConnected = true;
  btConnectedDevice = "Headset (" + String(addr) + ")";
  return true;
}