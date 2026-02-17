@echo off
REM Script di avvio per la GUI Config Generator (Windows)

cd /d "%~dp0"

REM Controlla se esiste l'ambiente virtuale
if not exist "venv\" (
    echo Ambiente virtuale non trovato. Creazione in corso...
    python -m venv venv

    if errorlevel 1 (
        echo Errore: impossibile creare l'ambiente virtuale
        echo Assicurati di avere Python 3 installato: python --version
        pause
        exit /b 1
    )

    echo Ambiente virtuale creato con successo!
)

REM Attiva l'ambiente virtuale
call venv\Scripts\activate.bat

if errorlevel 1 (
    echo Errore: impossibile attivare l'ambiente virtuale
    pause
    exit /b 1
)

REM Installa/aggiorna le dipendenze
echo Controllo dipendenze...
python -m pip install -q --upgrade pip
pip install -q -r requirements.txt

if errorlevel 1 (
    echo Errore: impossibile installare le dipendenze
    pause
    exit /b 1
)

REM Avvia la GUI
echo Avvio Config Generator GUI...
python config_generator_gui.py

REM Disattiva l'ambiente virtuale
deactivate

pause
