/*
                        Milano Smart Park Firmware
                   Copyright (c) 2021 Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

             Parts of this code are based on open source works
                 freely distributed by Luca Crotti @2019
*/

// Display Management Functions

#include "icons.h"

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawBoot(String *fwver, short veryear) { // draws the boot screen on the U8G2 display, on two pages

  u8g2.firstPage(); // page 1
  u8g2.clearBuffer();
  u8g2.drawXBM(0, 0, 64, 64, msp_icon64x64);
  u8g2.setFont(u8g2_font_6x13_tf);
  u8g2.drawStr(74, 10, "Milano"); u8g2.drawStr(74, 23, "Smart"); u8g2.drawStr(74, 36, "Park");
  u8g2.setCursor(74, 62); u8g2.print("v" + *fwver);
  u8g2.sendBuffer();
  delay(3000);
  u8g2.clearBuffer(); // page 2
  u8g2.drawXBM(0, 0, 64, 64, msp_icon64x64);
  u8g2.setFont(u8g2_font_6x13_tf);
  u8g2.drawStr(74, 23, "by"); u8g2.drawStr(74, 36, "Norman M.");
  u8g2.setCursor(74, 49); u8g2.print(veryear);
  u8g2.sendBuffer();
  delay(3000);

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
    u8g2.drawXBMP(112, 0, 16, 16, wifi1_icon16x16);
  } else {
    u8g2.drawXBMP(112, 0, 16, 16, nocon_icon16x16);
  }

  u8g2.drawLine(0, 17, 127, 17);

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawTwoLines(short offset1, const char message1[], short offset2, const char message2[], short secdelay) { // draws two text lines on the U8G2 display

  drawScrHead();
  u8g2.setCursor(offset1, 35); u8g2.print(message1);
  u8g2.setCursor(offset2, 55); u8g2.print(message2);
  u8g2.sendBuffer();
  delay(secdelay * 1000);

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawCountdown(int startsec, short offset, const char message[]) { // draws a countdown on the U8G2 display

  for (int i = startsec; i > 0; i--) {
    String output = "";
    output = String(i / 60) + ":";
    if (i % 60 >= 0 && i % 60 <= 9) {
      output += "0";
    }
    output += String(i % 60);
    output = "WAIT " + output + " MIN.";
    drawTwoLines(offset, message, 23, output.c_str(), 1);
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void drawMeasurements() { // draws measurements on the U8g2 display, on four pages

  log_v("Printing measurements on display...");

  // page 1
  drawScrHead();
  u8g2.setCursor(5, 28); u8g2.print("Temp:  " + floatToComma(temp) + "*C");
  u8g2.setCursor(5, 39); u8g2.print("Hum:  " + floatToComma(hum) + "%");
  u8g2.setCursor(5, 50); u8g2.print("Pre:  " + floatToComma(pre) + "hPa");
  u8g2.setCursor(5, 61); u8g2.print("PM10:  " + String(PM10) + "ug/m3");
  u8g2.sendBuffer();
  delay(5000);

  // page 2
  drawScrHead();
  u8g2.setCursor(5, 28); u8g2.print("PM2,5:  " + String(PM25) + "ug/m3");
  u8g2.setCursor(5, 39); u8g2.print("PM1:  " + String(PM1) + "ug/m3");
  u8g2.setCursor(5, 50); u8g2.print("NOx:  " + floatToComma(MICS_NO2) + "ug/m3");
  u8g2.setCursor(5, 61); u8g2.print("CO:  " + floatToComma(MICS_CO) + "ug/m3");
  u8g2.sendBuffer();
  delay(5000);

  // page 3
  drawScrHead();
  u8g2.setCursor(5, 28); u8g2.print("O3:  " + floatToComma(ozone) + "ug/m3");
  u8g2.setCursor(5, 39); u8g2.print("VOC:  " + floatToComma(VOC) + "kOhm");
  u8g2.setCursor(5, 50); u8g2.print("NH3:  " + floatToComma(MICS_NH3) + "ug/m3");
  u8g2.setCursor(5, 61); u8g2.print("C3H8:  " + floatToComma(MICS_C3H8) + "ug/m3");
  u8g2.sendBuffer();
  delay(5000);

  // page 4
  drawScrHead();
  u8g2.setCursor(5, 28); u8g2.print("C4H10:  " + floatToComma(MICS_C4H10) + "ug/m3");
  u8g2.setCursor(5, 39); u8g2.print("CH4:  " + floatToComma(MICS_CH4) + "ug/m3");
  u8g2.setCursor(5, 50); u8g2.print("H2:  " + floatToComma(MICS_H2) + "ug/m3");
  u8g2.setCursor(5, 61); u8g2.print("C2H5OH:  " + floatToComma(MICS_C2H5OH) + "ug/m3");
  u8g2.sendBuffer();
  delay(5000);

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++