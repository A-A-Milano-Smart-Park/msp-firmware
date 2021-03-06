/*
                        Milano Smart Park Firmware
                   Copyright (c) 2021 Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

             Parts of this code are based on open source works
                 freely distributed by Luca Crotti @2019
*/

// Netwok Management Functions

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool syncNTPTime(struct tm *timeptr) { // syncs UTC time

  configTime(0, 0, "pool.ntp.org"); // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer)
  auto start = millis();
  while (!getLocalTime(timeptr)) {
    auto timeout = millis() - start;
    if (timeout > 20000) {
      return false;
    }
  }
  return true;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool connectWiFi() { // sets WiFi mode and tx power (var wifipow), performs connection to WiFi network (vars ssid, passw)

  WiFi.mode(WIFI_STA); // Set WiFi to station mode
  delay(1500); // Waiting a bit for Wifi mode to set

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

      if (ssid_ok) { // Begin connection
        log_i("%s found!\n", ssid.c_str());
        u8g2.setCursor(2, 55); u8g2.print(ssid + " ok!");
        u8g2.sendBuffer();
        delay(4000);
        log_i("Connecting to %s, please wait...", ssid.c_str());
        drawScrHead();
        u8g2.drawStr(5, 30, "Connecting to: ");
        u8g2.drawStr(6, 42, ssid.c_str());
        u8g2.drawStr(15, 60, "Please wait...");
        u8g2.sendBuffer();

        WiFi.begin(ssid.c_str(), passw.c_str());
        auto start = millis(); // starting time
        while (WiFi.status() != WL_CONNECTED) {
          auto timeout = millis() - start;
          if (timeout > 10000) {
            log_e("Can't connect to network!\n");
            drawScrHead();
            u8g2.drawStr(15, 45, "WiFi connect err!");
            u8g2.sendBuffer();
            WiFi.disconnect();
            delay(3000);
            break;
          }
        }
        
        if (WiFi.status() == WL_CONNECTED) { // Connection successful

          return true;
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

  return false;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void connAndGetTime() { // lame function to set global vars

  Serial.println("Connecting to WiFi...\n");
  connected_ok = connectWiFi();
  if (connected_ok) {
    Serial.println("Connection with " + ssid + " made successfully!");
    drawScrHead();
    u8g2.drawStr(15, 45, "WiFi connected!");
    u8g2.sendBuffer();
    delay(2000);
    Serial.println("Waiting a bit before retrieving date&time...");
    drawCountdown(10, 2, "Wait before conn...");
    Serial.println("Retrieving date&time from NTP...");
    datetime_ok = syncNTPTime(&timeinfo); // Connecting with NTP server and retrieving date&time
    drawScrHead();
    if (datetime_ok) {
      Serial.println("Done!");
      strftime(Date, sizeof(Date), "%d/%m/%Y", &timeinfo); // Formatting date as DD/MM/YYYY
      strftime(Time, sizeof(Time), "%T", &timeinfo); // Formatting time as HH:MM:SS
      String tempT = String(Date) + " " + String(Time);
      log_d("Current date&time: %s", tempT.c_str());
      drawTwoLines(27, "Date & Time:", 8, tempT.c_str(), 0);
    } else {
      log_e("Failed to obtain date&time!");
      u8g2.drawStr(15, 45, "Date & Time err!");
    }
    Serial.println();
    u8g2.sendBuffer();
    delay(3000);
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
