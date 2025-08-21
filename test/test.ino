void setup() {
    pinMode(22, OUTPUT);
    Serial.begin(115200); // 初始
    Serial.print("LED_BUILTIN 引脚号：");
    Serial.println(LED_BUILTIN); // 打印一次即可
  }
  
  void loop() {
    digitalWrite(22, HIGH); // 持续设置引脚为高电平（控制LED
  }