/*
                        Milano Smart Park Firmware
                   Copyright (c) 2021 Norman Mulinacci

      This firmware is usable under the terms and conditions of the
           GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

  Parts of this firmware are based on open source works freely distributed by Luca Crotti @2019
*/

// Netwok Management Functions

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool syncNTPTime(String *date, String *timeT) { // stores date&time in a convenient format

  *date = "";
  *timeT = "";
  //configTime(gmtOffset_sec, daylightOffset_sec, ntpServer), Italy is GMT+1, DST is +1hour
  configTime(3600, 3600, "pool.ntp.org");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    log_e("Failed to obtain date&time!\n");
    drawScrHead();
    u8g2.drawStr(15, 45, "Date & time err!");
    u8g2.sendBuffer();
    delay(2000);
    return false;
  }
  char Date[11], Time[9];
  strftime(Date, 11, "%d/%m/%Y", &timeinfo);
  strftime(Time, 9, "%T", &timeinfo);
  *date = String(Date);
  *timeT = String(Time);
  log_i("Current date&time: %s %s\n", dayStamp.c_str(), timeStamp.c_str());
  drawScrHead();
  u8g2.drawStr(15, 45, "Date & time ok!");
  u8g2.sendBuffer();
  delay(2000);
  return true;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool connectWiFi() { // sets WiFi mode and tx power (var wifipow), performs connection to WiFi network (vars ssid, passw)

  WiFi.mode(WIFI_STA); // Set WiFi to station mode
  delay(1000); // Waiting a bit for Wifi mode set

  WiFi.setTxPower(wifipow); // Set WiFi transmission power
  log_i("WIFIPOW set to %d", wifipow);
  log_i("Legend: -4(-1dBm), 8(2dBm), 20(5dBm), 28(7dBm), 34(8.5dBm), 44(11dBm), 52(13dBm), 60(15dBm), 68(17dBm), 74(18.5dBm), 76(19dBm), 78(19.5dBm)\n");

  for (short retry = 0; retry < 4; retry++) { // Scan WiFi for selected network and connect
    log_i("Scanning WiFi networks...");
    drawScrHead();
    u8g2.drawStr(5, 35, "Scanning networks...");
    u8g2.sendBuffer();
    short networks = WiFi.scanNetworks(); // WiFi.scanNetworks will return the number of networks found
    log_i("Scanning complete\n");
    u8g2.drawStr(10, 55, "Scanning complete");
    u8g2.sendBuffer();
    delay(1000);

    if (networks > 0) { // Looking through found networks
      log_i("%d networks found\n", networks);
      drawScrHead();
      u8g2.setCursor(5, 35); u8g2.print("Networks found: " + String(networks));
      u8g2.sendBuffer();
      delay(100);
      bool ssid_ok = false; // For selected network if found
      for (short i = 0; i < networks; i++) { // Prints SSID and RSSI for each network found, checks against ssid
        if (WiFi.SSID(i) == ssid) ssid_ok = true;
        log_v("%d: %s(%d) %s%c", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "ENCRYPTED", i == networks - 1 ? '\n' : ' ');
        delay(100);
      }

      if (ssid_ok) { // Trying to connect
        log_i("%s found!\n", ssid.c_str());
        u8g2.setCursor(2, 55); u8g2.print(ssid + " ok!");
        u8g2.sendBuffer();
        delay(4000);
        log_i("Connecting to %s", ssid.c_str());
        drawScrHead();
        u8g2.drawStr(5, 30, "Connecting to: ");
        u8g2.drawStr(6, 42, ssid.c_str());
        u8g2.sendBuffer();

        WiFi.begin(ssid.c_str(), passw.c_str()); // Attempting connection
        short conntries = 0;
		String dots = "";
        while (WiFi.status() != WL_CONNECTED) {
          if (conntries > 5) {
            log_e("Can't connect to network!\n");
            drawScrHead();
            u8g2.drawStr(15, 45, "WiFi connect err!");
            u8g2.sendBuffer();
            WiFi.disconnect();
            delay(3000);
            break;
          }
          dots += '.';
          u8g2.drawStr(7 + (2 * (conntries + 1)), 55, ". ");
          u8g2.sendBuffer();
          conntries++;
          delay(1000);
        }
		log_i("%s\n", dots.c_str());

        if (WiFi.status() == WL_CONNECTED) { // Connection successful
          log_i("Connection with %s made successfully!\n", ssid.c_str());
          drawScrHead();
          u8g2.drawStr(15, 45, "WiFi connected!");
          u8g2.sendBuffer();
          delay(2000);

          return true; // All done correctly
        }
      } else {
        log_e("%s not found!\n", ssid.c_str());
        u8g2.setCursor(2, 55); u8g2.print("No " + ssid + "!");
        u8g2.sendBuffer();
        delay(4000);
      }
    } else {
      log_e("No networks found!\n");
      drawScrHead();
      u8g2.drawStr(15, 45, "No networks found!");
      u8g2.sendBuffer();
      delay(2000);
    }

    if (retry < 3) { // Print remaining tries
      log_i("Retrying, %d retries left\n", 3 - retry);
      String remain = String(3 - retry) + " tries remain.";
      drawTwoLines(30, "Retrying...", 20, remain.c_str(), 3);
    } else if (retry == 3) {
      log_e("No internet connection!\n");
      drawScrHead();
      u8g2.drawStr(25, 45, "No internet!");
      u8g2.sendBuffer();
      delay(3000);
    }
  }

  return false; // No internet connection

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++