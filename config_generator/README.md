# MSP Firmware Config Generator & Flasher

Una GUI cross-platform completa per generare file `config_v4.json` e flashare il firmware ESP32 per il progetto MSP.

## 🎯 Caratteristiche

### ✅ Configurazione JSON
- Interfaccia grafica intuitiva con tutti i parametri di configurazione
- Validazione automatica dei campi
- Help contestuale per ogni campo
- Valori di default pre-caricati
- Possibilità di caricare e modificare configurazioni esistenti

### ⚡ Flash ESP32
- **Rilevamento automatico** delle porte COM disponibili
- **Rilevamento automatico** della dimensione flash ESP32
- **Download automatico** dell'ultima release da GitHub
- **Flash firmware** con barra di progresso in tempo reale
- **Erase flash** per ripristino completo
- Log dettagliato di tutte le operazioni

### 🏗️ Architettura Modulare
- **config_data.py**: Tutti i valori di default, help text e costanti (facilmente modificabile)
- **config_generator_gui.py**: Interfaccia grafica principale
- **esp32_utils.py**: Utilità per comunicazione con ESP32
- **github_utils.py**: Utilità per download da GitHub

## 📦 Installazione

### Prerequisiti
- Python 3.7 o superiore
- pip (package manager Python)

### 1. Creare l'ambiente virtuale e installare le dipendenze

#### macOS/Linux:
```bash
cd config_generator
./run_gui.sh
```
Lo script creerà automaticamente l'ambiente virtuale e installerà tutte le dipendenze!

#### Windows:
Doppio click su `run_gui.bat` oppure:
```cmd
cd config_generator
run_gui.bat
```

### 2. Installazione manuale (opzionale)

Se preferisci installare manualmente:

```bash
cd config_generator
python3 -m venv venv
source venv/bin/activate  # Linux/macOS
# oppure
venv\Scripts\activate     # Windows

pip install -r requirements.txt
python config_generator_gui.py
```

## 🚀 Utilizzo

### Avvio Rapido
```bash
./run_gui.sh  # macOS/Linux
run_gui.bat   # Windows
```

### Tab 1: Configuration (📝)

#### Funzionalità

1. **Generate Config**: Compila i campi e genera il file JSON
   - Tutti i valori vengono validati automaticamente
   - Puoi scegliere dove salvare il file

2. **Load Config**: Carica un file di configurazione esistente per modificarlo
   - Tutti i valori vengono caricati nella GUI
   - Puoi modificare e salvare con un nuovo nome

3. **Reset to Defaults**: Ripristina tutti i valori di default

4. **Help (?)**: Ogni campo ha un pulsante con informazioni dettagliate

5. **Password Toggle (👁️)**: Mostra/nascondi password WiFi in chiaro
   - Click sull'icona 👁️ per visualizzare la password
   - Click sull'icona 🔒 per nasconderla nuovamente
   - Aiuta a verificare che la password sia stata inserita correttamente

#### Sezioni di Configurazione

- **Network Settings**: SSID, Password, WiFi Power
- **Device Settings**: Device ID, Upload Server
- **Sensor Settings**: Tipo sensore gas, calibrazione O3, misurazioni medie, altitudine
- **MICS Calibration Values**: Valori di calibrazione per sensori RED, OX, NH3
- **MICS Measurement Offsets**: Offset per le misurazioni
- **Compensation Factors**: Fattori di compensazione per umidità, temperatura, pressione
- **Modem Settings**: Configurazione modem cellulare (opzionale)
- **Time Settings**: Server NTP e timezone
- **Firmware Settings**: Auto upgrade del firmware

### Tab 2: ESP32 Flash (⚡)

#### 1. Selezione Dispositivo
- **Porta COM**: Dropdown con tutte le porte seriali disponibili
- **🔄 Refresh**: Aggiorna la lista delle porte
- **🔍 Detect**: Rileva automaticamente le informazioni del chip ESP32
  - Tipo di chip (ESP32, ESP32-S2, ESP32-C3, etc.)
  - Indirizzo MAC
  - Dimensione della flash

#### 2. Download Firmware
Hai due opzioni:

**Opzione A: Download da GitHub (Consigliata)**
- Clicca su **"📥 Download Latest Release from GitHub"**
- Lo script scaricherà automaticamente l'ultima release dal repository
- Il file verrà salvato in `~/Downloads/msp-firmware/`
- Mostra la versione scaricata

**Opzione B: File Locale**
- Clicca su **"📂 Select Local .bin File"**
- Seleziona un file `.bin` già presente sul tuo computer

#### 3. Flash Firmware
- Clicca su **"⚡ Flash Firmware"**
- Conferma l'operazione
- Il log mostrerà il progresso in tempo reale
- Al termine riceverai una notifica di successo o errore

#### 4. Erase Flash (Opzionale)
- Clicca su **"🗑️ Erase Flash"** per cancellare completamente la memoria
- Utile per ripristini completi o troubleshooting

## 📝 Personalizzazione

### Modificare Valori di Default e Help Text

Apri il file [config_data.py](config_data.py) e modifica:

```python
# Valori di default
DEFAULT_VALUES = {
    "ssid": "",  # Cambia qui il valore di default
    "wifi_power": "17",  # Modifica il WiFi power di default
    # ... altri valori
}

# Testi di help
HELP_TEXTS = {
    "ssid": "Il tuo testo di help personalizzato qui",
    # ... altri help text
}
```

### Modificare Valori Consentiti

```python
WIFI_POWER_VALUES = ["-1", "2", "5", "7", "8.5", "11", "13", "15", "17", "18.5", "19", "19.5"]
# Aggiungi o rimuovi valori dalla lista
```

### Modificare Repository GitHub

```python
GITHUB_REPO = "A-A-Milano-Smart-Park/msp-firmware"
# Cambia con il tuo repository
```

## 🛠️ Struttura File

```
config_generator/
├── config_data.py              # ⚙️ CONFIGURAZIONE (modifica qui!)
├── config_generator_gui.py     # 🖥️ GUI principale
├── esp32_utils.py             # 🔧 Utilità ESP32
├── github_utils.py            # 📥 Utilità GitHub
├── requirements.txt           # 📦 Dipendenze Python
├── run_gui.sh                # 🚀 Launcher macOS/Linux
├── run_gui.bat               # 🚀 Launcher Windows
├── README.md                 # 📖 Questo file
└── venv/                     # 🐍 Ambiente virtuale Python
```

## 📋 Dipendenze

- **tkinter**: GUI (incluso in Python)
- **pyserial**: Comunicazione seriale con ESP32
- **esptool**: Tool per flash ESP32
- **requests**: Download da GitHub

## 🔧 Risoluzione Problemi

### Porte seriali non rilevate

**macOS:**
- Assicurati di avere i driver CH340/CP2102 installati
- Controlla con: `ls /dev/tty.*`

**Linux:**
- Aggiungi il tuo utente al gruppo `dialout`:
  ```bash
  sudo usermod -a -G dialout $USER
  ```
- Riavvia la sessione

**Windows:**
- Installa i driver CH340 o CP2102
- Verifica in Gestione Dispositivi

### Errore "Permission denied" durante il flash

**macOS/Linux:**
```bash
sudo chmod 666 /dev/ttyUSB0  # o la tua porta
```

### tkinter non trovato

**Ubuntu/Debian:**
```bash
sudo apt-get install python3-tk
```

**Fedora:**
```bash
sudo dnf install python3-tkinter
```

### Errore durante il download da GitHub

- Verifica la connessione internet
- Controlla che il repository sia pubblico
- GitHub potrebbe avere rate limiting (aspetta qualche minuto)

## 🎓 Esempi di Utilizzo

### Workflow Completo

1. **Avvia l'applicazione**: `./run_gui.sh`
2. **Tab Configuration**:
   - Compila SSID e Password WiFi
   - Inserisci Device ID (es: MSP001)
   - Inserisci Upload Server URL
   - Modifica altri parametri se necessario
   - Clicca "Generate Config" e salva come `config_v4.json`
3. **Tab ESP32 Flash**:
   - Collega ESP32 via USB
   - Clicca "🔄 Refresh" per vedere la porta
   - Clicca "🔍 Detect" per verificare il chip
   - Clicca "📥 Download Latest Release from GitHub"
   - Clicca "⚡ Flash Firmware"
   - Attendi il completamento

### Solo Generazione Config

Se vuoi solo generare il file di configurazione senza flashare:
1. Usa solo la Tab "Configuration"
2. Compila i campi necessari
3. Genera e salva il file JSON

### Solo Flash Firmware

Se hai già il firmware `.bin`:
1. Vai alla Tab "ESP32 Flash"
2. Seleziona la porta
3. Clicca "📂 Select Local .bin File"
4. Clicca "⚡ Flash Firmware"

## 📄 Licenza

Questo progetto fa parte del repository MSP Firmware.

## 🤝 Contributi

Per contribuire o segnalare problemi, visita:
https://github.com/A-A-Milano-Smart-Park/msp-firmware

## 📞 Supporto

Per domande o problemi, apri una issue su GitHub.
