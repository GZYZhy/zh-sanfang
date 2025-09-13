#include "AudioMqtt.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <string.h>
#include <limits.h>
#include <Preferences.h>

extern const char* DEVICE_ID;
extern const char* LIGHT_CONTROL_TOPIC;
extern const char* mqtt_user;
extern const char* mqtt_password;

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastAudioReceivedTime = 0;

int min(int a, int b) {
  return (a < b) ? a : b;
}

void handleLightControl(const char* payload, unsigned int length) {
  if (length < 3) return; // 最小格式: id:mode

  String message = String(payload, length);
  int colonPos = message.indexOf(':');
  if (colonPos == -1) return;

  String targetID = message.substring(0, colonPos);
  String modeStr = message.substring(colonPos + 1);

  // 检查是否是发给本设备的指令
  if (targetID != String(DEVICE_ID)) return;

  // 检查是否是查询指令
  if (modeStr == "n") {
    // 查询当前灯光状态
    LightMode currentMode = getCurrentMode();
    String response = "[re]" + String(DEVICE_ID) + ":" + String(currentMode);
    client.publish(LIGHT_CONTROL_TOPIC, response.c_str());
    Serial.print("Light query response: ");
    Serial.println(response);
    return;
  }

  // 检查是否是麦克风查询指令
  if (modeStr == "m") {
    // 查询当前麦克风状态
    String status = micEnabled ? "k" : "g"; // k=开麦, g=关麦
    String response = "[re]" + String(DEVICE_ID) + ":" + status;
    client.publish(LIGHT_CONTROL_TOPIC, response.c_str());
    Serial.print("Mic status query response: ");
    Serial.println(response);
    return;
  }

  // 检查是否是麦克风控制指令
  if (modeStr == "g") {
    // 关麦指令
    if (micEnabled) {
      micEnabled = false;
      Serial.println("Mic: Turned OFF via MQTT command");

    } else {
      Serial.println("Mic: Already OFF, no change");
    }
    return;
  }

  if (modeStr == "k") {
    // 开麦指令
    if (!micEnabled) {
      micEnabled = true;
      Serial.println("Mic: Turned ON via MQTT command");
      // LED状态由mqtt.ino主循环统一控制
    } else {
      Serial.println("Mic: Already ON, no change");
    }
    return;
  }

  // 检查是否是音量查询指令
  if (modeStr == "nv") {
    // 查询当前音量水平
    String response = "[re]" + String(DEVICE_ID) + ":v" + String(output_volume);
    client.publish(LIGHT_CONTROL_TOPIC, response.c_str());
    Serial.print("Volume query response: ");
    Serial.println(response);
    return;
  }

  // 检查是否是音量设置指令（格式：vXX，其中XX是0-100的音量值）
  if (modeStr.startsWith("v")) {
    String volumeStr = modeStr.substring(1); // 去掉"v"前缀
    int newVolume = volumeStr.toInt();

    // 验证音量范围
    if (newVolume >= 0 && newVolume <= 100) {
      if (output_volume != newVolume) {
        output_volume = newVolume;
        saveVolumeSetting(output_volume); // 持久化保存
        Serial.print("Volume: Set to ");
        Serial.print(output_volume);
        Serial.println("% via MQTT command");
      } else {
        Serial.println("Volume: Already at requested level, no change");
      }
    } else {
      Serial.print("Volume: Invalid value ");
      Serial.print(newVolume);
      Serial.println(", must be 0-100");
    }
    return;
  }

  // 处理灯光控制指令
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
  Serial.print("] length: ");
  Serial.print(length);
  Serial.print(" bytes, first few bytes: ");
  for (int i = 0; i < min(5, length); i++) {
    Serial.print(payload[i]);
    Serial.print(" ");
  }
  Serial.println();

  // 检查是否是灯控制主题
  if (strcmp(topic, LIGHT_CONTROL_TOPIC) == 0) {
    handleLightControl((const char*)payload, length);
    return;
  }

  // 检查是否是音频主题
  if (strcmp(topic, AUDIO_TOPIC) == 0) {
    // 检查消息长度是否足够包含设备ID前缀
    if (length > strlen(DEVICE_ID) + 1) {
      // 提取消息中的设备ID
      String message = String((const char*)payload, length);
      int colonPos = message.indexOf(':');
      
      if (colonPos != -1) {
        String sourceID = message.substring(0, colonPos);
        
        // 如果不是自己的消息，才处理音频
        if (sourceID != String(DEVICE_ID)) {
          // 提取音频数据（跳过设备ID和冒号）
          const uint8_t* audioData = payload + colonPos + 1;
          int audioLength = length - colonPos - 1;
          
          // 处理音频数据
          for (int i = 0; i < audioLength; i++) {
            recive_16bit[i] = (audioData[i] - 128) << 5;

            // 应用音量缩放（只影响扬声器输出，不影响麦克风输入）
            int16_t scaled_sample = recive_16bit[i] * output_volume / 100;

            // 确保不会溢出16位范围
            if (scaled_sample > INT16_MAX) scaled_sample = INT16_MAX;
            if (scaled_sample < INT16_MIN) scaled_sample = INT16_MIN;

            output_16bit[i * 2] = scaled_sample;
            output_16bit[i * 2 + 1] = scaled_sample;
          }
          
          I2Swrite(output_16bit, audioLength);
          lastAudioReceivedTime = millis(); // 更新最后接收时间
        } else {
          Serial.println("Ignoring own message");
        }
      }
    }
    return;
  }
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
      client.subscribe(AUDIO_TOPIC,0);
      client.subscribe(LIGHT_CONTROL_TOPIC,0);
      Serial.print("Subscribed to audio topic: ");
      Serial.println(AUDIO_TOPIC);
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
  if(client.connected())
  {
    // 创建带设备ID前缀的消息缓冲区
    int totalLength = strlen(DEVICE_ID) + 1 + len; // ID + ":" + audio data
    uint8_t* message = new uint8_t[totalLength];

    // 添加设备ID和冒号
    memcpy(message, DEVICE_ID, strlen(DEVICE_ID));
    message[strlen(DEVICE_ID)] = ':';

    // 添加音频数据
    memcpy(message + strlen(DEVICE_ID) + 1, data, len);

    // 发布到音频主题
    if(false == client.publish(AUDIO_TOPIC, message, totalLength, 0))
    {
      Serial.println("sendfailed");
    }

    delete[] message;
  }
  else
  {
    Serial.println("not connect");
  }
}

void saveVolumeSetting(int volume)
{
  Preferences prefs;
  prefs.begin("mqtt_config", false);
  prefs.putInt("output_volume", volume);
  prefs.end();

  Serial.print("Volume: Saved ");
  Serial.print(volume);
  Serial.println("% to preferences");
}
