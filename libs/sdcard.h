/*
                        Milano Smart Park Firmware
                       Developed by Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
*/

// SD Card and File Management Functions

#ifndef SDCARD_H
#define SDCARD_H

#include "network.h"

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

#define LINES 16

  bool outcome = true;
  String command[LINES];
  short line_cnt = 0;
  bool have_ssid = false;
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
  if (command[line_cnt].startsWith("ssid", 0)) {
    ssid = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length());
    if (ssid.length() > 0) {
      log_i("ssid = *%s*", ssid.c_str());
      have_ssid = true;
    } else {
      log_i("SSID value is empty");
    }
  } else {
    log_e("Error parsing SSID line!");
    outcome = false;
  }
  line_cnt++;
  //passw
  if (command[line_cnt].startsWith("password", 0)) {
    passw = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length());
    log_i("passw = *%s*", passw.c_str());
  } else {
    log_e("Error parsing PASSWORD line!");
  }
  line_cnt++;
  //deviceid
  if (command[line_cnt].startsWith("device_id", 0)) {
    deviceid = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length());
    if (deviceid.length() == 0) {
      log_e("DEVICEID value is empty!");
      outcome = false;
    } else {
      log_i("deviceid = *%s*", deviceid.c_str());
    }
  } else {
    log_e("Error parsing DEVICE_ID line!");
    outcome = false;
  }
  line_cnt++;
  //wifipow
  if (command[line_cnt].startsWith("wifi_power", 0)) {
    temp = "";
    temp = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length());
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
    log_e("Error parsing WIFI_POWER line. Falling back to default value");
  }
  line_cnt++;
  //o3zeroval
  if (command[line_cnt].startsWith("o3_zero_value", 0)) {
    o3zeroval = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length()).toInt();
  } else {
    log_e("Error parsing O3_ZERO_VALUE line. Falling back to default value");
  }
  log_i("o3zeroval = *%d*", o3zeroval);
  line_cnt++;
  //avg_measurements
  if (command[line_cnt].startsWith("average_measurements", 0)) {
    avg_measurements = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length()).toInt();
  } else {
    log_e("Error parsing AVERAGE_MEASUREMENTS line. Falling back to default value");
  }
  log_i("avg_measurements = *%d*", avg_measurements);
  line_cnt++;
  //avg_delay
  if (command[line_cnt].startsWith("average_delay(seconds)", 0)) {
    avg_delay = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length()).toInt();
  } else {
    log_e("Error parsing AVERAGE_DELAY(SECONDS) line. Falling back to default value");
  }
  log_i("avg_delay = *%d*", avg_delay);
  line_cnt++;
  //sealevelalt
  if (command[line_cnt].startsWith("sea_level_altitude", 0)) {
    sealevelalt = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length()).toFloat();
  } else {
    log_e("Error parsing SEA_LEVEL_ALTITUDE line. Falling back to default value");
  }
  log_i("sealevelalt = *%.2f*", sealevelalt);
  line_cnt++;
  //server
  if (command[line_cnt].startsWith("upload_server", 0)) {
    temp = "";
    temp = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length());
    if (temp.length() > 0) {
      server = temp;
      server_ok = true;
    } else {
#ifdef API_SERVER
      log_i("SERVER value is empty. Falling back to value defined at compile time");
#else
      log_e("SERVER value is empty!");
#endif
    }
  } else {
#ifdef API_SERVER
    log_i("Error parsing UPLOAD_SERVER line. Falling back to value defined at compile time");
#else
    log_e("Error parsing UPLOAD_SERVER line!");
#endif
  }
  log_i("server = *%s*", server.c_str());
  line_cnt++;
  //mics_calibration_values
  if (command[line_cnt].startsWith("mics_calibration_values", 0)) {
    MICSCal[0] = command[line_cnt].substring(command[line_cnt].indexOf("RED:") + 4, command[line_cnt].indexOf(",OX:")).toInt();
    MICSCal[1] = command[line_cnt].substring(command[line_cnt].indexOf(",OX:") + 4, command[line_cnt].indexOf(",NH3:")).toInt();
    MICSCal[2] = command[line_cnt].substring(command[line_cnt].indexOf(",NH3:") + 5, command[line_cnt].length()).toInt();
  } else {
    log_e("Error parsing MICS_CALIBRATION_VALUES line. Falling back to default value");
  }
  log_i("MICSCal[] = *%d*, *%d*, *%d*", MICSCal[0], MICSCal[1], MICSCal[2]);
  line_cnt++;
  //mics_measurements_offsets
  if (command[line_cnt].startsWith("mics_measurements_offsets", 0)) {
    MICSOffset[0] = (int16_t) command[line_cnt].substring(command[line_cnt].indexOf("RED:") + 4, command[line_cnt].indexOf(",OX:")).toInt();
    MICSOffset[1] = (int16_t) command[line_cnt].substring(command[line_cnt].indexOf(",OX:") + 4, command[line_cnt].indexOf(",NH3:")).toInt();
    MICSOffset[2] = (int16_t) command[line_cnt].substring(command[line_cnt].indexOf(",NH3:") + 5, command[line_cnt].length()).toInt();
  } else {
    log_e("Error parsing MICS_MEASUREMENTS_OFFSETS line. Falling back to default value");
  }
  log_i("MICSOffset[] = *%d*, *%d*, *%d*", MICSOffset[0], MICSOffset[1], MICSOffset[2]);
  line_cnt++;
  //compensation_factors
  if (command[line_cnt].startsWith("compensation_factors", 0)) {
    compH = command[line_cnt].substring(command[line_cnt].indexOf("compH:") + 6, command[line_cnt].indexOf(",compT:")).toFloat();
    compT = command[line_cnt].substring(command[line_cnt].indexOf(",compT:") + 7, command[line_cnt].indexOf(",compP:")).toFloat();
    compP = command[line_cnt].substring(command[line_cnt].indexOf(",compP:") + 7, command[line_cnt].length()).toFloat();
  } else {
    log_e("Error parsing COMPENSATION_FACTORS line. Falling back to default value");
  }
  log_i("compH = *%.4f*, compT = *%.4f*, compP = *%.4f*", compH, compT, compP);
  line_cnt++;
  //use_modem
  if (command[line_cnt].startsWith("use_modem", 0)) {
    temp = "";
    temp = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length());
    if (temp.startsWith("true", 0)) use_modem = true;
  } else {
    log_e("Error parsing USE_MODEM line. Falling back to default value");
  }
  log_i("use_modem = *%s*", (use_modem) ? "true" : "false");
  if (!use_modem && !have_ssid) outcome = false;
  line_cnt++;
  //modem_apn
  if (command[line_cnt].startsWith("modem_apn", 0)) {
    temp = "";
    temp = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length());
    if (temp.length() > 0) {
      apn = temp;
    } else {
      log_i("APN value is empty");
    }
  } else {
    log_e("Error parsing MODEM_APN line!");
  }
  log_i("apn = *%s*\n", apn.c_str());
  if (use_modem && apn.length() == 0) outcome = false;
  line_cnt++;
  // Timezone and NTP server
  if (command[line_cnt].startsWith("ntp_server", 0)) {
    ntp_server = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length());
    if (ntp_server.length() == 0) {
      log_e("NTP_SERVER value is empty! Use default value: pool.ntp.org");
    } else {
      log_i("ntp_server = *%s*", ntp_server.c_str());
    }
  } else {
    log_e("Error parsing NTP_SERVER line!");
  }
  line_cnt++;
  if (command[line_cnt].startsWith("timezone", 0)) {
    timezone = command[line_cnt].substring(command[line_cnt].indexOf('=') + 1, command[line_cnt].length());
    if (timezone.length() == 0) {
      log_e("TIMEZONE value is empty! Use default value: CET-1CEST");
    } else {
      log_i("timezone = *%s*", timezone.c_str());
    }
  } else {
    log_e("Error parsing TIMEZONE line!");
  }
  

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
      String conftemplate =
        "#ssid=;\n"
        "#password=;\n"
        "#device_id=;\n"
        "#wifi_power=17dBm;\n"
        "#o3_zero_value=" + String(o3zeroval) + ";\n"
        "#average_measurements=" + String(avg_measurements) + ";\n"
        "#average_delay(seconds)=" + String(avg_delay) + ";\n"
        "#sea_level_altitude=" + String(sealevelalt) + ";\n"
        "#upload_server=;\n"
        "#mics_calibration_values=RED:" + String(MICSCal[0]) + ",OX:" + String(MICSCal[1]) + ",NH3:" + String(MICSCal[2]) + ";\n"
        "#mics_measurements_offsets=RED:" + String(MICSOffset[0]) + ",OX:" + String(MICSOffset[1]) + ",NH3:" + String(MICSOffset[2]) + ";\n"
        "#compensation_factors=compH:" + String(compH, 1) + ",compT:" + String(compT, 3) + ",compP:" + String(compP, 4) + ";\n"
        "#use_modem=" + String(use_modem ? "true" : "false") + ";\n"
        "#modem_apn=;\n"
        "#ntp_server=pool.ntp.org;\n"
        "#timezone=CET-1CEST;\n"
        "\n"
        "Accepted wifi_power values are: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5 dBm.\n"
        "\n"
        "sea_level_altitude is in meters and it must be changed according to the current location of the device. 122.0 meters is the average altitude in Milan, Italy.\n"
        "The timezone follows the standard definition in the tz timezone.\n"
        "More details at https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html";
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

bool addToLog(const char *path, const char *oldpath, const char *errpath, String *message) { // adds new line to the log file at the top, after the header lines
  
  String temp = "";
  log_v("Log file is located at: %s\n", path);
  log_v("Old path is: %s\n", oldpath);
  if (!SD.exists(oldpath)) {
    SD.rename(path, oldpath);
  } else {
    if (SD.exists(path)) SD.rename(path, errpath);
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
  temp = oldfile.readStringUntil('\r'); // reads until CR character
  logfile.println(temp);
  oldfile.readStringUntil('\n'); // consumes LF character (uses DOS-style CRLF)
  logfile.println(*message); // printing the new string, only once and after the header
  log_v("New line added!\n");
  while (oldfile.available()) { // copy the old log file with new string added
    temp = oldfile.readStringUntil('\r'); // reads until CR character
    logfile.println(temp);
    oldfile.readStringUntil('\n'); // consumes LF character (uses DOS-style CRLF)
  }
  oldfile.close();
  logfile.close();
  SD.remove(oldpath); // deleting old log

  return true;
  
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void logToSD(send_data_t *data) { // builds a new logfile line and calls addToLog() (using logpath global var) to add it

  log_i("Logging data to the .csv on the SD Card...\n");

  strftime(Date, sizeof(Date), "%d/%m/%Y", &data->sendTimeInfo);  // Formatting date as DD/MM/YYYY
  strftime(Time, sizeof(Time), "%T", &data->sendTimeInfo);        // Formatting time as HH:MM:SS

  String logvalue = "";
  char timeFormat[29] = {0};
  if (datetime_ok) strftime(timeFormat, sizeof(timeFormat), "%Y-%m-%dT%T.000Z", &data->sendTimeInfo); // formatting date&time in TZ format
  
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
    logvalue += floatToComma(data->temp);
  }
  logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(data->hum);
  }
  logvalue += ";";
  if (PMS_run) {
    logvalue += String(data->PM1);
  }
  logvalue += ";";
  if (PMS_run) {
    logvalue += String(data->PM25);
  }
  logvalue += ";";
  if (PMS_run) {
    logvalue += String(data->PM10);
  }
  logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(data->pre);
  }
  logvalue += ";";
  logvalue += ";"; // for "radiation"
  if (MICS_run) {
    logvalue += floatToComma(data->MICS_NO2);
  }
  logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(data->MICS_CO);
  }
  logvalue += ";";
  if (MICS_run) {
    logvalue += floatToComma(data->MICS_NH3);
  }
  logvalue += ";";
  if (O3_run) {
    logvalue += floatToComma(data->ozone);
  }
  logvalue += ";";
  if (BME_run) {
    logvalue += floatToComma(data->VOC);
  }
  logvalue += ";";
  logvalue += String(data->MSP);

  String oldlogpath = logpath + ".old";
  String errorpath = "logerror_" + String(timeFormat) + ".txt";

  if (addToLog(logpath.c_str(), oldlogpath.c_str(), errorpath.c_str(), &logvalue)) {
    log_i("SD Card log file updated successfully!\n");
  } else {
    log_e("Error updating SD Card log file!\n");
    drawTwoLines("SD Card log", "error!", 3);
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void readSD() {

  log_i("Initializing SD Card...\n");
  drawTwoLines("Initializing", "SD Card...", 1);
  SD_ok = initializeSD();
  if (SD_ok) {
    log_i("SD Card ok! Reading configuration...\n");
    drawTwoLines("SD Card ok!", "Reading config...", 1);
    cfg_ok = checkConfig("/config_v3.txt");
    if (!server_ok) {
      log_e("No server URL defined. Can't upload data!\n");
      drawTwoLines("No URL defined!", "No upload!", 6);
    }
    // if (avg_delay < 50) {
    //   log_e("AVG_DELAY should be at least 50 seconds! Setting to 50...\n");
    //   drawTwoLines("AVG_DELAY less than 50!", "Setting to 50...", 5);
    //   avg_delay = 50; // must be at least 45 for PMS5003 compensation routine, 5 seconds extra for reading cycle messages
    // }
    // setting the logpath variable
    logpath = "/log_" + deviceid + "_" + ver + ".csv";
    // checking logfile existance
    checkLogFile();
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#endif