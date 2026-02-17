# Guida alla Personalizzazione

Questa guida ti mostra come personalizzare facilmente l'applicazione modificando il file [config_data.py](config_data.py).

## 📝 Struttura del File config_data.py

Il file `config_data.py` contiene **TUTTE** le configurazioni centralizzate:

```
config_data.py
├── DEFAULT_VALUES          # Valori di default per tutti i campi
├── WIFI_POWER_VALUES      # Valori consentiti per WiFi Power
├── AVERAGE_MEASUREMENTS   # Valori consentiti per Average Measurements
├── GAS_SENSOR_TYPES       # Tipi di sensori gas
├── HELP_TEXTS             # Testi di aiuto per la GUI
├── JSON_HELP_TEXTS        # Testi di aiuto nel file JSON generato
└── GITHUB_REPO            # Repository GitHub per download firmware
```

## ✏️ Esempi di Personalizzazione

### 1. Cambiare i Valori di Default

**Scenario**: Vuoi che il WiFi Power di default sia 15 invece di 17

**File**: `config_data.py`
**Linea**: ~13

```python
DEFAULT_VALUES = {
    # ...
    "wifi_power": "15",  # Cambiato da "17" a "15"
    # ...
}
```

### 2. Modificare i Testi di Help

**Scenario**: Vuoi un testo di aiuto più dettagliato per il campo SSID

**File**: `config_data.py`
**Linea**: ~53

```python
HELP_TEXTS = {
    "ssid": """Nome della rete WiFi a cui il dispositivo si connetterà.

    IMPORTANTE:
    - La rete deve essere a 2.4GHz (NON 5GHz)
    - Evita caratteri speciali nel nome della rete
    - Lunghezza massima: 32 caratteri

    Esempio: MiaReteWiFi""",
    # ...
}
```

### 3. Aggiungere Nuovi Valori Consentiti

**Scenario**: Vuoi aggiungere un nuovo valore per WiFi Power

**File**: `config_data.py`
**Linea**: ~46

```python
WIFI_POWER_VALUES = [
    "-1", "2", "5", "7", "8.5", "11", "13", "15", "17", "18.5", "19", "19.5",
    "20"  # NUOVO VALORE AGGIUNTO
]
```

### 4. Cambiare Repository GitHub

**Scenario**: Vuoi scaricare firmware da un repository diverso

**File**: `config_data.py`
**Linea**: ~70

```python
GITHUB_REPO = "tuo-username/tuo-repository"  # Cambiato
```

### 5. Modificare Valori di Calibrazione di Default

**Scenario**: Hai nuovi valori standard per i sensori MICS

**File**: `config_data.py`
**Linea**: ~20

```python
DEFAULT_VALUES = {
    # ...
    # MICS Calibration Values
    "mics_cal_red": "1000",  # Cambiato da "955"
    "mics_cal_ox": "950",    # Cambiato da "900"
    "mics_cal_nh3": "200",   # Cambiato da "163"
    # ...
}
```

### 6. Cambiare Altitudine di Default

**Scenario**: I tuoi dispositivi sono installati a Roma invece che Milano

**File**: `config_data.py`
**Linea**: ~19

```python
DEFAULT_VALUES = {
    # ...
    "sea_level_altitude": "21.00",  # Altitudine media di Roma
    # ...
}
```

E aggiorna anche l'help text:

```python
HELP_TEXTS = {
    # ...
    "sea_level_altitude": "Altitudine sul livello del mare della posizione del dispositivo in metri\n\nQuesto valore è necessario per la calibrazione corretta della pressione atmosferica\n\nEsempio: 21.0 metri è l'altitudine media di Roma, Italia",
    # ...
}
```

## 🔧 Modifiche Avanzate

### Aggiungere un Nuovo Tipo di Sensore

**File**: `config_data.py`
**Linea**: ~50

```python
GAS_SENSOR_TYPES = {
    "MICS6814": 0,
    "MICS4514 (DFRobot SEN0377)": 1,
    "NUOVO_SENSORE": 2,  # AGGIUNTO
}
```

### Modificare Parametri di Flash ESP32

**File**: `config_data.py`
**Linea**: ~80

```python
# Aumenta la velocità di flash (se il tuo ESP32 lo supporta)
FLASH_BAUDRATE = 1500000  # Cambiato da 921600

# Cambia il flash mode
FLASH_MODE = "qio"  # Cambiato da "dio"
```

## 🚨 Attenzione

### NON Modificare

❌ **NON modificare** il file `config_generator_gui.py` a meno che tu non voglia cambiare la logica dell'interfaccia

❌ **NON cambiare** i nomi delle chiavi in `DEFAULT_VALUES` (solo i valori)

### Esempio SBAGLIATO:
```python
DEFAULT_VALUES = {
    "ssid_wifi": "",  # ❌ SBAGLIATO! Non cambiare il nome della chiave
}
```

### Esempio CORRETTO:
```python
DEFAULT_VALUES = {
    "ssid": "MiaReteDefault",  # ✅ CORRETTO! Cambia solo il valore
}
```

## ✅ Testing delle Modifiche

Dopo aver fatto modifiche a `config_data.py`:

1. Salva il file
2. Riavvia l'applicazione:
   ```bash
   ./run_gui.sh  # macOS/Linux
   run_gui.bat   # Windows
   ```
3. Verifica che le modifiche siano visibili nella GUI

## 📋 Checklist Modifiche

Prima di salvare le modifiche, verifica:

- [ ] Hai modificato solo i **valori**, non i **nomi** delle chiavi
- [ ] Le stringhe sono racchiuse tra virgolette (`"` o `'`)
- [ ] I numeri NON hanno virgolette (es: `17` non `"17"`)
- [ ] I booleani sono `True` o `False` (maiuscolo)
- [ ] Hai aggiornato HELP_TEXTS se hai cambiato DEFAULT_VALUES
- [ ] Il file è salvato con encoding UTF-8
- [ ] Non ci sono errori di sintassi Python

## 🐛 Risoluzione Problemi

### Errore: "SyntaxError" all'avvio

**Causa**: Errore di sintassi in `config_data.py`

**Soluzione**:
1. Controlla che tutte le stringhe siano chiuse correttamente
2. Verifica che le virgole siano al posto giusto
3. Assicurati che le parentesi quadre/graffe siano bilanciate

### Errore: "KeyError"

**Causa**: Hai cambiato il nome di una chiave in `DEFAULT_VALUES`

**Soluzione**:
1. Ripristina il nome originale della chiave
2. Modifica solo il valore associato

### I cambiamenti non si vedono

**Causa**: L'applicazione usa ancora la versione in cache

**Soluzione**:
1. Chiudi completamente l'applicazione
2. Riavviala con `./run_gui.sh` o `run_gui.bat`

## 📞 Supporto

Se hai dubbi sulle personalizzazioni, consulta:
- [README.md](README.md) - Documentazione principale
- [config_data.py](config_data.py) - Il file stesso (ben commentato)
- GitHub Issues del progetto

## 💡 Suggerimenti

1. **Fai un backup**: Prima di modifiche importanti, copia `config_data.py` in `config_data.py.backup`

2. **Modifiche incrementali**: Fai una modifica alla volta e testa

3. **Commenta le modifiche**: Aggiungi commenti per ricordare le tue personalizzazioni
   ```python
   "wifi_power": "15",  # Modificato il 2024-01-12 per dispositivi outdoor
   ```

4. **Versiona**: Se usi git, committa le modifiche con messaggi chiari
   ```bash
   git add config_data.py
   git commit -m "Update default WiFi power to 15dBm"
   ```
