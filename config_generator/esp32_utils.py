"""
ESP32 utilities module
Funzioni per interagire con ESP32: rilevamento porte, flash size, flashing
"""

import serial.tools.list_ports
import subprocess
import sys
import os
from typing import List, Tuple, Optional


def get_available_ports() -> List[Tuple[str, str]]:
    """
    Ottiene la lista delle porte seriali disponibili

    Returns:
        Lista di tuple (porta, descrizione)
    """
    ports = serial.tools.list_ports.comports()
    available_ports = []

    for port in ports:
        # Filtra porte che potrebbero essere ESP32
        description = f"{port.device} - {port.description}"
        if port.manufacturer:
            description += f" ({port.manufacturer})"
        available_ports.append((port.device, description))

    return available_ports


def get_chip_info_verbose(port: str) -> Optional[dict]:
    """
    Ottiene informazioni sul chip con metodo più verboso per debugging

    Args:
        port: Porta seriale del dispositivo

    Returns:
        Dizionario con informazioni e output completo
    """
    try:
        # Prova prima con read_mac per connessione di base
        result = subprocess.run(
            [sys.executable, "-m", "esptool", "--port", port, "read_mac"],
            capture_output=True,
            text=True,
            timeout=30
        )

        full_output = result.stdout + "\n" + result.stderr
        info = {
            'chip': 'ESP32 (generico)',
            'mac': 'Unknown',
            'flash_size': '4MB (stimato)',
            'debug_output': full_output
        }

        # Estrai MAC address
        for line in full_output.split('\n'):
            if 'MAC:' in line:
                mac = line.split('MAC:')[-1].strip()
                info['mac'] = mac
            elif 'Chip is' in line:
                chip_type = line.split('Chip is')[-1].strip().split('(')[0].strip()
                info['chip'] = chip_type

        # Ora prova flash_id per dimensione flash
        result2 = subprocess.run(
            [sys.executable, "-m", "esptool", "--port", port, "flash_id"],
            capture_output=True,
            text=True,
            timeout=30
        )

        flash_output = result2.stdout + "\n" + result2.stderr
        info['debug_output'] += "\n\n--- FLASH_ID ---\n" + flash_output

        for line in flash_output.split('\n'):
            if 'Detected flash size:' in line:
                flash_size = line.split(':')[-1].strip()
                info['flash_size'] = flash_size
            elif 'Chip is' in line and info['chip'] == 'ESP32 (generico)':
                chip_type = line.split('Chip is')[-1].strip().split('(')[0].strip()
                info['chip'] = chip_type

        return info

    except Exception as e:
        return {
            'chip': 'ESP32 (non rilevato)',
            'mac': 'Unknown',
            'flash_size': '4MB (default)',
            'error': str(e)
        }


def detect_flash_size(port: str) -> Optional[str]:
    """
    Rileva automaticamente la dimensione della flash dell'ESP32

    Args:
        port: Porta seriale del dispositivo

    Returns:
        Dimensione della flash (es: "4MB") o None se non rilevata
    """
    try:
        # Usa esptool per leggere le informazioni del chip
        result = subprocess.run(
            [sys.executable, "-m", "esptool", "--port", port, "flash_id"],
            capture_output=True,
            text=True,
            timeout=30
        )

        if result.returncode != 0:
            return None

        # Parse dell'output per trovare la dimensione della flash
        output = result.stdout
        for line in output.split('\n'):
            if 'Detected flash size:' in line:
                # Estrai la dimensione (es: "4MB")
                size = line.split(':')[-1].strip()
                return size

        return None
    except Exception as e:
        print(f"Errore nel rilevamento della flash size: {e}")
        return None


def get_chip_info(port: str) -> Optional[dict]:
    """
    Ottiene informazioni dettagliate sul chip ESP32

    Args:
        port: Porta seriale del dispositivo

    Returns:
        Dizionario con informazioni sul chip o None
    """
    try:
        # Usa flash_id che fornisce più informazioni
        result = subprocess.run(
            [sys.executable, "-m", "esptool", "--port", port, "flash_id"],
            capture_output=True,
            text=True,
            timeout=30
        )

        if result.returncode != 0:
            return None

        output = result.stdout + "\n" + result.stderr
        info = {
            'chip': 'Unknown',
            'mac': 'Unknown',
            'flash_size': 'Unknown'
        }

        # Parse output per trovare informazioni
        for line in output.split('\n'):
            # Rileva tipo di chip
            if 'Chip is' in line:
                chip_type = line.split('Chip is')[-1].strip()
                # Rimuovi parentesi e info extra
                chip_type = chip_type.split('(')[0].strip()
                info['chip'] = chip_type

            # Rileva MAC address
            elif 'MAC:' in line:
                mac = line.split('MAC:')[-1].strip()
                info['mac'] = mac

            # Rileva flash size
            elif 'Detected flash size:' in line:
                flash_size = line.split(':')[-1].strip()
                info['flash_size'] = flash_size

            # Alternativa per il rilevamento del chip
            elif 'Detecting chip type' in line:
                if 'ESP32' in line:
                    info['chip'] = 'ESP32'

            # Rileva chip type dall'header
            elif 'Serial port' in line:
                # Continua a cercare nelle righe successive
                pass

        # Se non abbiamo trovato il chip, prova a indovinare dalla presenza di ESP32
        if info['chip'] == 'Unknown' and 'esp32' in output.lower():
            info['chip'] = 'ESP32'

        # Se non abbiamo la flash size, usa un default
        if info['flash_size'] == 'Unknown':
            # Prova a cercare nell'output per indizi
            if '4MB' in output or '4 MB' in output:
                info['flash_size'] = '4MB'
            elif '2MB' in output or '2 MB' in output:
                info['flash_size'] = '2MB'
            elif '8MB' in output or '8 MB' in output:
                info['flash_size'] = '8MB'
            else:
                info['flash_size'] = '4MB (stimato)'

        return info
    except Exception as e:
        print(f"Errore nel recupero info chip: {e}")
        return None


def flash_firmware_package(port: str, firmware_files: dict, flash_size: str = "4MB",
                          baudrate: int = 921600, progress_callback=None) -> Tuple[bool, str]:
    """
    Flasha il pacchetto firmware completo sull'ESP32 (bootloader + partitions + app)

    Args:
        port: Porta seriale del dispositivo
        firmware_files: Dizionario con paths dei file del firmware:
            {
                'bootloader': path_bootloader,
                'partitions': path_partitions,
                'boot_app0': path_boot_app0,
                'firmware': path_firmware
            }
        flash_size: Dimensione della flash (es: "4MB")
        baudrate: Velocità di comunicazione
        progress_callback: Funzione callback per gli aggiornamenti di progresso

    Returns:
        Tuple (success: bool, message: str)
    """
    try:
        # Verifica che tutti i file esistano
        required_files = ['bootloader', 'partitions', 'boot_app0', 'firmware']
        for file_key in required_files:
            if file_key not in firmware_files or not firmware_files[file_key]:
                return False, f"File mancante: {file_key}"
            if not os.path.exists(firmware_files[file_key]):
                return False, f"File non trovato: {firmware_files[file_key]}"

        # Indirizzi di flash per ESP32
        flash_addresses = {
            'bootloader': '0x1000',
            'partitions': '0x8000',
            'boot_app0': '0xe000',
            'firmware': '0x10000'
        }

        # Comando esptool per il flash completo
        cmd = [
            sys.executable, "-m", "esptool",
            "--port", port,
            "--baud", str(baudrate),
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash",
            "-z",
            "--flash_mode", "dio",
            "--flash_freq", "80m",
            "--flash_size", flash_size
        ]

        # Aggiungi ogni file con il suo indirizzo
        for file_key in required_files:
            cmd.append(flash_addresses[file_key])
            cmd.append(firmware_files[file_key])

        if progress_callback:
            progress_callback("Connessione all'ESP32...")
            progress_callback(f"Flash size: {flash_size}")
            progress_callback("Scrittura: bootloader + partitions + boot_app0 + firmware")

        # Esegui il flash
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1
        )

        output_lines = []
        for line in iter(process.stdout.readline, ''):
            if line:
                output_lines.append(line.strip())
                if progress_callback:
                    # Estrai la percentuale se presente
                    if '%' in line:
                        progress_callback(line.strip())
                    elif 'Connecting' in line:
                        progress_callback("Connessione in corso...")
                    elif 'Writing at' in line:
                        # Estrai l'indirizzo corrente
                        if '0x00001000' in line or 'bootloader' in line.lower():
                            progress_callback("Scrittura bootloader...")
                        elif '0x00008000' in line:
                            progress_callback("Scrittura partition table...")
                        elif '0x0000e000' in line:
                            progress_callback("Scrittura boot_app0...")
                        elif '0x00010000' in line:
                            progress_callback("Scrittura firmware principale...")
                        else:
                            progress_callback("Scrittura firmware...")
                    elif 'Hash' in line:
                        progress_callback("Verifica hash...")
                    elif 'Leaving' in line:
                        progress_callback("Riavvio dispositivo...")

        process.wait()

        if process.returncode == 0:
            return True, "Flash completato con successo! Bootloader, partitions e firmware installati."
        else:
            error_msg = "\n".join(output_lines[-10:])  # Ultime 10 righe
            return False, f"Errore durante il flash:\n{error_msg}"

    except subprocess.TimeoutExpired:
        return False, "Timeout durante il flash. Riprova."
    except Exception as e:
        return False, f"Errore durante il flash: {str(e)}"


def flash_firmware(port: str, firmware_path: str, flash_size: str = "4MB",
                  baudrate: int = 921600, progress_callback=None) -> Tuple[bool, str]:
    """
    DEPRECATED: Usa flash_firmware_package per un flash completo con bootloader

    Flasha solo il firmware principale sull'ESP32 (solo app, senza bootloader)
    Questa funzione è mantenuta per compatibilità ma NON dovrebbe essere usata
    per un flash completo dopo erase_flash.

    Args:
        port: Porta seriale del dispositivo
        firmware_path: Path del file .bin del firmware
        flash_size: Dimensione della flash (es: "4MB")
        baudrate: Velocità di comunicazione
        progress_callback: Funzione callback per gli aggiornamenti di progresso

    Returns:
        Tuple (success: bool, message: str)
    """
    try:
        if not os.path.exists(firmware_path):
            return False, f"File firmware non trovato: {firmware_path}"

        # Comando esptool per il flash
        cmd = [
            sys.executable, "-m", "esptool",
            "--port", port,
            "--baud", str(baudrate),
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash",
            "-z",
            "--flash_mode", "dio",
            "--flash_freq", "40m",
            "--flash_size", flash_size,
            "0x10000", firmware_path  # Indirizzo standard per l'app ESP32
        ]

        if progress_callback:
            progress_callback("Connessione all'ESP32...")

        # Esegui il flash
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1
        )

        output_lines = []
        for line in iter(process.stdout.readline, ''):
            if line:
                output_lines.append(line.strip())
                if progress_callback:
                    # Estrai la percentuale se presente
                    if '%' in line:
                        progress_callback(line.strip())
                    elif 'Connecting' in line:
                        progress_callback("Connessione in corso...")
                    elif 'Writing' in line:
                        progress_callback("Scrittura firmware...")
                    elif 'Hash' in line:
                        progress_callback("Verifica hash...")

        process.wait()

        if process.returncode == 0:
            return True, "Flash completato con successo!"
        else:
            error_msg = "\n".join(output_lines[-10:])  # Ultime 10 righe
            return False, f"Errore durante il flash:\n{error_msg}"

    except subprocess.TimeoutExpired:
        return False, "Timeout durante il flash. Riprova."
    except Exception as e:
        return False, f"Errore durante il flash: {str(e)}"


def erase_flash(port: str, progress_callback=None) -> Tuple[bool, str]:
    """
    Cancella completamente la flash dell'ESP32

    Args:
        port: Porta seriale del dispositivo
        progress_callback: Funzione callback per gli aggiornamenti

    Returns:
        Tuple (success: bool, message: str)
    """
    try:
        if progress_callback:
            progress_callback("Cancellazione flash in corso...")

        result = subprocess.run(
            [sys.executable, "-m", "esptool", "--port", port, "erase_flash"],
            capture_output=True,
            text=True,
            timeout=60
        )

        if result.returncode == 0:
            return True, "Flash cancellata con successo"
        else:
            return False, f"Errore durante la cancellazione: {result.stderr}"

    except Exception as e:
        return False, f"Errore: {str(e)}"
