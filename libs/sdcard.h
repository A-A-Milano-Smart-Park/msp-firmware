/*
                        Milano Smart Park Firmware
                       Developed by Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

             Parts of this code are based on open source works
                 freely distributed by Luca Crotti @2019
*/

// SD Card and File Management Functions

#ifndef SDCARD_H
#define SDCARD_H

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool initializeSD() { // checks for SD Card presence and type

  short timeout = 0;
  while (!SD.begin()) {
    if (timeout > 4) { // errors after 10 seconds
      log_e("No SD Card detected! No internet connection possible!\n");
      drawTwoLines("No SD Card!", "No web!", 3);
      return false;
    }
    delay(1000); // giving it some time to detect the card properly
    timeout++;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_MMC) {
    log_v("SD Card type: MMC");
  } else if (cardType == CARD_SD) {
    log_v("SD Card type: SD");
  } else if (cardType == CARD_SDHC) {
    log_v("SD Card type: SDHC");
  } else {
    log_e("Unidentified Card type, format the SD Card!  No internet connection possible!\n");
    drawTwoLines("SD Card format!", "No web!", 3);
    return false;
  }
  delay(300);
  log_v("SD Card size: %lluMB\n", SD.cardSize() / (1024 * 1024));
  return true;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool parseConfig(File fl) { // parses the configuration file on the SD Card

#define LINES 12

  bool outcome = true;
  String command[LINES];
  String temp;
  int i = 0;
  unsigned long lastpos = 0;
  while (fl.available() && i < LINES) {   // Storing the config file in a string array
    fl.seek(lastpos);
    if (i == 0) temp = fl.readStringUntil('#');
    command[i] = fl.readStringUntil(';');
    temp = fl.readStringUntil('#');
    log_d("Line number %d: %s", i + 1, command[i].c_str());
    lastpos = fl.position();
    i++;
  }
  fl.close();
  // Importing variables from the string array.
  //ssid
  if (command[0].startsWith("ssid", 0)) {
    ssid = command[0].substring(command[0].indexOf('=') + 1, command[0].length());
    if (ssid.length() == 0) {
      log_e("SSID value is empty!");
      outcome = false;
    } else {
      log_i("ssid = *%s*", ssid.c_str());
    }
  } else {
    log_e("Error parsing SSID line!");
    outcome = false;
  }
  //passw
  if (command[1].startsWith("password", 0)) {
    passw = command[1].substring(command[1].indexOf('=') + 1, command[1].length());
    log_i("passw = *%s*", passw.c_str());
  } else {
    log_e("Error parsing PASSW line!");
    outcome = false;
  }
  //deviceid
  if (command[2].startsWith("device_id", 0)) {
    deviceid = command[2].substring(command[2].indexOf('=') + 1, command[2].length());
    if (deviceid.length() == 0) {
      log_e("DEVICEID value is empty!");
      outcome = false;
    } else {
      log_i("deviceid = *%s*", deviceid.c_str());
    }
  } else {
    log_e("Error parsing DEVICEID line!");
    outcome = false;
  }
  //wifipow
  if (command[3].startsWith("wifi_power", 0)) {
    temp = "";
    temp = command[3].substring(command[3].indexOf('=') + 1, command[3].length());
    if (temp.indexOf("19.5dBm") == 0) {
      wifipow = WIFI_POWER_19_5dBm;
      log_i("wifipow = *WIFI_POWER_19_5dBm*");
    } else if (temp.indexOf("19dBm") == 0) {
      wifipow = WIFI_POWER_19dBm;
      log_i("wifipow = *WIFI_POWER_19dBm*");
    } else if (temp.indexOf("18.5dBm") == 0) {
      wifipow = WIFI_POWER_18_5dBm;
      log_i("wifipow = *WIFI_POWER_18_5dBm*");
    } else if (temp.indexOf("17dBm") == 0) {
      wifipow = WIFI_POWER_17dBm;
      log_i("wifipow = *WIFI_POWER_17dBm*");
    } else if (temp.indexOf("15dBm") == 0) {
      wifipow = WIFI_POWER_15dBm;
      log_i("wifipow = *WIFI_POWER_15dBm*");
    } else if (temp.indexOf("13dBm") == 0) {
      wifipow = WIFI_POWER_13dBm;
      log_i("wifipow = *WIFI_POWER_13dBm*");
    } else if (temp.indexOf("11dBm") == 0) {
      wifipow = WIFI_POWER_11dBm;
      log_i("wifipow = *WIFI_POWER_11dBm*");
    } else if (temp.indexOf("8.5dBm") == 0) {
      wifipow = WIFI_POWER_8_5dBm;
      log_i("wifipow = *WIFI_POWER_8_5dBm*");
    } else if (temp.indexOf("7dBm") == 0) {
      wifipow = WIFI_POWER_7dBm;
      log_i("wifipow = *WIFI_POWER_7dBm*");
    } else if (temp.indexOf("5dBm") == 0) {
      wifipow = WIFI_POWER_5dBm;
      log_i("wifipow = *WIFI_POWER_5dBm*");
    } else if (temp.indexOf("2dBm") == 0) {
      wifipow = WIFI_POWER_2dBm;
      log_i("wifipow = *WIFI_POWER_2dBm*");
    } else if (temp.indexOf("-1dBm") == 0) {
      wifipow = WIFI_POWER_MINUS_1dBm;
      log_i("wifipow = *WIFI_POWER_MINUS_1dBm*");
    } else {
      log_e("Invalid WIFIPOW value. Falling back to default value");
    }
  } else {
    log_e("Error parsing WIFIPOW line. Falling back to default value");
  }
  //o3zeroval
  if (command[4].startsWith("o3_zero_value", 0)) {
    o3zeroval = command[4].substring(command[4].indexOf('=') + 1, command[4].length()).toInt();
  } else {
    log_e("Error parsing O3ZEROVAL line. Falling back to default value");
  }
  log_i("o3zeroval = *%d*", o3zeroval);
  //avg_measurements
  if (command[5].startsWith("average_measurements", 0)) {
    avg_measurements = command[5].substring(command[5].indexOf('=') + 1, command[5].length()).toInt();
  } else {
    log_e("Error parsing AVG_MEASUREMENTS line. Falling back to default value");
  }
  log_i("avg_measurements = *%d*", avg_measurements);
  //avg_delay
  if (command[6].startsWith("average_delay(seconds)", 0)) {
    avg_delay = command[6].substring(command[6].indexOf('=') + 1, command[6].length()).toInt();
  } else {
    log_e("Error parsing AVG_DELAY line. Falling back to default value");
  }
  log_i("avg_delay = *%d*", avg_delay);
  //sealevelalt
  if (command[7].startsWith("sea_level_altitude", 0)) {
    sealevelalt = command[7].substring(command[7].indexOf('=') + 1, command[7].length()).toFloat();
  } else {
    log_e("Error parsing SEALEVELALT line. Falling back to default value");
  }
  log_i("sealevelalt = *%.2f*", sealevelalt);
  //server
  if (command[8].startsWith("upload_server", 0)) {
    temp = "";
    temp = command[8].substring(command[8].indexOf('=') + 1, command[8].length());
    if (temp.length() > 0) {
      server = temp;
      server_ok = true;
    } else {
#ifdef API_SERVER
      log_e("SERVER value is empty. Falling back to value defined at compile time");
#else
      log_e("SERVER value is empty!");
#endif
    }
  } else {
#ifdef API_SERVER
    log_e("Error parsing SERVER line. Falling back to value defined at compile time");
#else
    log_e("Error parsing SERVER line!");
#endif
  }
  log_i("server = *%s*", server.c_str());
  //mics_calibration_values
  if (command[9].startsWith("mics_calibration_values", 0)) {
    MICSCal[0] = command[9].substring(command[9].indexOf("RED:") + 4, command[9].indexOf(",OX:")).toInt();
    MICSCal[1] = command[9].substring(command[9].indexOf(",OX:") + 4, command[9].indexOf(",NH3:")).toInt();
    MICSCal[2] = command[9].substring(command[9].indexOf(",NH3:") + 5, command[9].length()).toInt();
  } else {
    log_e("Error parsing MICSCal[] line. Falling back to default value");
  }
  log_i("MICSCal[] = *%d*, *%d*, *%d*", MICSCal[0], MICSCal[1], MICSCal[2]);
  //mics_measurements_offsets
  if (command[10].startsWith("mics_measurements_offsets", 0)) {
    MICSOffset[0] = command[10].substring(command[10].indexOf("RED:") + 4, command[10].indexOf(",OX:")).toInt();
    MICSOffset[1] = command[10].substring(command[10].indexOf(",OX:") + 4, command[10].indexOf(",NH3:")).toInt();
    MICSOffset[2] = command[10].substring(command[10].indexOf(",NH3:") + 5, command[10].length()).toInt();
  } else {
    log_e("Error parsing MICSOffset[] line. Falling back to default value");
  }
  log_i("MICSOffset[] = *%d*, *%d*, *%d*", MICSOffset[0], MICSOffset[1], MICSOffset[2]);
  //compensation_factors
  if (command[11].startsWith("compensation_factors", 0)) {
    compH = command[11].substring(command[11].indexOf("compH:") + 6, command[11].indexOf(",compT:")).toFloat();
    compT = command[11].substring(command[11].indexOf(",compT:") + 7, command[11].indexOf(",compP:")).toFloat();
    compP = command[11].substring(command[11].indexOf(",compP:") + 7, command[11].length()).toFloat();
  } else {
    log_e("Error parsing compensation_factors line. Falling back to default value");
  }
  log_i("compH = *%.4f*, compT = *%.4f*, compP = *%.4f*\n", compH, compT, compP);

  return outcome;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool checkConfig(const char *configpath) { // verifies the existance of the configuration file, creates the file if not found

  File cfgfile;

  if (SD.exists(configpath)) {
    cfgfile = SD.open(configpath, FILE_READ);// open read only
    log_i("Found config file. Parsing...\n");

    if (parseConfig(cfgfile)) {
      return true;
    } else {
      log_e("Error parsing config file! No network configuration!\n");
      drawTwoLines("Cfg error!", "No web!", 3);
      return false;
    }

  } else {
    log_e("Couldn't find config file! Creating a new one with template...");
    drawTwoLines("No cfg found!", "Creating...", 2);
    cfgfile = SD.open(configpath, FILE_WRITE); // open r/w

    if (cfgfile) {
      // template for default config file
      String conftemplate = "#ssid=;\n#password=;\n#device_id=;\n#wifi_power=";
      conftemplate += "17dBm";
      conftemplate += ";\n#o3_zero_value=";
      conftemplate += String(o3zeroval);
      conftemplate += ";\n#average_measurements=";
      conftemplate += String(avg_measurements);
      conftemplate += ";\n#average_delay(seconds)=";
      conftemplate += String(avg_delay);
      conftemplate += ";\n#sea_level_altitude=";
      conftemplate += String(sealevelalt);
      conftemplate += ";\n#upload_server=;\n#mics_calibration_values=";
      conftemplate += "RED:" + String(MICSCal[0]) + ",OX:" + String(MICSCal[1]) + ",NH3:" + String(MICSCal[2]);
      conftemplate += ";\n#mics_measurements_offsets=";
      conftemplate += "RED:" + String(MICSOffset[0]) + ",OX:" + String(MICSOffset[1]) + ",NH3:" + String(MICSOffset[2]);
      conftemplate += ";\n#compensation_factors=";
      conftemplate += "compH:" + String(compH, 1) + ",compT:" + String(compT, 3) + ",compP:" + String(compP, 4);
      conftemplate += ";\n\no3_zero_value disables the O3 sensor when set to -1. For normal operation the default offset is 1489.\n";
      conftemplate += "\nAccepted wifi_power values are: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5 dBm.\n";
      conftemplate += "\nsea_level_altitude is in meters and it must be changed according to the current location of the device. 122.0 meters is the average altitude in Milan, Italy.\n";
      cfgfile.println(conftemplate);
      log_i("New config file with template created!\n");
      cfgfile.close();
      drawTwoLines("Done! Please", "insert data!", 2);
    } else {
      log_e("Error writing to SD Card!\n");
      drawTwoLines("Error while", "writing SD Card!", 2);
    }

    return false;
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool checkLogFile() { // verifies the existance of the csv log using the logpath var, creates the file if not found

  if (!SD.exists(logpath)) {
    log_e("Couldn't find log file. Creating a new one...\n");
    File filecsv = SD.open(logpath, FILE_WRITE);

    if (filecsv) { // Creating logfile and adding header string
      String heads = "sent_ok?;recordedAt;date;time;temp;hum;PM1;PM2_5;PM10;pres;radiation;nox;co;nh3;o3;voc;msp";
      filecsv.println(heads);
      log_i("Log file created!\n");
      filecsv.close();
      return true;
    }

    log_e("Error creating log file!\n");
    return false;
  }

  log_i("Log file present!\n");
  return true;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool addToLog(const char *path, const char *oldpath, String *message) { // adds new line to the log file at the top, after the header lines
  
  String temp = "";
  log_v("Log file is located at: %s\n", path);
  log_v("Old path is: %s\n", oldpath);
  if (!SD.exists(oldpath)) {
    SD.rename(path, oldpath);
  } else {
    if (SD.exists(path)) SD.remove(path);
    log_e("An error occurred, resuming logging from the old log...\n");
  }
  File oldfile = SD.open(oldpath, FILE_READ); // opening the renamed log
  if (!oldfile) {
    log_e("Error opening the renamed the log file!");
    return false;
  }
  File logfile = SD.open(path, FILE_WRITE); // recreates empty logfile
  if (!logfile) {
    log_e("Error recreating the log file!");
    return false;
  }
  bool newline_ok = false;
  while (oldfile.available()) { // copy the old log file with new string added
    temp = oldfile.readStringUntil('\r'); // reads until CR character
    logfile.println(temp);
    oldfile.readStringUntil('\n'); // consumes LF character (uses DOS-style CRLF)
    if (!newline_ok) {
      logfile.println(*message); // printing the new string, only once and after the header
      log_v("New line added!\n");
      newline_ok = true;
    }
  }
  oldfile.close();
  logfile.close();
  SD.remove(oldpath); // deleting old log

  return true;
  
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void logToSD() { // builds a new logfile line and calls addToLog() (using logpath global var) to add it

  log_i("Logging data to the .csv on the SD Card...\n");

  String logvalue = "";
  char timeFormat[29] = {0};
  if (datetime_ok) strftime(timeFormat, sizeof(timeFormat), "%Y-%m-%dT%T.000Z", &timeinfo); // formatting date&time in TZ format
  
  // Data is layed out as follows:
  // "sent_ok?;recordedAt;date;time;temp;hum;PM1;PM2_5;PM10;pres;radiation;nox;co;nh3;o3;voc;msp"
  
  logvalue += (sent_ok) ? "OK" : "ERROR";
  logvalue += ";";
  logvalue += String(timeFormat);
  logvalue += ";";
  logvalue += String(Date);
  logvalue += ";";
  logvalue += String(Time);
  logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(temp);
  }
  logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(hum);
  }
  logvalue += ";";
  if (PMS_run) {
    logvalue += String(PM1);
  }
  logvalue += ";";
  if (PMS_run) {
    logvalue += String(PM25);
  }
  logvalue += ";";
  if (PMS_run) {
    logvalue += String(PM10);
  }
  logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(pre);
  }
  logvalue += ";";
  logvalue += ";"; // for "radiation"
  if (MICS_run) {
    logvalue += floatToComma(MICS_NO2);
  }
  logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_CO);
  }
  logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_NH3);
  }
  logvalue += ";";
  if (O3_run) {
    logvalue += floatToComma(ozone);
  }
  logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(VOC);
  }
  logvalue += ";";
  logvalue += String(MSP);

  String oldlogpath = logpath + ".old";

  if (addToLog(logpath.c_str(), oldlogpath.c_str(), &logvalue)) {
    log_i("SD Card log file updated successfully!\n");
  } else {
    log_e("Error updating SD Card log file!\n");
    drawTwoLines("SD Card log", "error!", 3);
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#endif