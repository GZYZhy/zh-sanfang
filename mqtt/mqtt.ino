#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include "AudioMqtt.h"
#include "IISAudio.h"
#include "RGBLight.h"
#include <Preferences.h>
#include <WiFiClient.h>
#include <WebServer.h>

const char* mqtt_server = "24.233.0.55";

// WiFiManager实例
WiFiManager wm;
Preferences prefs;

// 存储的WiFi配置
String saved_ssid;
String saved_password;

// MQTT配置参数 - 在主程序中定义
const char* DEVICE_ID = "zhsf_1";
const char* LIGHT_CONTROL_TOPIC = "zhsf/tally";
const char* mqtt_user = "esptalk";
const char* mqtt_password = "zhsanfang";

// vMix配置
bool enable_vmix = false;
String vmix_ip = "";
WiFiClient vmixClient;
bool connectedToVmix = false;
const int VMIX_TCP_PORT = 8099;

// Web服务器配置
WebServer server(80);

// 按键相关变量已在后面重新定义


void wifiInit(void)//智能配网连接WIFI
{
    // 初始化Preferences
    prefs.begin("mqtt_config", false);
    saved_ssid = prefs.getString("wifi_ssid", "");
    saved_password = prefs.getString("wifi_password", "");
    enable_vmix = prefs.getBool("enable_vmix", false);
    vmix_ip = prefs.getString("vmix_ip", "");
    prefs.end();
    
    Serial.println("vMix config: " + String(enable_vmix ? "Enabled" : "Disabled"));
    if (enable_vmix) {
        Serial.println("vMix IP: " + vmix_ip);
    }

    Serial.println("WiFi: Connecting");
    
    // 如果有保存的凭据，先尝试连接
    if (saved_ssid.length() > 0) {
        Serial.println("WiFi: Trying saved network");
        WiFi.mode(WIFI_STA);
        WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
        
        // 等待10秒连接
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
            delay(500);
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi: Connected");
            Serial.println("IP: " + WiFi.localIP().toString());
            return;
        }
        Serial.println("\nWiFi: Saved network failed");
    }

    // 启动配网模式
    Serial.println("WiFi: Starting config portal");
    Serial.println("Connect to: MQTT-Device");
    Serial.println("Web: 192.168.4.1");
    
    WiFi.mode(WIFI_AP_STA);
    wm.setConfigPortalTimeout(180); // 3分钟超时
    
    // 添加vMix配置参数
    WiFiManagerParameter p_enable_vmix("enable_vmix", "Enable vMix", "0", 2);
    WiFiManagerParameter p_vmix_ip("vmix_ip", "vMix IP", "", 40);
    wm.addParameter(&p_enable_vmix);
    wm.addParameter(&p_vmix_ip);
    
    // 启动配置门户（使用设备ID作为SSID）
    String configPortalSSID = "zhsf_" + String(DEVICE_ID);
    if (!wm.startConfigPortal(configPortalSSID.c_str())) {
        Serial.println("WiFi: Config failed, restarting");
        delay(3000);
        ESP.restart();
    } else {
        Serial.println("WiFi: Connected");
        Serial.println("IP: " + WiFi.localIP().toString());
        
        // 保存新的WiFi凭据和vMix配置
        String newSSID = WiFi.SSID();
        String newPass = WiFi.psk();
        String newEnableVmix = String(p_enable_vmix.getValue());
        String newVmixIp = String(p_vmix_ip.getValue());
        newVmixIp.trim();
        
        prefs.begin("mqtt_config", false);
        prefs.putString("wifi_ssid", newSSID);
        prefs.putString("wifi_password", newPass);
        prefs.putBool("enable_vmix", newEnableVmix == "1");
        prefs.putString("vmix_ip", newVmixIp);
        prefs.end();
        
        // 更新运行时变量
        enable_vmix = (newEnableVmix == "1");
        vmix_ip = newVmixIp;
        
        Serial.println("Config: Saved");
        Serial.println("vMix: " + String(enable_vmix ? "Enabled" : "Disabled"));
        if (enable_vmix) {
            Serial.println("vMix IP: " + vmix_ip);
        }
    }
}
// 闭麦键检测函数 - 检测短按事件
bool checkMuteButtonPress(void) {
  static bool lastReading = HIGH;
  static unsigned long pressStartTime = 0;
  static bool pressDetected = false;

  int reading = digitalRead(BTN);

  // 检测按键按下开始
  if (reading == LOW && lastReading == HIGH) {
    pressStartTime = millis();
    pressDetected = true;
    Serial.println("Btn: Press detected");
  }

  // 检测按键释放
  if (reading == HIGH && lastReading == LOW && pressDetected) {
    unsigned long pressDuration = millis() - pressStartTime;

    // 如果按键时间小于500ms，认为是短按（切换状态）
    if (pressDuration < 500) {
      micEnabled = !micEnabled; // 切换麦克风状态
      Serial.print("Mic ");
      Serial.println(micEnabled ? "ON (开麦模式)" : "OFF (闭麦模式)");

      // 根据麦克风状态设置LED
      if (micEnabled) {
        digitalWrite(LED, LOW); // 开麦时LED常亮（点亮）
      } else {
        digitalWrite(LED, HIGH); // 闭麦时LED熄灭
      }

      pressDetected = false;
      return true; // 返回true表示发生了状态切换
    }
    pressDetected = false;
  }

  lastReading = reading;
  return false; // 没有状态切换
}

// vMix连接函数
void connectToVmix() {
  if (!enable_vmix || vmix_ip.length() == 0) {
    Serial.println("vMix: Disabled or no IP configured");
    connectedToVmix = false;
    return;
  }
  
  if (vmixClient.connect(vmix_ip.c_str(), VMIX_TCP_PORT)) {
    Serial.println("vMix: TCP connected");
    delay(200);
    while (vmixClient.available()) vmixClient.readStringUntil('\n');
    vmixClient.println("SUBSCRIBE TALLY");
    connectedToVmix = true;
    Serial.println("vMix: Subscribed to TALLY");
  } else {
    Serial.println("vMix: Connect failed");
    connectedToVmix = false;
  }
}

// 处理vMix tally消息
void handleVmixTally() {
  if (connectedToVmix && vmixClient.available()) {
    String line = vmixClient.readStringUntil('\n');
    line.trim();
    
    if (line.startsWith("TALLY OK")) {
      String tally = line.substring(9);
      
      // 获取输入编号（DEVICE_ID的最后一位数字）
      int inputNumber = 1;
      char lastChar = DEVICE_ID[strlen(DEVICE_ID) - 1];
      if (isdigit(lastChar)) {
        inputNumber = lastChar - '0';
      }
      
      // 解析tally状态
      char state = (inputNumber - 1 < tally.length()) ? tally.charAt(inputNumber - 1) : '0';
      
      if (state == '1') {
        // Program状态 - 红灯常亮（覆盖MQTT控制）
        setLightMode(MODE_RED);
        Serial.println("vMix: Program (RED)");
      } else if (state == '2') {
        // Preview状态 - 黄灯闪烁（覆盖MQTT控制）
        setLightMode(MODE_YELLOW_FLASH);
        Serial.println("vMix: Preview (YELLOW FLASH)");
      } else {
        // 未激活状态 - 不覆盖MQTT控制，允许MQTT控制灯状态
        Serial.println("vMix: Inactive (MQTT control allowed)");
      }
    }
  }
}

void setup(void)
{
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("System: Starting");
  
  wifiInit();

  I2SInit();
  Serial.println("Audio: Ready");

  pinMode(BTN, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  digitalWrite(LED,LOW);
  
  RGBLightInit();
  Serial.println("RGB: Ready");
  
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  Serial.println("MQTT: Ready");
  
  // 连接vMix
  if (enable_vmix) {
    connectToVmix();
  }

  // 初始化Web服务器（只在配网成功后启用）
  if (WiFi.status() == WL_CONNECTED) {
    server.on("/forget", HTTP_GET, []() {
      // 忘记网络配置并重启进入配网模式
      prefs.begin("mqtt_config", false);
      prefs.remove("wifi_ssid");
      prefs.remove("wifi_password");
      prefs.end();
      
      server.send(200, "text/plain", "OK");
      delay(1000);
      ESP.restart();
    });
    
    server.begin();
    Serial.println("Web server started");
    Serial.print("Access http://");
    Serial.print(WiFi.localIP());
    Serial.println("/forget to reset WiFi config");
  }
}


// 双工模式变量（发送和接收可以同时进行）
bool sendOver=1;//发送完成标志位
bool recOver=0;//接受完成标志位

// 闭麦键状态变量
bool micEnabled = false; // 麦克风状态：false=闭麦只收听，true=开麦既收听又发送
bool lastButtonState = HIGH; // 上一次按键状态
unsigned long lastDebounceTime = 0; // 消抖计时器

void loop(void)
{
  // 处理Web服务器请求（只在配网成功后启用）
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
  
  if (!client.connected()) {//判断是否连接
    reconnect();
  }
  client.loop();
  
  // 处理vMix连接和消息
  if (enable_vmix) {
    if (!connectedToVmix) {
      static unsigned long lastVmixTry = 0;
      if (millis() - lastVmixTry > 5000) {
        connectToVmix();
        lastVmixTry = millis();
      }
    } else {
      handleVmixTally();
    }
  }
  
  updateLight(); // 更新RGB灯状态（处理闪烁等效果）

  // 检查闭麦键按下事件
  checkMuteButtonPress();

  // 双工模式：根据麦克风状态决定是否发送音频
  if (micEnabled) {
    // 麦克风开启：既发送又接收
    int samples_read = I2Sread(samples_16bit,128);//读取数据

    // 只有在成功读取到数据时才发送
    if (samples_read > 0) {
      covert_bit(samples_16bit,samples_8bit,samples_read);//发送时转换为8位
      sendData(samples_8bit,samples_read);//发射数据
    }

    // 如果有接收音频，LED闪烁；否则LED常亮（表示麦克风开启）
    if (millis() - lastAudioReceivedTime < 500) {
      // 闪烁效果：每100ms切换一次状态
      static unsigned long lastBlinkTime = 0;
      static bool ledState = HIGH;

      if (millis() - lastBlinkTime > 100) {
        ledState = !ledState;
        digitalWrite(LED, ledState ? HIGH : LOW);
        lastBlinkTime = millis();
      }
    } else {
      digitalWrite(LED, LOW); // 麦克风开启但无接收音频时，LED常亮（点亮）
    }
  } else {
    // 麦克风关闭：只接收
    // 实时检测音频活动：如果500ms内有音频数据收到，则闪烁LED
    if (millis() - lastAudioReceivedTime < 500) {
      // 闪烁效果：每100ms切换一次状态
      static unsigned long lastBlinkTime = 0;
      static bool ledState = HIGH;

      if (millis() - lastBlinkTime > 100) {
        ledState = !ledState;
        digitalWrite(LED, ledState ? HIGH : LOW);
        lastBlinkTime = millis();
      }
    } else {
      digitalWrite(LED, HIGH); // 麦克风关闭且无音频活动，LED熄灭
    }
  }  
}
