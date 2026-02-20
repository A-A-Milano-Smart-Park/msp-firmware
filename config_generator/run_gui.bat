@echo off
REM Config Generator GUI - Script di avvio automatizzato (Windows)
setlocal EnableDelayedExpansion

cd /d "%~dp0"

echo ============================================================
echo  MSP Firmware - Config Generator GUI
echo ============================================================
echo.

REM ============================================================
REM STEP 1: Trova o installa Python
REM ============================================================

set PYTHON_CMD=
set PYTHON_VERSION_REQUIRED=3.8
set PYTHON_INSTALL_VERSION=3.11.9
set PYTHON_INSTALL_URL=https://www.python.org/ftp/python/%PYTHON_INSTALL_VERSION%/python-%PYTHON_INSTALL_VERSION%-amd64.exe

REM Prova 'python'
python --version >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=2" %%v in ('python --version 2^>^&1') do set PY_VER=%%v
    for /f "tokens=1,2 delims=." %%a in ("!PY_VER!") do (
        if %%a GEQ 3 (
            set PYTHON_CMD=python
        )
    )
)

REM Prova 'python3'
if not defined PYTHON_CMD (
    python3 --version >nul 2>&1
    if not errorlevel 1 (
        set PYTHON_CMD=python3
    )
)

REM Cerca Python in percorsi comuni di installazione (necessario dopo un'installazione
REM nella stessa sessione, quando il PATH non e' ancora aggiornato)
if not defined PYTHON_CMD (
    for %%p in (
        "%LOCALAPPDATA%\Programs\Python\Python313\python.exe"
        "%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
        "%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
        "%LOCALAPPDATA%\Programs\Python\Python310\python.exe"
        "%ProgramFiles%\Python313\python.exe"
        "%ProgramFiles%\Python312\python.exe"
        "%ProgramFiles%\Python311\python.exe"
        "%ProgramFiles%\Python310\python.exe"
    ) do (
        if exist %%p (
            set PYTHON_CMD=%%~p
            goto :python_found
        )
    )
)

if defined PYTHON_CMD goto :python_found

REM ---- Python non trovato: installazione automatica ----
echo [!] Python non trovato. Avvio installazione automatica...
echo.

REM Prova con winget (disponibile su Windows 10 1809+)
winget --version >nul 2>&1
if not errorlevel 1 (
    echo [*] Installazione Python tramite winget...
    winget install -e --id Python.Python.3.11 --silent --accept-package-agreements --accept-source-agreements
    if not errorlevel 1 (
        echo [OK] Installazione winget completata.
        REM Cerca Python nei percorsi standard (il PATH non e' ancora aggiornato)
        for %%p in (
            "%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
            "%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
            "%LOCALAPPDATA%\Programs\Python\Python313\python.exe"
        ) do (
            if exist %%p (
                set PYTHON_CMD=%%~p
                goto :python_found
            )
        )
    )
)

REM Fallback: scarica l'installer da python.org tramite PowerShell
echo [*] Scaricamento Python %PYTHON_INSTALL_VERSION% da python.org...
set PYTHON_INSTALLER=%TEMP%\python_installer_%PYTHON_INSTALL_VERSION%.exe

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "try { Invoke-WebRequest -Uri '%PYTHON_INSTALL_URL%' -OutFile '%PYTHON_INSTALLER%' -UseBasicParsing; exit 0 } catch { Write-Host $_.Exception.Message; exit 1 }"

if not exist "%PYTHON_INSTALLER%" (
    echo.
    echo [ERRORE] Impossibile scaricare Python automaticamente.
    echo.
    echo Installa Python manualmente:
    echo   1. Vai su https://www.python.org/downloads/
    echo   2. Scarica Python 3.11 o superiore
    echo   3. Durante l'installazione, spunta "Add Python to PATH"
    echo   4. Riavvia questo script
    echo.
    pause
    exit /b 1
)

echo [*] Installazione Python %PYTHON_INSTALL_VERSION% in corso...
"%PYTHON_INSTALLER%" /quiet InstallAllUsers=0 PrependPath=1 Include_test=0 Include_doc=0
if errorlevel 1 (
    del "%PYTHON_INSTALLER%" >nul 2>&1
    echo.
    echo [ERRORE] Installazione Python fallita.
    echo Prova a installarlo manualmente da: https://www.python.org/downloads/
    echo.
    pause
    exit /b 1
)
del "%PYTHON_INSTALLER%" >nul 2>&1

REM Cerca Python dopo l'installazione
for %%p in (
    "%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
    "%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
    "%LOCALAPPDATA%\Programs\Python\Python313\python.exe"
) do (
    if exist %%p (
        set PYTHON_CMD=%%~p
        goto :python_found
    )
)

echo.
echo [ERRORE] Python e' stato installato ma non e' stato trovato.
echo Chiudi e riapri questo script.
echo.
pause
exit /b 1

:python_found
echo [OK] Python trovato: !PYTHON_CMD!
"!PYTHON_CMD!" --version
echo.

REM ============================================================
REM STEP 2: Crea l'ambiente virtuale (se non esiste)
REM ============================================================

if not exist "venv\" (
    echo [*] Creazione ambiente virtuale...
    "!PYTHON_CMD!" -m venv venv
    if errorlevel 1 (
        echo.
        echo [ERRORE] Impossibile creare l'ambiente virtuale.
        echo Assicurati che il modulo 'venv' sia disponibile.
        echo.
        pause
        exit /b 1
    )
    echo [OK] Ambiente virtuale creato.
    echo.
)

REM ============================================================
REM STEP 3: Attiva l'ambiente virtuale
REM ============================================================

call venv\Scripts\activate.bat
if errorlevel 1 (
    echo.
    echo [ERRORE] Impossibile attivare l'ambiente virtuale.
    echo Prova a eliminare la cartella 'venv' e riavviare lo script.
    echo.
    pause
    exit /b 1
)

REM ============================================================
REM STEP 4: Installa/aggiorna le dipendenze
REM ============================================================

echo [*] Aggiornamento pip...
python -m pip install -q --upgrade pip

echo [*] Installazione dipendenze da requirements.txt...
pip install -q -r requirements.txt
if errorlevel 1 (
    echo.
    echo [ERRORE] Impossibile installare le dipendenze.
    echo Controlla la connessione internet e riprova.
    echo.
    deactivate
    pause
    exit /b 1
)
echo [OK] Dipendenze installate.
echo.

REM ============================================================
REM STEP 5: Avvia la GUI
REM ============================================================

echo [*] Avvio Config Generator GUI...
echo.
python config_generator_gui.py

REM ============================================================
REM Pulizia
REM ============================================================

deactivate
echo.
echo Config Generator chiuso.
pause
