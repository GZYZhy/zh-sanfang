#include <WiFi.h>
#include <PubSubClient.h>
#include "AudioMqtt.h"
#include "IISAudio.h"
#include "RGBLight.h"

const char* ssid     = "Netcore-9E40DE";//修改为你的WIFI账号与密码
const char* password = "13400243126ybz";
const char* mqtt_server = "24.233.0.55";//这是树莓的MQTT服务器地址

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


void wifiInit(void)//连接WIFI
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println(WiFi.localIP());
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
        Serial.println("按键按下!");
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
  wifiInit();

  I2SInit();//I2S初始化 
  Serial.println("I2SInit over!");

  pinMode(BTN, INPUT_PULLUP);//按键
  pinMode(LED, OUTPUT);//指示灯
  digitalWrite(LED,LOW);
  
  RGBLightInit(); // 初始化RGB灯
  Serial.println("RGBLight initialized!");
  
  client.setServer(mqtt_server, 1883);//mqtt配置
  client.setCallback(callback);//绑定回调函数
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
