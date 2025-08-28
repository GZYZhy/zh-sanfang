#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include "AudioMqtt.h"
#include "IISAudio.h"
#include "RGBLight.h"
#include <Preferences.h>

const char* mqtt_server = "24.233.0.55";//这是树莓的MQTT服务器地址

// WiFiManager实例
WiFiManager wm;
Preferences prefs;

// 存储的WiFi配置
String saved_ssid;
String saved_password;
int device_suffix = 1; // 设备后缀编号1-10

// MQTT配置参数 - 在主程序中定义
const char* DEVICE_ID = "zhsf_1";
const char* LIGHT_CONTROL_TOPIC = "zhsf/tally";
const char* mqtt_user = "esptalk";
const char* mqtt_password = "zhsanfang";

bool buttonState;          // 当前按键状态
bool lastButtonState = HIGH; // 上一次按键状态
bool debouncedState = HIGH;  // 消抖后的按键状态
#define debounceDelay  50// 消抖时间间隔（毫秒）

// 计时变量
unsigned long lastDebounceTime = 0;


void wifiInit(void)//智能配网连接WIFI
{
    // 初始化Preferences
    prefs.begin("mqtt_config", false);
    saved_ssid = prefs.getString("wifi_ssid", "");
    saved_password = prefs.getString("wifi_password", "");
    device_suffix = prefs.getInt("device_suffix", 1);
    prefs.end();

    // 更新设备ID
    String new_device_id = "zhsf_" + String(device_suffix);
    DEVICE_ID = new_device_id.c_str();
    
    Serial.println("Device ID: " + String(DEVICE_ID));
    
    // 确保MQTT使用正确的设备ID
    if (client.connected()) {
        client.disconnect();
        Serial.println("MQTT: Disconnected for ID refresh");
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

    // 添加设备后缀选择参数
    WiFiManagerParameter custom_suffix("suffix", "设备后缀编号 (1-10)", String(device_suffix).c_str(), 2);
    wm.addParameter(&custom_suffix);

    // 启动配网模式
    Serial.println("WiFi: Starting config portal");
    Serial.println("Connect to: ZhangDeSanFang");
    
    WiFi.mode(WIFI_AP_STA);
    wm.setConfigPortalTimeout(180);
    wm.setAPCallback([](WiFiManager* mgr) {
        Serial.println("AP: ZhangDeSanFang");
        Serial.println("Web: 192.168.4.1");
    });
    
    // 启动配置门户
    if (!wm.startConfigPortal("ZhangDeSanFang")) {
        Serial.println("WiFi: Config failed, restarting");
        delay(3000);
        ESP.restart();
    } else {
        Serial.println("WiFi: Connected");
        Serial.println("IP: " + WiFi.localIP().toString());
        
        // 获取用户输入的设备后缀
        int new_suffix = String(custom_suffix.getValue()).toInt();
        if (new_suffix >= 1 && new_suffix <= 10) {
            device_suffix = new_suffix;
        }
        
        // 保存新的配置
        String newSSID = WiFi.SSID();
        String newPass = WiFi.psk();
        
        prefs.begin("mqtt_config", false);
        prefs.putString("wifi_ssid", newSSID);
        prefs.putString("wifi_password", newPass);
        prefs.putInt("device_suffix", device_suffix);
        prefs.end();
        
        // 更新设备ID
        String new_device_id = "zhsf_" + String(device_suffix);
        DEVICE_ID = new_device_id.c_str();
        
        Serial.println("Config: Saved");
        Serial.println("Device ID: " + String(DEVICE_ID));
        
        // 强制MQTT重新连接以使用新的设备ID
        if (client.connected()) {
            client.disconnect();
            Serial.println("MQTT: Disconnected for ID change");
        }
    }
}
bool  BtnisPressed(void)//按键是否按下
{
  int reading = digitalRead(BTN);
  
  // 检查状态是否变化（由于噪声或按下）
  if (reading != lastButtonState) {
    // 重置消抖计时器
    lastDebounceTime = millis();
  }
  else return 0;
  // 检查消抖时间是否已过
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // 如果按键状态与当前消抖状态不同
    if (reading != debouncedState) {
      debouncedState = reading;
      
      // 只有当状态稳定为LOW时才视为有效按下
      if (debouncedState == LOW) {
        Serial.println("Btn: Pressed");
        return 1;
        // 这里可以添加按键按下后要执行的操作
      }
      else return 0;
    }
    else return 0;
  }
else return 0;
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
}


bool sendOver=1;//发送完成标志位
bool recOver=0;//接受完成标志位
bool speakOut;//0代表对外讲话，1代表收听

void loop(void)
{
  if (!client.connected()) {//判断是否连接
    reconnect();
  }
  client.loop();
  
  updateLight(); // 更新RGB灯状态（处理闪烁等效果）
  
  if(BtnisPressed())//按下按键发射数据
  {
    speakOut=0;
    digitalWrite(LED,LOW);//发射时开灯
    int samples_read = I2Sread(samples_16bit,128);//读取数据
    covert_bit(samples_16bit,samples_8bit,samples_read);//发送时转换为8位
    sendData(samples_8bit,samples_read);//发射数据
  }
  else
  {
    delay(28);//经过一段延时再判断，接收数据并且播放也需要时间
    speakOut=1;
    if(recOver)
    {
      recOver=0;
      digitalWrite(LED, LOW); // 接收到消息时点亮LED
      delay(100); // 短暂点亮
      digitalWrite(LED, HIGH); // 然后熄灭，实现闪烁效果
    }
    else
    {
      digitalWrite(LED,HIGH);//没有接收到消息，也没有发射，关灯
      i2s_zero_dma_buffer(SPK_I2S_PORT);//清空DMA中缓存的数据，你可以尝试一下不清空（注释这行）是什么效果
    }
  }  
}
