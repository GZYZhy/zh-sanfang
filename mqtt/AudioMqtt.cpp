#include "AudioMqtt.h"

extern const char* DEVICE_ID;
extern const char* LIGHT_CONTROL_TOPIC;
extern const char* mqtt_user;
extern const char* mqtt_password;

WiFiClient espClient;
PubSubClient client(espClient);

void handleLightControl(const char* payload, unsigned int length) {
  if (length < 3) return; // 最小格式: id:mode
  
  String message = String(payload, length);
  int colonPos = message.indexOf(':');
  if (colonPos == -1) return;
  
  String targetID = message.substring(0, colonPos);
  String modeStr = message.substring(colonPos + 1);
  
  // 检查是否是发给本设备的指令
  if (targetID != DEVICE_ID) return;
  
  int mode = modeStr.toInt();
  if (mode >= MODE_OFF && mode <= MODE_RED_BLUE_ALTERNATE) {
    setLightMode(static_cast<LightMode>(mode));
    Serial.print("Light mode set to: ");
    Serial.println(mode);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // 检查是否是灯控制主题
  if (strcmp(topic, LIGHT_CONTROL_TOPIC) == 0) {
    handleLightControl((const char*)payload, length);
    return;
  }

  // 原有的音频处理逻辑
  for (int i = 0; i < length; i++)//接收到信息后转换为16bit，补充左右声道，写入到I2S
  {
    recive_16bit[i] = (payload[i] - 128) << 5;
    output_16bit[i * 2 ] = recive_16bit[i];
    output_16bit[i * 2 + 1] = recive_16bit[i];
  }
  
  I2Swrite(output_16bit,length);
}
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // 使用固定的设备ID作为客户端ID
    if (client.connect(DEVICE_ID, mqtt_user, mqtt_password)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      // ... and resubscribe
      client.subscribe(LOCALTOPIC,0);
      client.subscribe(LIGHT_CONTROL_TOPIC,0);
      Serial.print("Subscribed to light control topic: ");
      Serial.println(LIGHT_CONTROL_TOPIC);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
void sendData(const uint8_t  *data, uint16_t len)
{
  if(MQTT_CONNECTED==client.state())
  {
    if(false  == client.publish(PUBTOPIC, data,len,0))
    {
      Serial.println("sendfailed");
    }

  }
  else
  {
    Serial.println("not connect");
  }
    
}
