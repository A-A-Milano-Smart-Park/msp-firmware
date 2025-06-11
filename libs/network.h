/*
                        Milano Smart Park Firmware
                       Developed by Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
*/

// Network Management Functions

#ifndef NETWORK_H
#define NETWORK_H

#include "display.h"

typedef struct __SEND_DATA__ {
  struct tm sendTimeInfo;  /*!< Date and time of the data to be sent */
  float temp;
  float hum;
  float pre;
  float VOC;
  int32_t PM1;
  int32_t PM25;
  int32_t PM10;
  float MICS_CO;
  float MICS_NO2;
  float MICS_NH3;
  float ozone;
  int8_t MSP; /*!< MSP# Index */
} send_data_t;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void printWiFiMACAddr() {

  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  char baseMacChr[18] = { 0 };
  sprintf(baseMacChr, "%02X:%02X:%02X:%02X:%02X:%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  Serial.println("WIFI MAC ADDRESS: " + String(baseMacChr) + "\n");
  drawTwoLines("WIFI MAC ADDRESS:", baseMacChr, 6);
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool connectWiFi() {  // sets WiFi mode and tx power (var wifipow), performs connection to WiFi network (vars ssid, passw)

  log_i("Setting WiFi STATION mode...");
  WiFi.mode(WIFI_STA);       // Set WiFi to station mode
  delay(1000); // Wait for mode to set
  WiFi.setTxPower(wifipow);  // Set WiFi transmission power
  log_i("WIFIPOW set to %d", wifipow);
  log_i("Legend: -4(-1dBm), 8(2dBm), 20(5dBm), 28(7dBm), 34(8.5dBm), 44(11dBm), 52(13dBm), 60(15dBm), 68(17dBm), 74(18.5dBm), 76(19dBm), 78(19.5dBm)\n");

  for (short retry = 0; retry < 4; retry++) {  // Scan WiFi for selected network and connect
    log_i("Scanning WiFi networks...");
    short networks = WiFi.scanNetworks();  // WiFi.scanNetworks will return the number of networks found
    log_i("Scanning complete\n");

    if (networks > 0) {  // Looking through found networks
      log_i("%d networks found\n", networks);
      bool ssid_ok = false;                   // For selected network if found
      for (short i = 0; i < networks; i++) {  // Prints SSID and RSSI for each network found, checks against ssid
        log_v("%d: %s(%d) %s%c", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "ENCRYPTED", i == networks - 1 ? '\n' : ' ');
        if (WiFi.SSID(i) == ssid) {
          ssid_ok = true;
          break;
        }
      }

      if (ssid_ok) {  // Begin connection
        log_i("%s found!\n", ssid.c_str());
        String foundNet = ssid + " OK!";
        log_i("Connecting to %s, please wait...", ssid.c_str());

        WiFi.begin(ssid.c_str(), passw.c_str());
        auto start = millis();  // starting time
        while (WiFi.status() != WL_CONNECTED) {
          auto timeout = millis() - start;
          if (timeout > 10000) {
            WiFi.disconnect();
            log_e("Can't connect to network!\n");
            drawLine("WiFi connect err!", 2);
            break;
          }
        }

        if (WiFi.status() == WL_CONNECTED) {  // Connection successful
          log_i("Connection to WiFi made successfully!");
          return true;
        }
      } else {
        log_e("%s not found!\n", ssid.c_str());
        String noNet = "NO " + ssid + "!";
        drawLine(noNet.c_str(), 2);
      }
    } else {
      log_e("No networks found!\n");
      drawLine("No networks found!", 2);
    }

    if (retry < 3) {  // Print remaining tries
      log_i("Retrying, %d retries left\n", 3 - retry);
      String remain = String(3 - retry) + " tries remain.";
      drawTwoLines("Retrying...", remain.c_str(), 2);
    } else if (retry == 3) {
      log_e("No internet connection!\n");
      drawLine("No internet!", 2);
    }
  }

  return false;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void disconnectWiFi() {

  log_i("Turning off WiFi...\n");
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  connected_ok = false;
  delay(1000);  // Waiting a bit for Wifi mode set
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void initializeModem() {

  log_i("Initializing SIM800L modem serial connection...");

  // Set GSM module baud rate
  gsmSerial.begin(9600, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(6000);

  // Restart takes quite some time
  log_i("Issuing modem reset (takes some time)...");
  modem.restart();

  log_i("Done!");

  log_d("Modem Name: %s", modem.getModemName().c_str());

  log_d("Modem Info: %s", modem.getModemInfo().c_str());

  log_d("IMEI: %s", modem.getIMEI().c_str());

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool connectModem() {

  initializeModem();

  String CCID = modem.getSimCCID();
  String IMSI = modem.getIMSI();

  log_d("CCID: %s", CCID.c_str());

  log_d("IMSI: %s", IMSI.c_str());

  if (CCID.startsWith("ERROR", 0) || IMSI.startsWith("ERROR", 0)) {
    log_e("No SIM found or SIM not inserted!");
    drawTwoLines("ERROR:", "NO SIM!", 3);
    return false;    
  }

  short retries = 0;
  while (retries < 2) {
    log_i("Waiting for network...");
    if (modem.waitForNetwork()) break;
    log_e("Couldn't find network!");
    if (retries < 1) {
      log_i("Retrying in 10 seconds...");
      delay(10000);
    }
    retries++;
  }

  if (!modem.isNetworkConnected()) {
    log_e("Network is not connected!");
    drawTwoLines("ERROR:", "NO NETWORK!", 3);
    return false;
  }

  log_i("Mobile network found!");

  retries = 0;
  while (retries < 4) {
    log_i("Connecting to GPRS...");
    if (modem.gprsConnect(apn.c_str(), "", "")) break;
    log_e("Connection failed!");
    if (retries < 3) {
      log_i("Retrying in 5 seconds...");
      delay(5000);
    }
    retries++;
  }

  if (!modem.isGprsConnected()) {
    log_e("GPRS is not connected!");
    drawTwoLines("ERROR:", "NO GPRS!", 3);
    return false;
  }

  log_i("Connection to GPRS made successfully!");

  log_d("Operator: %s", modem.getOperator().c_str());

  log_d("Signal quality: %d", modem.getSignalQuality());

  return true;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#define TIME_SYNC_MAX_RETRY 5

void connAndGetTime() {  // connects to the internet and retrieves time from NTP server

  datetime_ok = false;  // resetting the date&time var
  if (!use_modem) {
    configTime(0, 0, ntp_server.c_str());  // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2)
    Serial.println("Connecting to WiFi...\n");
    drawTwoLines("Connecting to", "WiFi...", 1);
    connected_ok = connectWiFi();
  } else if (!modem.isNetworkConnected() || !modem.isGprsConnected()) {  // reconnect only if network is lost
    Serial.println("Connecting to GPRS...\n");
    drawTwoLines("Connecting to", "GPRS...", 1);
    connected_ok = connectModem();
  }
  if (connected_ok) {
    Serial.println("Retrieving date&time...");
    drawTwoLines("Getting date&time...", "Please wait...", 1);
    String tzRule = timezone;  // timezone rule
    if (tzRule.length() == 0) {
      log_e("TIMEZONE value is empty! Use default value: CET-1CEST");
      tzRule = "CET-1CEST";  // Default timezone rule
    } else {
      log_i("TIMEZONE = *%s*", tzRule.c_str());
    }
    setenv("TZ", tzRule.c_str(), 1);  // Setting timezone
    tzset();  // Apply timezone settings
    int8_t timeSyncRetries = TIME_SYNC_MAX_RETRY;  // Number of retries for time synchronization

    auto start = millis();
    while ((!datetime_ok) || (timeSyncRetries >= 0)) {  // Retrieving date&time
      timeSyncRetries--;
      auto timeout = millis() - start;
      if (!use_modem) {
        datetime_ok = getLocalTime(&timeinfo);
      } else {
        // TODO: review the modem NTP sync
        int hh = 0, mm = 0, ss = 0, yyyy = 0, mon = 0, day = 0;
        float tz = 0;
        modem.NTPServerSync(ntp_server, 0);
        datetime_ok = modem.getNetworkTime(&yyyy, &mon, &day, &hh, &mm, &ss, &tz);
        timeinfo.tm_hour = hh;
        timeinfo.tm_min = mm;
        timeinfo.tm_sec = ss;
        timeinfo.tm_year = yyyy - 1900;
        timeinfo.tm_mon = mon - 1;
        timeinfo.tm_mday = day;
        timeinfo.tm_isdst = -1;
      }
      if (timeout > 90000) break;
    }

    if (datetime_ok) {
      drawTwoLines("Getting date&time...", "OK!", 1);
      strftime(Date, sizeof(Date), "%d/%m/%Y", &timeinfo);  // Formatting date as DD/MM/YYYY
      strftime(Time, sizeof(Time), "%T", &timeinfo);        // Formatting time as HH:MM:SS
      String tempT = String(Date) + " " + String(Time);
      Serial.println("Current date&time: " + tempT);
      drawTwoLines("Date & Time:", tempT.c_str(), 0);
    } else {
      log_e("Failed to obtain date&time! Are you connected to the internet?\n");
      drawTwoLines("Date & time err!", "Is internet ok?", 0);
    }
    Serial.println();
    delay(3000);
  }
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void connectToServer(SSLClient *client, send_data_t *dataToBeSent) {

  time_t epochTime = mktime(&dataToBeSent->sendTimeInfo);  // converting UTC date&time in UNIX Epoch Time format

  client->setVerificationTime((epochTime / 86400UL) + 719528UL, epochTime % 86400UL);  // setting SLLClient's verification time to current time while converting UNIX Epoch Time to BearSSL's expected format

  auto start = millis();  // time for connection
  Serial.println("Uploading data to server through HTTPS in progress...\n");
  // drawTwoLines("Uploading data", "to server...", 0);

  log_i("Connecting to %s...\n", server.c_str());
  log_i("Device ID: %s\n", deviceid.c_str());
  log_i("api_secret_salt: %s\n", api_secret_salt.c_str());

  short retries = 0;
  while (retries < 4) {
    if (client->connect(server.c_str(), 443)) {
      log_i("Connection to server made! Time: %d\n", millis() - start);
      // Building the post string:
      String postStr = "X-MSP-ID=" + deviceid;
      if (BME_run) {
        postStr += "&temp=";
        postStr += String(dataToBeSent->temp, 3);
        postStr += "&hum=";
        postStr += String(dataToBeSent->hum, 3);
        postStr += "&pre=";
        postStr += String(dataToBeSent->pre, 3);
        postStr += "&voc=";
        postStr += String(dataToBeSent->VOC, 3);
      }
      if (MICS_run) {
        postStr += "&cox=";
        postStr += String(dataToBeSent->MICS_CO, 3);
        postStr += "&nox=";
        postStr += String(dataToBeSent->MICS_NO2, 3);
        postStr += "&nh3=";
        postStr += String(dataToBeSent->MICS_NH3, 3);
      }
      if (PMS_run) {
        postStr += "&pm1=";
        postStr += String(dataToBeSent->PM1);
        postStr += "&pm25=";
        postStr += String(dataToBeSent->PM25);
        postStr += "&pm10=";
        postStr += String(dataToBeSent->PM10);
      }
      if (O3_run) {
        postStr += "&o3=";
        postStr += String(dataToBeSent->ozone, 3);
      }
      postStr += "&msp=";
      postStr += String(dataToBeSent->MSP);
      postStr += "&recordedAt=";
      postStr += String(epochTime);

      // Sending client requests
      String postLine = "POST /api/v1/records HTTP/1.1\nHost: " + server + "\nAuthorization: Bearer " + api_secret_salt + ":" + deviceid + "\n";
      postLine += "Connection: close\nUser-Agent: MilanoSmartPark\nContent-Type: application/x-www-form-urlencoded\nContent-Length: " + String(postStr.length());
      log_d("Post line:\n%s\n", postLine.c_str());
      log_d("Post string: %s\n", postStr.c_str());
      client->print(postLine + "\n\n" + postStr);
      client->flush();

      // Get answer from server
      start = millis();
      String answLine = "";
      while (client->available()) {
        char c = client->read();
        answLine += c;
        auto timeout = millis() - start;
        if (timeout > 10000) break;
      }
      client->stop();  // Stopping the client

      // Check server answer
      if (answLine.startsWith("HTTP/1.1 201 Created", 0)) {
        log_i("Server answer ok! Data uploaded successfully!\n");
        // drawTwoLines("Data uploaded", "successfully!", 2);
        sent_ok = true;
      } else {
        log_e("Server answered with an error! Data not uploaded!\n");
        log_e("The full answer is:\n%s\n", answLine.c_str());
        // drawTwoLines("Serv answ error!", "Data not sent!", 10);
      }
      break;  // exit

    } else {
      log_e("Error while connecting to server!");
      String mesg = "";
      if (retries == 3) {
        log_e("Data not uploaded!\n");
        mesg = "Data not sent!";
      } else {
        log_i("Trying again, %d retries left...\n", 3 - retries);
        mesg = String(3 - retries) + " retries left...";
      }
      // drawTwoLines("Serv conn error!", mesg.c_str(), 10);
      retries++;
    }
  }
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#endif