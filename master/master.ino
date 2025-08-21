#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <BluetoothSerial.h>

// ==================== WiFi配置 ====================
// 隐藏WiFi热点信息
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
#define K "zhsf-1"
#define L "0000"
#define M 14
#define N 22
#define O 25
#define P 26

enum Q{R,S,T};
WebServer U(80);
WiFiUDP V;
BluetoothSerial W;
bool X=false,Y=false;
Q Z=R;
int a=0;
uint8_t b[J]={0},c[J]={0};
size_t d=0,e=0;

void setup() {
  Serial.begin(115200);
  pinMode(MUTE_BUTTON,INPUT);
  pinMode(BUILTIN_LED,OUTPUT);
  pinMode(CONTROL_LED_R,OUTPUT);
  pinMode(CONTROL_LED_G,OUTPUT);
  digitalWrite(BUILTIN_LED,HIGH);
  digitalWrite(CONTROL_LED_R,LOW);
  digitalWrite(CONTROL_LED_G,LOW);

  initWiFi(DEV_MASTER);
  initHFP();
  initWebServer();
  if(!udp.begin(UDP_PORT))Serial.println("UDP F");else Serial.println("UDP");


}

void loop(){
  server.handleClient();
  handleMuteButton();
  receiveAudio();
  isBtConnected=SerialBT.connected();
  updateLedStatus();

  // 无论是否静音，都要播放从设备的音频（接收音频不受静音影响）
  if(isBtConnected){
    if(slave1Len>0&&slave2Len>0){
      uint8_t mixedAudio[MAX_AUDIO_LEN];
      for(int i=0;i<MAX_AUDIO_LEN;i++)mixedAudio[i]=(slave1Audio[i]+slave2Audio[i])/2;
      SerialBT.write(mixedAudio,MAX_AUDIO_LEN);
      slave1Len=0;slave2Len=0;
    }else if(slave1Len>0){SerialBT.write(slave1Audio,slave1Len);slave1Len=0;}
    else if(slave2Len>0){SerialBT.write(slave2Audio,slave2Len);slave2Len=0;}
  }
}

void initWiFi(DeviceType type){
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(MASTER_IP,GATEWAY,SUBNET);
  if(!WiFi.softAP(AP_SSID,AP_PASS,AP_CHANNEL,0)){while(1)delay(1000);}
  Serial.println("AP");
}

void initWebServer(){
  server.on("/scanBt",handleScanBt);
  server.on("/connectBt",handleConnectBt);
  server.on("/getStatus",handleGetStatus);
  server.on("/setMute",handleSetMute);
  server.on("/setLed",handleSetLed);
  server.begin();
  Serial.println("Web");
}

void initHFP(){
  if(!SerialBT.begin(HFP_DEVICE_NAME)){
    Serial.println("BT F");
    return;
  }
  Serial.println("BT");
}

void sendAudio(const uint8_t* data,size_t len){
  if(isMuted||!isBtConnected)return;
  udp.beginPacket(SLAVE1_IP,UDP_PORT);udp.write(data,len);udp.endPacket();
  udp.beginPacket(SLAVE2_IP,UDP_PORT);udp.write(data,len);udp.endPacket();
}

void receiveAudio(){
  int packetSize=udp.parsePacket();
  if(packetSize<=0)return;
  IPAddress senderIP=udp.remoteIP();
  uint8_t buffer[MAX_AUDIO_LEN];
  size_t len=udp.read(buffer,MAX_AUDIO_LEN);
  if(senderIP==SLAVE1_IP&&len>0){memcpy(slave1Audio,buffer,len);slave1Len=len;}
  else if(senderIP==SLAVE2_IP&&len>0){memcpy(slave2Audio,buffer,len);slave2Len=len;}
}

void updateLedStatus(){
  static unsigned long lastBlink=0;
  if(isMuted)digitalWrite(BUILTIN_LED,LOW);
  else if(millis()-lastBlink>500){digitalWrite(BUILTIN_LED,!digitalRead(BUILTIN_LED));lastBlink=millis();}
  if(controlLedState==0){digitalWrite(CONTROL_LED_R,LOW);digitalWrite(CONTROL_LED_G,LOW);}
  else if(controlLedState==1){digitalWrite(CONTROL_LED_R,HIGH);digitalWrite(CONTROL_LED_G,LOW);}
  else if(controlLedState==2){digitalWrite(CONTROL_LED_R,LOW);digitalWrite(CONTROL_LED_G,HIGH);}
}

void handleMuteButton(){
  static unsigned long lastDebounce=0;
  static bool lastState=LOW,buttonPressed=false;
  const unsigned long debounceDelay=200;
  int currentState=digitalRead(MUTE_BUTTON);
  if(currentState!=lastState)lastDebounce=millis();
  if(millis()-lastDebounce>debounceDelay){
    if(currentState==HIGH&&!buttonPressed){buttonPressed=true;isMuted=!isMuted;Serial.println(isMuted?"M1":"M0");}
    else if(currentState==LOW&&buttonPressed)buttonPressed=false;
  }
  lastState=currentState;
}

void handleScanBt(){server.send(200,"application/json",scanBtDevices());}
void handleConnectBt(){if(server.hasArg("addr"))server.send(200,"text/plain",connectBtDevice(server.arg("addr").c_str())?"OK":"FAIL");}

void handleGetStatus(){char json[50];sprintf(json,"{\"bt\":%d,\"mute\":%d}",isBtConnected?1:0,isMuted?1:0);server.send(200,"application/json",json);}
void handleSetMute(){if(server.hasArg("state")){isMuted=(server.arg("state")=="1");Serial.println(isMuted?"M1":"M0");server.send(200,"text/plain","OK");}}
void handleSetLed(){if(server.hasArg("state")){String state=server.arg("state");controlLedState=(state=="red")?1:(state=="green")?2:0;server.send(200,"text/plain","OK");}}

String scanBtDevices(){return "[{\"name\":\"H1\",\"addr\":\"00:1A:7D:DA:71:13\"},{\"name\":\"H2\",\"addr\":\"00:1B:2C:3D:4E:5F\"}]";}
bool connectBtDevice(const char* addr){Serial.print("C:");Serial.println(addr);delay(1000);return SerialBT.connected();}