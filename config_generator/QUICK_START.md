# 🚀 Quick Start Guide - MSP Config Generator

## ⚡ Avvio Rapido

### macOS / Linux
```bash
cd config_generator
./run_gui.sh
```

### Windows
```cmd
cd config_generator
run_gui.bat
```
Oppure doppio click su `run_gui.bat`

---

## 🎯 Workflow Completo

### 1️⃣ Genera Configurazione
1. Apri la tab **"📝 Configuration"**
2. Compila i campi richiesti:
   - **SSID**: Nome WiFi
   - **Password**: Password WiFi
     - 💡 **Tip**: Usa il pulsante 👁️ per vedere la password in chiaro e verificarla!
   - **Device ID**: Es. MSP001
   - **Upload Server**: URL del server
3. Clicca **"Generate Config"**
4. Salva come `config_v4.json`

### 2️⃣ Flash ESP32
1. **Collega ESP32** via USB
2. Vai alla tab **"⚡ ESP32 Flash"**
3. Clicca **"🔄 Refresh"** per vedere le porte
4. Seleziona la porta del tuo ESP32
5. Clicca **"🔍 Detect"** per verificare il chip
6. Clicca **"📥 Download Latest Release from GitHub"**
   - Il firmware verrà scaricato in `~/Downloads/msp-firmware/`
7. Clicca **"⚡ Flash Firmware"**
8. Attendi il completamento

### 3️⃣ Verifica
Il log mostrerà:
```
✅ Flash completato con successo!
```

---

## 🔧 Personalizzazione Rapida

### Cambiare Valori di Default

Apri **[config_data.py](config_data.py)** e modifica:

```python
DEFAULT_VALUES = {
    "ssid": "MyWiFi",              # ← Cambia qui il WiFi di default
    "wifi_power": "17",            # ← Cambia potenza WiFi
    "device_id": "MSP001",         # ← Device ID di default
    "sea_level_altitude": "122.00", # ← Altitudine di default
    # ... altri valori
}
```

### Cambiare Testi di Help

```python
HELP_TEXTS = {
    "ssid": "Il tuo nuovo testo di help personalizzato",
    # ... altri help text
}
```

### Cambiare Repository GitHub

```python
GITHUB_REPO = "TuoUsername/tuo-repo"
```

Salva il file e riavvia la GUI!

---

## 🧪 Test del Sistema

Esegui lo script di test per verificare che tutto funzioni:

```bash
cd config_generator
source venv/bin/activate
python test_modules.py
```

Output atteso:
```
🎯 Test superati: 5/5
🎉 Tutti i test sono passati!
```

---

## 📝 Esempi di Configurazione

### Configurazione Base
- **SSID**: MyWiFiNetwork
- **Password**: ********
- **Device ID**: MSP001
- **WiFi Power**: 17 dBm
- **Upload Server**: https://api.example.com/upload

### Configurazione con Modem
1. Abilita **"Use Modem"**
2. Inserisci **"Modem APN"**: Es. `mobile.vodafone.it`

### Configurazione Sensori
- **Gas Sensor Type**: MICS6814 o MICS4514
- **O3 Zero Value**: -1 (auto calibration)
- **Average Measurements**: 30 (default)

---

## ⚠️ Risoluzione Problemi

### Porta COM non trovata

**macOS**:
```bash
ls /dev/tty.*
# Cerca /dev/cu.usbserial-* o /dev/cu.SLAB_USBtoUART
```

**Linux**:
```bash
ls /dev/ttyUSB*
# Aggiungi permessi: sudo chmod 666 /dev/ttyUSB0
```

**Windows**:
- Apri **Gestione Dispositivi**
- Cerca **Porte COM**
- Installa driver CH340 o CP2102 se necessario

### Errore durante Flash

1. **Disconnetti e riconnetti** l'ESP32
2. Prova a tenere premuto il bottone **BOOT** durante il flash
3. Clicca **"🗑️ Erase Flash"** prima di flashare
4. Riduci la velocità baud (modifica `config_data.py`: `FLASH_BAUDRATE = 115200`)

### GUI non si avvia

**macOS/Linux**:
```bash
# Reinstalla dipendenze
source venv/bin/activate
pip install --force-reinstall -r requirements.txt
```

**Ubuntu/Debian** (se manca tkinter):
```bash
sudo apt-get install python3-tk
```

---

## 📂 File Importanti

| File | Descrizione | Modifica? |
|------|-------------|-----------|
| `config_data.py` | ⚙️ Configurazione (valori, help, costanti) | ✅ **SÌ** |
| `config_generator_gui.py` | 🖥️ GUI principale | ❌ No (a meno che tu voglia aggiungere funzionalità) |
| `esp32_utils.py` | 🔧 Utility ESP32 | ❌ No |
| `github_utils.py` | 📥 Utility GitHub | ❌ No |
| `run_gui.sh` | 🚀 Launcher macOS/Linux | ❌ No |
| `test_modules.py` | 🧪 Script di test | ❌ No |

---

## 💡 Tips & Tricks

### 1. Salva Template di Configurazione
- Crea diverse configurazioni per diversi setup
- Salva come `config_office.json`, `config_outdoor.json`, etc.
- Usa **"Load Config"** per caricarle velocemente

### 2. Flash Multipli
- Tieni il firmware scaricato in `~/Downloads/msp-firmware/`
- Usa **"📂 Select Local .bin File"** per flash veloci
- Non serve riscaricarlo ogni volta

### 3. Backup Prima di Flash
- Salva sempre la configurazione corrente
- Annota i valori di calibrazione MICS
- Tieni una copia del firmware funzionante

### 4. Verifica Flash
Dopo il flash, apri il **Serial Monitor** per verificare:
```bash
# macOS/Linux
screen /dev/cu.usbserial-14230 115200

# Windows (usa PuTTY o Arduino IDE Serial Monitor)
```

---

## 🆘 Supporto

- **GitHub Issues**: https://github.com/A-A-Milano-Smart-Park/msp-firmware/issues
- **Test Suite**: `python test_modules.py`
- **Documentazione completa**: [README.md](README.md)

---

## ✅ Checklist Flash Completo

- [ ] ESP32 collegato via USB
- [ ] Porta COM rilevata e selezionata
- [ ] Chip info rilevato correttamente
- [ ] Firmware scaricato o selezionato
- [ ] Flash completato con successo
- [ ] LED ESP32 lampeggia (boot corretto)
- [ ] Serial Monitor mostra output (opzionale)

---

**Buon lavoro! 🚀**
