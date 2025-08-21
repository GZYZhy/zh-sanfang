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
#define K "zhsf-2"
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
Q Z=S;
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
  f(S);
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
void j(){
  int w=V.parsePacket();
  if(w<=0)return;
  if(V.remoteIP()!=D)return;
  uint8_t y[J];
  size_t z=V.read(y,J);
  if(z>0&&Y)W.write(y,z);
}

void l(){
  static int aa=0;
  d=J;
  for(int ab=0;ab<J;ab++){
    b[ab]=128+127*sin(2*PI*800*aa/8000.0);
    aa++;
    if(aa>=8000)aa=0;
  }
}

// ==================== LED状态更新 ====================
void k(){
  if(X){digitalWrite(Qq,HIGH);digitalWrite(Rr,LOW);}
  else{digitalWrite(Qq,LOW);digitalWrite(Rr,HIGH);}
  switch(a){
    case 0:digitalWrite(O,LOW);digitalWrite(P,LOW);break;
    case 1:digitalWrite(O,HIGH);digitalWrite(P,LOW);break;
    case 2:digitalWrite(O,LOW);digitalWrite(P,HIGH);break;
  }
}

// ==================== 静音按键处理 ====================
void i(){
  static unsigned long ac=0;
  static bool ad=LOW,ae=false;
  const unsigned long af=200;
  int ag=digitalRead(M);
  if(ag!=ad)ac=millis();
  if(millis()-ac>af){
    if(ag==HIGH&&!ae){ae=true;X=!X;}
    else if(ag==LOW&&ae)ae=false;
  }
  ad=ag;
}

// ==================== Web回调函数 ====================
// 纯JSON API，无HTML界面

// 以下Web回调函数与主设备相同
void o(){U.send(200,"application/json",nn());}
void p(){
  if(U.hasArg("addr")){
    String ah=U.arg("addr");
    bool ai=qq(ah.c_str());
    U.send(200,"text/plain",ai?"OK":"FAIL");
  }else U.send(200,"text/plain","FAIL");
}
void q(){char aj[50];sprintf(aj,"{\"bt\":%d,\"mute\":%d}",Y?1:0,X?1:0);U.send(200,"application/json",aj);}
void r(){if(U.hasArg("state")){String ak=U.arg("state");X=(ak=="1");U.send(200,"text/plain","OK");}else U.send(200,"text/plain","FAIL");}
void s(){if(U.hasArg("state")){String al=U.arg("state");a=(al=="red")?1:(al=="green")?2:0;U.send(200,"text/plain","OK");}else U.send(200,"text/plain","FAIL");}

// 蓝牙工具函数
String nn(){return "[{\"name\":\"H1\",\"addr\":\"00:1A:7D:DA:71:13\"},{\"name\":\"H2\",\"addr\":\"00:1B:2C:3D:4E:5F\"}]";}
bool qq(const char* am){delay(1000);return W.connected();}
