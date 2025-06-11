/*
                        Milano Smart Park Firmware
                       Developed by Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
*/

// Display Management Functions

#ifndef DISPLAY_H
#define DISPLAY_H

#include "icons.h"

static String dspFloatToComma(float value) {  // converts float values in strings with the decimal part separated from the integer part by a comma

  String convert = String(value, 3);
  convert.replace(".", ",");
  return convert;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawBoot(String *fwver) { // draws the boot screen on the U8G2 display

  u8g2.firstPage();
  u8g2.clearBuffer();
  u8g2.drawXBM(0, 0, 64, 64, msp_icon64x64);
  u8g2.setFont(u8g2_font_6x13B_tf);
  u8g2.drawStr(74, 12, "Milano"); u8g2.drawStr(74, 25, "Smart"); u8g2.drawStr(74, 38, "Park");
  u8g2.setFont(u8g2_font_6x13_mf);
  u8g2.setCursor(37, 62); u8g2.print("by NM");
  u8g2.setCursor(74, 62); u8g2.print(*fwver);
  u8g2.sendBuffer();
  delay(5000);

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawScrHead() { // draws the screen header on the U8G2 display

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);

  // system state
  u8g2.setCursor(0, 13); u8g2.print("#" + deviceid + "#");

  if (SD_ok) {
    u8g2.drawXBMP(72, 0, 16, 16, sd_icon16x16);
  }
  if (datetime_ok) {
    u8g2.drawXBMP(92, 0, 16, 16, clock_icon16x16);
  }
  if (connected_ok) {
    if (use_modem) {
      u8g2.drawXBMP(112, 0, 16, 16, mobile_icon16x16);
    } else {
      u8g2.drawXBMP(112, 0, 16, 16, wifi1_icon16x16);
    }
  } else {
    u8g2.drawXBMP(112, 0, 16, 16, nocon_icon16x16);
  }

  u8g2.drawLine(0, 17, 127, 17);

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

short getLineHOffset(const char string[]) { // get the proper horizontal offset of a string for the U8G2 display
  
  short offset = 0;
  u8g2_uint_t x = (u8g2.getDisplayWidth() - u8g2.getStrWidth(string)) / 2;
  if (x > 0) {
    offset = short(x);
  }
  return offset;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawLine(const char message[], short secdelay) { // draws a text line on the U8G2 display
  
  short offset = getLineHOffset(message);
  drawScrHead();
  u8g2.setCursor(offset, 45); u8g2.print(message);
  u8g2.sendBuffer();
  delay(secdelay * 1000);

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawTwoLines(const char message1[], const char message2[], short secdelay) { // draws two text lines on the U8G2 display
  
  short offset1 = getLineHOffset(message1);
  short offset2 = getLineHOffset(message2);
  drawScrHead();
  u8g2.setCursor(offset1, 35); u8g2.print(message1);
  u8g2.setCursor(offset2, 55); u8g2.print(message2);
  u8g2.sendBuffer();
  delay(secdelay * 1000);
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawCountdown(short startsec, const char message[]) { // draws a countdown on the U8G2 display

  for (short i = startsec; i >= 0; i--) {
    char output[17] = {0};
    sprintf(output, "WAIT %02d:%02d MIN.", i / 60, i % 60);
    drawTwoLines(message, output, 1);
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawMeasurements(const float _temp, const float _hum, const float _pre, const float _VOC,
                      const int32_t _PM1, const int32_t _PM25, const int32_t _PM10, const float _MICS_CO, const float _MICS_NO2,
                      const float _MICS_NH3, const float _ozone
) { // draws measurements on the U8g2 display

  log_i("Printing measurements on display...");

  // page 1
  drawScrHead();
  u8g2.setCursor(5, 28); u8g2.print("Temp:  " + ((BME_run) ? (dspFloatToComma(_temp) + "*C") : ""));
  u8g2.setCursor(5, 39); u8g2.print("Hum:  " + ((BME_run) ? (dspFloatToComma(_hum) + "%" ) : ""));
  u8g2.setCursor(5, 50); u8g2.print("Pre:  " + ((BME_run) ? (dspFloatToComma(_pre) + "hPa") : ""));
  u8g2.setCursor(5, 61); u8g2.print("VOC:  " + ((BME_run) ? (dspFloatToComma(_VOC) + "kOhm") : ""));
  u8g2.sendBuffer();
  delay(7000);

  // page 2
  drawScrHead();
  u8g2.setCursor(5, 28); u8g2.print("PM1:  " + ((PMS_run) ? (String(_PM1) + "ug/m3") : ""));
  u8g2.setCursor(5, 39); u8g2.print("PM2,5:  " + ((PMS_run) ? (String(_PM25) + "ug/m3") : ""));
  u8g2.setCursor(5, 50); u8g2.print("PM10:  " + ((PMS_run) ? (String(_PM10) + "ug/m3") : ""));
  u8g2.sendBuffer();
  delay(5000);

  // page 3
  drawScrHead();
  u8g2.setCursor(5, 28); u8g2.print("CO:  " + ((MICS_run) ? (dspFloatToComma(_MICS_CO) + "ug/m3") : ""));
  u8g2.setCursor(5, 39); u8g2.print("NOx:  " + ((MICS_run) ? (dspFloatToComma(_MICS_NO2) + "ug/m3") : ""));
  u8g2.setCursor(5, 50); u8g2.print("NH3:  " + ((MICS_run) ? (dspFloatToComma(_MICS_NH3) + "ug/m3") : ""));
  u8g2.sendBuffer();
  delay(5000);

  // page 4
  drawScrHead();
  u8g2.setCursor(5, 39); u8g2.print("O3:  " + ((O3_run) ? (dspFloatToComma(_ozone) + "ug/m3") : ""));
  u8g2.sendBuffer();
  delay(3000);
  
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawMicsValues(uint16_t redval, uint16_t oxval, uint16_t nh3val) { // draw input values to screen

  log_d("MICS6814 stored base resistance values:");
  log_d("RED: %d | OX: %d | NH3: %d\n", redval, oxval, nh3val);
  drawScrHead();
  u8g2.setCursor(2, 28); u8g2.print("MICS6814 Res0 values:");
  u8g2.setCursor(30, 39); u8g2.print("RED: " + String(redval));
  u8g2.setCursor(30, 50); u8g2.print("OX: " + String(oxval));
  u8g2.setCursor(30, 61); u8g2.print("NH3: " + String(nh3val));
  u8g2.sendBuffer();
  delay(5000);

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#endif