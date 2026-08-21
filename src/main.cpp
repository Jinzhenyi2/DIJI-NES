#include <Arduino.h>
#include "lgfx_conf.h"   // 使用你已有的屏幕配置文件

LGFX tft;

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("Initializing screen...");
  tft.init();
  tft.setRotation(1);    // 横屏 160x128（与项目一致）
  tft.fillScreen(TFT_BLACK);
  delay(500);

  // 显示纯色循环
  Serial.println("Displaying colors...");
}

void loop() {
  // 红色
  tft.fillScreen(TFT_RED);
  delay(1500);
  // 绿色
  tft.fillScreen(TFT_GREEN);
  delay(1500);
  // 蓝色
  tft.fillScreen(TFT_BLUE);
  delay(1500);
  // 白色
  tft.fillScreen(TFT_WHITE);
  delay(1500);
  // 黑色
  tft.fillScreen(TFT_BLACK);
  delay(1500);
}