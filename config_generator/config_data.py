"""
Configuration data module
Contiene tutti i valori di default, help text e costanti per il Config Generator
Modifica questo file per cambiare i valori di default e i testi di help
"""

# ============================================================================
# VALORI DI DEFAULT
# ============================================================================

DEFAULT_VALUES = {
    # Network Settings
    "ssid": "",
    "password": "",
    "wifi_power": "17",

    # Device Settings
    "device_id": "",
    "upload_server": "",

    # Sensor Settings
    "gas_sensor_type": "MICS6814",
    "o3_zero_value": "-1",
    "average_measurements": "30",
    "sea_level_altitude": "122.00",

    # MICS Calibration Values
    "mics_cal_red": "955",
    "mics_cal_ox": "900",
    "mics_cal_nh3": "163",

    # MICS Measurement Offsets
    "mics_offset_red": "0",
    "mics_offset_ox": "0",
    "mics_offset_nh3": "0",

    # Compensation Factors
    "comp_h": "0.6",
    "comp_t": "1.352",
    "comp_p": "0.0132",

    # Modem Settings
    "use_modem": False,
    "modem_apn": "",

    # Time Settings
    "ntp_server": "pool.ntp.org",
    "timezone": "GMT0",

    # Firmware Settings
    "fw_auto_upgrade": True,
}

# ============================================================================
# VALORI CONSENTITI (per dropdown/combobox)
# ============================================================================

WIFI_POWER_VALUES = ["-1", "2", "5", "7", "8.5", "11", "13", "15", "17", "18.5", "19", "19.5"]

AVERAGE_MEASUREMENTS_VALUES = ["1", "2", "3", "4", "5", "6", "10", "12", "15", "20", "30", "60"]

GAS_SENSOR_TYPES = {
    "MICS6814": 0,
    "MICS4514 (DFRobot SEN0377)": 1
}

# ============================================================================
# TESTI DI HELP
# ============================================================================

HELP_TEXTS = {
    # Network Settings
    "ssid": "Nome della rete WiFi a cui il dispositivo si connetterà",
    "password": "Password della rete WiFi",
    "wifi_power": "Potenza del segnale WiFi. Valori accettati: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5 dBm\n\nValori più alti = maggiore portata ma maggiore consumo",

    # Device Settings
    "device_id": "Identificatore univoco del dispositivo (es: MSP001, MSP002, etc.)\n\nQuesto ID viene usato per identificare il dispositivo nel sistema",
    "upload_server": "URL del server dove verranno caricati i dati delle misurazioni\n\nFormato: http://server.com/api/upload o https://server.com/api/upload",

    # Sensor Settings
    "gas_sensor_type": "Tipo di sensore gas installato sul dispositivo:\n\n0 = MICS6814 (sensore standard)\n1 = MICS4514 (DFRobot SEN0377)\n\nSeleziona il tipo corretto in base all'hardware installato",
    "o3_zero_value": "Valore di calibrazione zero per il sensore O3\n\nUsa -1 per la calibrazione automatica\nUsa un valore specifico se hai già calibrato il sensore",
    "average_measurements": "Numero di misurazioni da mediare prima di inviare i dati\n\nValori accettati: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60\n\nValori più alti = dati più stabili ma aggiornamenti meno frequenti",
    "sea_level_altitude": "Altitudine sul livello del mare della posizione del dispositivo in metri\n\nQuesto valore è necessario per la calibrazione corretta della pressione atmosferica\n\nEsempio: 122.0 metri è l'altitudine media di Milano, Italia",

    # MICS Calibration Values
    "mics_cal_red": "Valore di calibrazione per il sensore MICS RED (gas riducenti)\n\nQuesto valore viene ottenuto durante la calibrazione del sensore",
    "mics_cal_ox": "Valore di calibrazione per il sensore MICS OX (gas ossidanti)\n\nQuesto valore viene ottenuto durante la calibrazione del sensore",
    "mics_cal_nh3": "Valore di calibrazione per il sensore MICS NH3 (ammoniaca)\n\nQuesto valore viene ottenuto durante la calibrazione del sensore",

    # MICS Measurement Offsets
    "mics_offset_red": "Offset di correzione per le misurazioni del sensore RED\n\nUsa 0 se non necessario, altrimenti inserisci il valore di offset calcolato",
    "mics_offset_ox": "Offset di correzione per le misurazioni del sensore OX\n\nUsa 0 se non necessario, altrimenti inserisci il valore di offset calcolato",
    "mics_offset_nh3": "Offset di correzione per le misurazioni del sensore NH3\n\nUsa 0 se non necessario, altrimenti inserisci il valore di offset calcolato",

    # Compensation Factors
    "comp_h": "Fattore di compensazione per l'umidità\n\nValore di default: 0.6\nModifica solo se hai valori di calibrazione specifici",
    "comp_t": "Fattore di compensazione per la temperatura\n\nValore di default: 1.352\nModifica solo se hai valori di calibrazione specifici",
    "comp_p": "Fattore di compensazione per la pressione\n\nValore di default: 0.0132\nModifica solo se hai valori di calibrazione specifici",

    # Modem Settings
    "use_modem": "Abilita l'uso del modem cellulare per la connettività\n\nSeleziona questa opzione se il dispositivo usa una connessione cellulare invece del WiFi",
    "modem_apn": "APN (Access Point Name) del provider cellulare\n\nNecessario solo se 'Use Modem' è abilitato\nContatta il tuo provider per ottenere l'APN corretto",

    # Time Settings
    "ntp_server": "Indirizzo del server NTP per la sincronizzazione dell'ora\n\nDefault: pool.ntp.org (server NTP pubblico)\nPuoi usare server NTP locali per maggiore affidabilità",
    "timezone": "Definizione del fuso orario in formato TZ standard\n\nEsempi:\n- CET-1CEST,M3.5.0,M10.5.0/3 (Europa/Roma)\n- GMT0 (Greenwich)\n- EST5EDT,M3.2.0,M11.1.0 (US Eastern)\n\nDettagli: https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html",

    # Firmware Settings
    "fw_auto_upgrade": "Abilita l'aggiornamento automatico del firmware\n\nSe abilitato, il dispositivo controllerà e installerà automaticamente nuove versioni del firmware quando disponibili",
}

# ============================================================================
# TESTI DI HELP PER IL FILE JSON GENERATO
# ============================================================================

JSON_HELP_TEXTS = {
    "wifi_power": "Accepted values: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5 dBm",
    "average_measurements": "Accepted values: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60",
    "sea_level_altitude": "Value in meters, must be changed according to device location. 122.0 meters is the average altitude in Milan, Italy",
    "timezone": "Standard tz timezone definition. More details at https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html",
    "gas_sensor_type": "Gas sensor type: 0 = MICS6814, 1 = MICS4514 (DFRobot SEN0377)"
}

# ============================================================================
# CONFIGURAZIONE GITHUB
# ============================================================================

GITHUB_REPO = "A-A-Milano-Smart-Park/msp-firmware"
GITHUB_API_URL = f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest"

# ============================================================================
# CONFIGURAZIONE FLASH ESP32
# ============================================================================

# Parametri di flash per ESP32
FLASH_BAUDRATE = 921600
FLASH_MODE = "dio"
FLASH_FREQ = "40m"

# Dimensioni flash supportate (in MB)
FLASH_SIZES = {
    0x100000: "1MB",
    0x200000: "2MB",
    0x400000: "4MB",
    0x800000: "8MB",
    0x1000000: "16MB",
}

# Indirizzi di memoria per il flash (ESP32 standard)
FLASH_ADDRESSES = {
    "bootloader": "0x1000",
    "partitions": "0x8000",
    "app": "0x10000",
}
