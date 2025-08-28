#ifndef _AUDIOMQTT_
#define _AUDIOMQTT_

#include <PubSubClient.h>
#include <WiFi.h>
#include "IISAudio.h"
#include "RGBLight.h"

extern PubSubClient client;
extern bool recOver;
extern unsigned long lastAudioReceivedTime;

#define AUDIO_TOPIC "zhsf/audio"

extern const char* DEVICE_ID;
extern const char* LIGHT_CONTROL_TOPIC;
extern const char* mqtt_user;
extern const char* mqtt_password;

void callback(char* topic, byte* payload, unsigned int length);
void reconnect() ;
void sendData(const uint8_t  *data, uint16_t len);
void handleLightControl(const char* payload, unsigned int length);

#endif
