#include <esp_now.h>
#include "Audioi2s.h"
#include "transport.h"

bool sendOver=1;//发送完成标志位
bool recOver=0;//接受完成标志位
bool speakOut;//0代表对外讲话，1代表收听

// RGB灯模式定义
enum RGBMode {
  MODE_YELLOW_BLINK,  // 黄色闪烁
  MODE_YELLOW,        // 黄色常亮
  MODE_RED_BLINK,     // 红色闪烁
  MODE_RED,           // 红色常亮
  MODE_GREEN,         // 绿色常亮
  MODE_BLUE,          // 蓝色常亮
  MODE_RED_BLUE_BLINK,// 红蓝交替闪烁
  MODE_OFF,           // 熄灭
  MODE_COUNT          // 模式总数
};

// 函数前向声明
void setRGBColor(bool red, bool green, bool blue);
void setRGBMode(RGBMode mode);
void updateRGBMode();
bool checkDoubleClick();
void cycleRGBMode();

RGBMode currentMode = MODE_GREEN; // 开机默认绿色
unsigned long lastButtonPressTime = 0;
unsigned long lastDoubleClickTime = 0;
bool buttonPressed = false;
int clickCount = 0;
unsigned long lastBlinkTime = 0;
bool blinkState = false;

void setup(void)
{
  Serial.begin(115200);
  Serial.println("start!");
  InitESPNow();//ESP-NOW初始化，用来传输音频
  esp_now_register_send_cb(OnDataSent);//绑定发射和接受回调函数
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("InitESPNow over!");
  I2SInit();//I2S初始化 
  Serial.println("I2SInit over!");
  
  Serial.println("init end!");
  pinMode(BTN, INPUT_PULLUP);//按键
  pinMode(LED, OUTPUT);//指示灯
  digitalWrite(LED,HIGH);
  
  // 初始化RGB灯引脚
  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);
  setRGBMode(MODE_GREEN); // 开机默认绿色
}
bool  BtnisPressed(void)//按键是否按下
{
  bool key=digitalRead(BTN);
  if(1==key)
  {
    return 0;
  }
  else
  {
    return 1 ;
  }
}

// 设置RGB灯颜色（高电平点亮，低电平熄灭）
void setRGBColor(bool red, bool green, bool blue) {
  digitalWrite(RGB_R, red ? HIGH : LOW);
  digitalWrite(RGB_G, green ? HIGH : LOW);
  digitalWrite(RGB_B, blue ? HIGH : LOW);
}

// 设置RGB灯模式
void setRGBMode(RGBMode mode) {
  currentMode = mode;
  switch(mode) {
    case MODE_YELLOW:
      setRGBColor(true, true, false); // 黄色
      break;
    case MODE_RED:
      setRGBColor(true, false, false); // 红色
      break;
    case MODE_GREEN:
      setRGBColor(false, true, false); // 绿色
      break;
    case MODE_BLUE:
      setRGBColor(false, false, true); // 蓝色
      break;
    case MODE_OFF:
      setRGBColor(false, false, false); // 熄灭
      break;
    default:
      // 闪烁模式在updateRGBMode中处理
      break;
  }
}

// 更新RGB灯闪烁效果
void updateRGBMode() {
  unsigned long currentTime = millis();
  
  switch(currentMode) {
    case MODE_YELLOW_BLINK:
      if (currentTime - lastBlinkTime > 500) {
        blinkState = !blinkState;
        setRGBColor(blinkState, blinkState, false);
        lastBlinkTime = currentTime;
      }
      break;
      
    case MODE_RED_BLINK:
      if (currentTime - lastBlinkTime > 500) {
        blinkState = !blinkState;
        setRGBColor(blinkState, false, false);
        lastBlinkTime = currentTime;
      }
      break;
      
    case MODE_RED_BLUE_BLINK:
      if (currentTime - lastBlinkTime > 250) {
        blinkState = !blinkState;
        if (blinkState) {
          setRGBColor(true, false, false); // 红色
        } else {
          setRGBColor(false, false, true); // 蓝色
        }
        lastBlinkTime = currentTime;
      }
      break;
      
    default:
      // 常亮模式不需要更新
      break;
  }
}

// 检测双击事件
bool checkDoubleClick() {
  unsigned long currentTime = millis();
  bool isPressed = BtnisPressed();
  
  if (isPressed && !buttonPressed) {
    // 按键按下
    buttonPressed = true;
    if (currentTime - lastButtonPressTime < 500) {
      // 双击检测
      clickCount++;
      if (clickCount >= 2) {
        clickCount = 0;
        lastDoubleClickTime = currentTime;
        return true;
      }
    } else {
      clickCount = 1;
    }
    lastButtonPressTime = currentTime;
  } else if (!isPressed && buttonPressed) {
    // 按键释放
    buttonPressed = false;
  }
  
  // 超时重置点击计数
  if (currentTime - lastButtonPressTime > 1000 && clickCount > 0) {
    clickCount = 0;
  }
  
  return false;
}

// 切换RGB灯模式
void cycleRGBMode() {
  currentMode = static_cast<RGBMode>((currentMode + 1) % MODE_COUNT);
  setRGBMode(currentMode);
  Serial.print("RGB模式切换至: ");
  Serial.println(currentMode);
}
void loop(void)
{
  // 检测双击事件
  if (checkDoubleClick()) {
    cycleRGBMode();
  }
  
  // 更新RGB灯闪烁效果
  updateRGBMode();
  
  if(BtnisPressed())//按下按键发射数据
  {
    speakOut=0;
    digitalWrite(LED,HIGH);//发射时开灯
    int samples_read = I2Sread(samples_16bit,128);//读取数据
    covert_bit(samples_16bit,samples_8bit,samples_read);//发送时转换为8位
    sendData(samples_8bit,samples_read);//发射数据
  }
  else//接收数据，接受部分在回调函数中
  {
    delay(28);//经过一段延时再判断，接收数据并且播放也需要时间
    speakOut=1;
    if(recOver)
    {
      recOver=0;
      if(digitalRead(LED))//接受数据时闪烁LED
      {
          digitalWrite(LED,LOW);
      }
      else
      {
          digitalWrite(LED,HIGH);
      }
    }
    else
    {
      digitalWrite(LED,LOW);//没有接收到消息，也没有发射，关灯
      i2s_zero_dma_buffer(SPK_I2S_PORT);//清空DMA中缓存的数据，你可以尝试一下不清空（注释这行）是什么效果
    }
  }
}
