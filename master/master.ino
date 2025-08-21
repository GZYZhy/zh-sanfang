#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <BluetoothSerial.h>

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
  pinMode(M,INPUT);
  pinMode(N,OUTPUT);
  pinMode(O,OUTPUT);
  pinMode(P,OUTPUT);
  digitalWrite(N,HIGH);
  digitalWrite(O,LOW);
  digitalWrite(P,LOW);
  f(R);
  g();
  h();
  if(!V.begin(I))Serial.println("UF");else Serial.println("U");


}

void loop(){
  U.handleClient();
  i();
  j();
  Y=W.connected();
  k();
  if(Y){
    if(d>0&&e>0){
      uint8_t l[J];
      for(int m=0;m<J;m++)l[m]=(b[m]+c[m])/2;
      W.write(l,J);
      d=0;e=0;
    }else if(d>0){W.write(b,d);d=0;}
    else if(e>0){W.write(c,e);e=0;}
  }
}

void f(Q n){
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(D,G,H);
  if(!WiFi.softAP(A,B,C,0)){while(1)delay(1000);}
  Serial.println("A");
}

void h(){
  U.on("/scanBt",o);
  U.on("/connectBt",p);
  U.on("/getStatus",q);
  U.on("/setMute",r);
  U.on("/setLed",s);
  U.begin();
  Serial.println("W");
}

void g(){
  if(!W.begin(K)){
    Serial.println("BF");
    return;
  }
  Serial.println("B");
}

void t(const uint8_t* u,size_t v){
  if(X||!Y)return;
  V.beginPacket(E,I);V.write(u,v);V.endPacket();
  V.beginPacket(F,I);V.write(u,v);V.endPacket();
}

void j(){
  int w=V.parsePacket();
  if(w<=0)return;
  IPAddress x=V.remoteIP();
  uint8_t y[J];
  size_t z=V.read(y,J);
  if(x==E&&z>0){memcpy(b,y,z);d=z;}
  else if(x==F&&z>0){memcpy(c,y,z);e=z;}
}

void k(){
  static unsigned long aa=0;
  if(X)digitalWrite(N,LOW);
  else if(millis()-aa>500){digitalWrite(N,!digitalRead(N));aa=millis();}
  if(a==0){digitalWrite(O,LOW);digitalWrite(P,LOW);}
  else if(a==1){digitalWrite(O,HIGH);digitalWrite(P,LOW);}
  else if(a==2){digitalWrite(O,LOW);digitalWrite(P,HIGH);}
}

void i(){
  static unsigned long ab=0;
  static bool ac=LOW,ad=false;
  const unsigned long ae=200;
  int af=digitalRead(M);
  if(af!=ac)ab=millis();
  if(millis()-ab>ae){
    if(af==HIGH&&!ad){ad=true;X=!X;Serial.println(X?"M1":"M0");}
    else if(af==LOW&&ad)ad=false;
  }
  ac=af;
}

void o(){U.send(200,"application/json",nn());}
void p(){if(U.hasArg("addr"))U.send(200,"text/plain",qq(U.arg("addr").c_str())?"OK":"FAIL");}
void q(){char ag[50];sprintf(ag,"{\"bt\":%d,\"mute\":%d}",Y?1:0,X?1:0);U.send(200,"application/json",ag);}
void r(){if(U.hasArg("state")){X=(U.arg("state")=="1");Serial.println(X?"M1":"M0");U.send(200,"text/plain","OK");}}
void s(){if(U.hasArg("state")){String ah=U.arg("state");a=(ah=="red")?1:(ah=="green")?2:0;U.send(200,"text/plain","OK");}}

String nn(){return "[{\"name\":\"H1\",\"addr\":\"00:1A:7D:DA:71:13\"},{\"name\":\"H2\",\"addr\":\"00:1B:2C:3D:4E:5F\"}]";}
bool qq(const char* ai){Serial.print("C:");Serial.println(ai);delay(1000);return W.connected();}