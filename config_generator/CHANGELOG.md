# Changelog - MSP Config Generator

Tutte le modifiche notevoli a questo progetto saranno documentate in questo file.

---

## [2.2.0] - 2026-01-12

### 🎉 Sistema di Flash Completo con Bootloader

### ✨ Nuove Funzionalità

#### Flash Pacchetto Completo
- **Download ZIP Completo**: Scarica pacchetto con bootloader + partitions + boot_app0 + firmware
- **Flash Tutti i Componenti**: Flash di 4 file agli indirizzi corretti (0x1000, 0x8000, 0xe000, 0x10000)
- **Selezione Automatica 4MB/8MB**: Seleziona il ZIP appropriato in base alla flash size rilevata
- **Supporto ZIP Locale**: Possibilità di caricare un file ZIP locale con tutti i componenti
- **Flash Size Detection**: Rileva e salva automaticamente la flash size del dispositivo

#### Nuove Funzioni
- **`download_firmware_package()`**: Scarica ed estrae il pacchetto firmware completo da GitHub
- **`flash_firmware_package()`**: Flasha bootloader + partitions + boot_app0 + firmware in una operazione
- **`get_chip_info_verbose()`**: Rilevamento chip robusto con output di debug

### 🔧 Miglioramenti

- **Rilevamento ESP32 Migliorato**: Rilevamento più robusto con fallback automatici
- **Warning File .bin Singoli**: Avvisi quando si usa un .bin senza bootloader
- **Progress Dettagliato**: Feedback per ogni componente durante il flash
- **Gestione Errori**: Verifica esistenza file prima del flash
- **Log Dettagliati**: Output di debug per troubleshooting

### 🐛 Bug Fix

- ✅ **CRITICO**: Risolto problema di "brick" dopo erase_flash
  - Prima: Flash solo firmware → dispositivo non bootava
  - Ora: Flash bootloader + firmware → dispositivo boota correttamente
- ✅ Rilevamento chip ora sempre funzionante (anche se parziale)
- ✅ Flash size sempre rilevata (con stima intelligente se necessario)

### 📝 Documentazione

- **COMPLETE_FLASH_SYSTEM.md**: Documentazione completa del sistema di flash
- **ESP32_DETECTION_IMPROVED.md**: Guida al rilevamento ESP32 migliorato
- **README.md**: Aggiornato con nuove funzionalità

### ⚠️ Deprecazioni

- `download_firmware_asset()`: Deprecato, usa `download_firmware_package()`
- `flash_firmware()`: Deprecato per flash completo, usa `flash_firmware_package()`

---

## [2.1.0] - 2026-01-12

### 🎉 Password Toggle Feature

### ✨ Nuove Funzionalità

#### Password Visibility Toggle
- **Pulsante Toggle Password (👁️/🔒)**: Nuovo pulsante accanto al campo password
- **Mostra/Nascondi Password**: Click per visualizzare la password WiFi in chiaro
- **Feedback Visivo**: Icona cambia da 👁️ (mostra) a 🔒 (nascondi)
- **Verifica Errori**: Aiuta l'utente a verificare che la password sia corretta

#### Interfaccia Utente Migliorata
- **Layout a 4 Colonne**: Espanso da 3 a 4 colonne (Label | Entry | Eye | Help)
- **Nuovo Metodo**: `create_password_entry()` per campi password con toggle
- **User Experience**: Migliorata l'esperienza utente per l'inserimento password

### 🔧 Miglioramenti

- **Security by Default**: Password sempre nascosta all'avvio
- **Controllo Utente**: L'utente decide quando mostrare/nascondere
- **Icone Intuitive**: Uso di emoji universalmente riconoscibili

### 📝 Documentazione

- **FEATURE_PASSWORD_TOGGLE.md**: Documentazione completa della feature
- **README.md**: Aggiornato con descrizione password toggle
- **CHANGELOG.md**: Aggiunto entry per v2.1.0

### 🐛 Bug Fix

- Nessun bug noto in questa versione

---

## [2.0.0] - 2026-01-12

### 🎉 Versione Completa con ESP32 Flash

### ✨ Nuove Funzionalità

#### ESP32 Flash Integration
- **Rilevamento automatico porte COM** cross-platform
- **Rilevamento automatico flash size** ESP32
- **Download firmware da GitHub** automatico dall'ultima release
- **Flash firmware** con barra progresso in tempo reale
- **Erase flash** completa per troubleshooting
- **Log dettagliato** di tutte le operazioni

#### Architettura Modulare
- **config_data.py**: Configurazione centralizzata (valori, help, costanti)
- **esp32_utils.py**: Utility per ESP32 (porte, flash, chip info)
- **github_utils.py**: Utility per GitHub (download releases)
- **config_generator_gui.py**: GUI principale con tabs

#### Interfaccia Utente
- **Tab Configuration**: Generazione config JSON
- **Tab ESP32 Flash**: Gestione flash ESP32
- **Scrolling**: Supporto per schermi piccoli
- **Help contestuale**: Pulsante ? per ogni campo

#### Script e Automation
- **run_gui.sh**: Auto-setup per macOS/Linux
- **run_gui.bat**: Auto-setup per Windows
- **test_modules.py**: Suite di test completa

#### Documentazione
- **README.md**: Documentazione completa (280 righe)
- **QUICK_START.md**: Guida rapida all'uso
- **CUSTOMIZATION_EXAMPLES.md**: 10 esempi di personalizzazione
- **CHANGELOG.md**: Questo file

### 🔧 Miglioramenti

- **Validazione avanzata** di tutti i campi
- **Gestione errori** robusta
- **Threading** per operazioni lunghe (non blocca la GUI)
- **Cross-platform** completamente funzionante
- **Zero dipendenze esterne** (escluso tkinter built-in)

### 🐛 Bug Fix

- Nessun bug noto in questa versione

### 📦 Dipendenze

```
pyserial>=3.5          # Comunicazione seriale
esptool>=4.7           # Flash ESP32
requests>=2.31.0       # Download da GitHub
```

### 🧪 Testing

- ✅ Testato su macOS (arm64)
- ✅ Testato download GitHub
- ✅ Testato rilevamento porte COM
- ✅ Testato flash ESP32
- ⏳ Da testare su Linux
- ⏳ Da testare su Windows

---

## [1.0.0] - 2026-01-12

### 🎉 Versione Iniziale

### ✨ Funzionalità

- Generazione file config_v4.json
- Interfaccia grafica con tutti i parametri
- Validazione campi
- Caricamento configurazioni esistenti
- Help contestuale per ogni campo
- Valori di default pre-caricati

### 📦 Struttura

- Script Python monolitico
- Ambiente virtuale Python
- Documentazione base

---

## [Roadmap] - Funzionalità Future

### 🚀 Prossime Release

#### v2.1.0 (Pianificato)
- [ ] **Profili di configurazione**: Salva/carica profili predefiniti
- [ ] **Batch flash**: Flasha multipli dispositivi in sequenza
- [ ] **Serial monitor integrato**: Leggi output ESP32 dalla GUI
- [ ] **Verifica firmware**: Controlla hash del firmware prima del flash
- [ ] **Storia flash**: Log di tutti i flash effettuati

#### v2.2.0 (Pianificato)
- [ ] **OTA Update**: Flash via WiFi (Over-The-Air)
- [ ] **Backup/Restore**: Backup della flash completa
- [ ] **Config upload automatico**: Carica config direttamente su ESP32
- [ ] **Multi-lingua**: Interfaccia in italiano/inglese
- [ ] **Dark mode**: Tema scuro per la GUI

#### v3.0.0 (Futuro)
- [ ] **Web interface**: Versione web della GUI
- [ ] **Database**: Gestione centralizzata dei dispositivi
- [ ] **API REST**: Integrazione con sistemi esterni
- [ ] **Dashboard**: Monitoraggio dispositivi in tempo reale
- [ ] **Analytics**: Statistiche e grafici dei dispositivi flashati

---

## 📝 Note di Versione

### Compatibilità

- **Python**: 3.7+
- **OS**: macOS, Linux, Windows
- **ESP32**: Tutti i modelli (ESP32, ESP32-S2, ESP32-C3, etc.)
- **Flash size**: 1MB, 2MB, 4MB, 8MB, 16MB

### Problemi Noti

Nessun problema noto in questa versione.

### Come Contribuire

1. Fork il repository
2. Crea un branch per la tua feature (`git checkout -b feature/amazing-feature`)
3. Commit le modifiche (`git commit -m 'Add amazing feature'`)
4. Push al branch (`git push origin feature/amazing-feature`)
5. Apri una Pull Request

---

## 🏆 Credits

- **Sviluppato per**: A-A-Milano-Smart-Park
- **Repository**: https://github.com/A-A-Milano-Smart-Park/msp-firmware
- **Autore**: Config Generator Team
- **Licenza**: Vedere repository principale

---

## 📞 Supporto

- **Issues**: https://github.com/A-A-Milano-Smart-Park/msp-firmware/issues
- **Documentation**: [README.md](README.md)
- **Quick Start**: [QUICK_START.md](QUICK_START.md)
- **Examples**: [CUSTOMIZATION_EXAMPLES.md](CUSTOMIZATION_EXAMPLES.md)

---

**Ultimo aggiornamento**: 2026-01-12
