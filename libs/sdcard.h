/*
                        Milano Smart Park Firmware
                   Copyright (c) 2021 Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

             Parts of this code are based on open source works
                 freely distributed by Luca Crotti @2019
*/

// SD Card and File Management Functions

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool initializeSD() { // checks for SD Card presence and type

  short timeout = 0;
  while (!SD.begin()) {
    if (timeout > 4) { // errors after 10 seconds
      log_e("No SD Card detected! No internet connection possible!\n");
      drawTwoLines(10, "No SD Card!", 25, "No web!", 3);
      return false;
    }
    delay(1000); // giving it some time to detect the card properly
    timeout++;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_MMC) {
    log_i("SD Card type: MMC");
  } else if (cardType == CARD_SD) {
    log_i("SD Card type: SD");
  } else if (cardType == CARD_SDHC) {
    log_i("SD Card type: SDHC");
  } else {
    log_e("Unidentified Card type, format the SD Card!  No internet connection possible!\n");
    drawTwoLines(5, "SD Card format!", 25, "No web!", 3);
    return false;
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  delay(300);
  log_i("SD Card size: %lluMB\n", cardSize);
  return true;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void appendFile(fs::FS &fs, const char path[], const char message[]) { // appends a new line to a specified file

  log_v("Appending to file: %s", path);
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    log_e("Failed to open file for appending!\n");
    return;
  }
  if (file.println(message)) {
    log_v("String appended\n");
  } else {
    log_e("Append failed!\n");
  }
  file.close();

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool parseConfig(File fl) { // parses the configuration file on the SD Card

  bool outcome = true;
  String command[11];
  String temp;
  int i = 0;
  unsigned long lastpos = 0;
  while (fl.available() && i < 11) {   // Storing the config file in a string array
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
    ssid = command[0].substring(command[0].indexOf("ssid") + 5, command[0].length());
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
    passw = command[1].substring(command[1].indexOf("password") + 9, command[1].length());
    if (passw.length() == 0) {
      log_e("PASSW value is empty!");
      outcome = false;
    } else {
      log_i("passw = *%s*", passw.c_str());
    }
  } else {
    log_e("Error parsing PASSW line!");
    outcome = false;
  }
  //deviceid
  if (command[2].startsWith("codice", 0)) {
    deviceid = command[2].substring(command[2].indexOf("codice") + 7, command[2].length());
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
  if (command[3].startsWith("potenza_wifi", 0)) {
    temp = "";
    temp = command[3].substring(command[3].indexOf("potenza_wifi") + 13, command[3].length());
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
  //waittime
  if (command[4].startsWith("attesa(minuti)", 0)) {
    temp = "";
    temp = command[4].substring(command[4].indexOf("attesa(minuti)") + 15, command[4].length());
    waittime = temp.toInt();
  } else {
    log_e("Error parsing WAITTIME line. Falling back to default value");
  }
  log_i("waittime = *%d*", waittime);
  //avg_measurements
  if (command[5].startsWith("numero_misurazioni_media", 0)) {
    temp = "";
    temp = command[5].substring(command[5].indexOf("numero_misurazioni_media") + 25, command[5].length());
    avg_measurements = temp.toInt();
  } else {
    log_e("Error parsing AVG_MEASUREMENTS line. Falling back to default value");
  }
  log_i("avg_measurements = *%d*", avg_measurements);
  //avg_delay
  if (command[6].startsWith("ritardo_media(secondi)", 0)) {
    temp = "";
    temp = command[6].substring(command[6].indexOf("ritardo_media(secondi)") + 23, command[6].length());
    avg_delay = temp.toInt();
  } else {
    log_e("Error parsing AVG_DELAY line. Falling back to default value");
  }
  log_i("avg_delay = *%d*", avg_delay);
  //sealevelalt
  if (command[9].startsWith("altitudine_s.l.m.", 0)) {
    temp = "";
    temp = command[9].substring(command[9].indexOf("altitudine_s.l.m.") + 18, command[9].length());
    sealevelalt = temp.toFloat();
  } else {
    log_e("Error parsing SEALEVELALT line. Falling back to default value");
  }
  log_i("sealevelalt = *%.2f*", sealevelalt);
  //server
  if (command[10].startsWith("upload_server", 0)) {
    temp = "";
    temp = command[10].substring(command[10].indexOf("upload_server") + 14, command[10].length());
	if (temp.length() > 0) {
      server = temp;
	  server_ok = true;
	  log_i("server = *%s*\n", server.c_str());
    } else {
	  #ifdef API_SERVER
	  log_e("SERVER value is empty. Falling back to value defined at compile time\n");
	  #else
	  log_e("SERVER value is empty!\n");
	  #endif
    }
  } else {
	#ifdef API_SERVER
	log_e("Error parsing SERVER line. Falling back to value defined at compile time\n");
	#else
	log_e("Error parsing SERVER line!\n");
	#endif
  }

  return outcome;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool checkConfig() { // verifies the existance of the configuration file, creates the file if not found

  File cfgfile;

  if (SD.exists("/config_v2.cfg")) {
    cfgfile = SD.open("/config_v2.cfg", FILE_READ);// open read only
    log_i("Found config file. Parsing...\n");

    if (parseConfig(cfgfile)) {
      return true;
    } else {
      log_e("Error parsing config file! No network configuration!\n");
      drawTwoLines(5, "Cfg error!", 25, "No web!", 3);
      return false;
    }

  } else {
    log_e("Couldn't find config file! Creating a new one with template...");
    drawTwoLines(5, "No cfg found!", 25, "No web!", 3);
    cfgfile = SD.open("/config_v2.cfg", FILE_WRITE); // open r/w

    if (cfgfile) {
      cfgfile.close();
      appendFile(SD, "/config_v2.cfg", "#ssid=;\n#password=;\n#codice=;\n#potenza_wifi=19.5dBm;\n#attesa(minuti)=0;\n#numero_misurazioni_media=6;\n#ritardo_media(secondi)=273;\n#attiva_MQ7=0;\n#abilita_IAQ=0;\n#altitudine_s.l.m.=122.0;\n#upload_server=;\n\n//altitudine_s.l.m. influenza la misura della pressione atmosferica e va adattata a seconda della zona. 122m è l'altitudine di Milano\n//Valori possibili per potenza_wifi: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5 dBm");
      log_i("New config file with template created!\n");
    } else {
      log_e("Error writing to SD Card!\n");
    }
 
    return false;
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void checkLogFile() { // verifies the existance of the csv log using the logpath var, creates the file if not found

  if (!SD.exists(logpath)) {
    log_e("Couldn't find log file. Creating a new one...\n");
    File filecsv = SD.open(logpath, FILE_WRITE);

    if (filecsv) {
      filecsv.close();
      String headertext = "Log file of device " + deviceid + " | Firmware v" + ver + " | MAC Address: " + macAdr;
      appendFile(SD, logpath.c_str(), headertext.c_str());
      appendFile(SD, logpath.c_str(), "Date;Time;Temp(*C);Hum(%);Pre(hPa);PM10(ug/m3);PM2,5(ug/m3);PM1(ug/m3);NOx(ug/m3);CO(ug/m3);O3(ug/m3);VOC(kOhm);NH3(ug/m3);C3H8(ug/m3);C4H10(ug/m3);CH4(ug/m3);H2(ug/m3);C2H5OH(ug/m3)");
      log_i("Log file created!\n");
      return;
    }
    
    log_e("Error creating log file!\n");
    return;
  }

  log_i("Log file present!\n");
  return;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool addToLog(fs::FS &fs, const char path[], String *message) { // adds new line to the log file at the top, after the header lines

  String temp = "";
  log_v("Log file is located at: %s\n", path);
  File logfile = fs.open(path, FILE_READ);
  if (!logfile) {
    log_e("Error opening the log file!");
    return false;
  }
  File tempfile = fs.open("/templog", FILE_WRITE);
  if (!tempfile) {
    log_e("Error creating templog!");
    logfile.close();
    return false;
  }
  while (logfile.available()) { // copies entire log file into temp log
    temp = logfile.readStringUntil('\r'); //reads until carriage return character
    tempfile.print(temp);
    tempfile.print('_'); // uses underscore to separate lines
    temp = logfile.readStringUntil('\n'); //discarding line feed character
  }
  logfile.close();
  tempfile.close();
  fs.remove(path); // deletes log file
  logfile = fs.open(path, FILE_WRITE); // recreates empty logfile
  if (!logfile) {
    log_e("Error recreating the log file!");
    return false;
  }
  tempfile = fs.open("/templog", FILE_READ);
  if (!tempfile) {
    log_e("Error reopening templog!");
    return false;
  }
  for (int i = 0; i < 2; i++) { // copying the header lines from tempfile
    temp = tempfile.readStringUntil('_');
    logfile.println(temp);
  }
  logfile.println(*message); // printing the new line
  while (tempfile.available()) { // copying the remaining strings
    temp = tempfile.readStringUntil('_');
    logfile.println(temp);
  }
  tempfile.close();
  logfile.close();
  fs.remove("/templog");

  return true;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void logToSD() { // builds a new logfile line and calls addToLog() (using logpath global var) to add it

  log_v("Logging data to the .csv on the SD Card...");

  String logvalue = "";

  // Data is layed out as follows:
  // "Date;Time;Temp(*C);Hum(%);Pre(hPa);PM10(ug/m3);PM2,5(ug/m3);PM1(ug/m3);NOx(ug/m3);CO(ug/m3);O3(ug/m3);VOC(kOhm);NH3(ug/m3);C3H8(ug/m3);C4H10(ug/m3);CH4(ug/m3);H2(ug/m3);C2H5OH(ug/m3)"

  logvalue += dayStamp; logvalue += ";";
  logvalue += timeStamp; logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(temp);
  } logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(hum);
  } logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(pre);
  } logvalue += ";";
  if (PMS_run) {
    logvalue += String(PM10);
  } logvalue += ";";
  if (PMS_run) {
    logvalue += String(PM25);
  } logvalue += ";";
  if (PMS_run) {
    logvalue += String(PM1);
  } logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_NO2);
  } logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_CO);
  } logvalue += ";";
  if (O3_run) {
    logvalue += floatToComma(ozone);
  } logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(VOC);
  } logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_NH3);
  } logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_C3H8);
  } logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_C4H10);
  } logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_CH4);
  } logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_H2);
  } logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(MICS_C2H5OH);
  }

  if (addToLog(SD, logpath.c_str(), &logvalue)) {
    log_i("SD Card log file updated successfully!\n");
  } else {
    log_e("SD Card log file not updated!\n");
    drawTwoLines(30, "SD Card log", 30, "error!", 3);
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++