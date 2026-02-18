#!/bin/bash
# Script di avvio per la GUI Config Generator (macOS/Linux)

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# Controlla se esiste l'ambiente virtuale
if [ ! -d "venv" ]; then
    echo "Ambiente virtuale non trovato. Creazione in corso..."
    python3 -m venv venv

    if [ $? -ne 0 ]; then
        echo "Errore: impossibile creare l'ambiente virtuale"
        echo "Assicurati di avere Python 3 installato: python3 --version"
        exit 1
    fi

    echo "Ambiente virtuale creato con successo!"
fi

# Attiva l'ambiente virtuale
source venv/bin/activate

if [ $? -ne 0 ]; then
    echo "Errore: impossibile attivare l'ambiente virtuale"
    exit 1
fi

# Installa/aggiorna le dipendenze
echo "Controllo dipendenze..."
pip install -q --upgrade pip
pip install -q -r requirements.txt

if [ $? -ne 0 ]; then
    echo "Errore: impossibile installare le dipendenze"
    exit 1
fi

# Avvia la GUI
echo "Avvio Config Generator GUI..."
python config_generator_gui.py

# Disattiva l'ambiente virtuale
deactivate
