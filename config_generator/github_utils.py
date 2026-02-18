"""
GitHub utilities module
Funzioni per interagire con GitHub releases
"""

import requests
import os
import zipfile
import platform
from typing import Optional, Tuple, Dict
from pathlib import Path


def get_latest_release_info(repo: str) -> Optional[dict]:
    """
    Ottiene informazioni sull'ultima release da GitHub

    Args:
        repo: Nome del repository nel formato "owner/repo"

    Returns:
        Dizionario con info sulla release o None
    """
    try:
        api_url = f"https://api.github.com/repos/{repo}/releases/latest"
        response = requests.get(api_url, timeout=10)

        if response.status_code == 200:
            return response.json()
        else:
            print(f"Errore API GitHub: {response.status_code}")
            return None

    except Exception as e:
        print(f"Errore nel recupero info release: {e}")
        return None


def download_firmware_package(release_info: dict, download_dir: str,
                             flash_size: str = "4MB",
                             progress_callback=None) -> Optional[dict]:
    """
    Scarica il pacchetto firmware completo (.zip) dalla release

    Args:
        release_info: Dizionario con info sulla release
        download_dir: Directory dove salvare il file
        flash_size: Dimensione flash ("4MB" o "8MB")
        progress_callback: Funzione callback per il progresso

    Returns:
        Dizionario con paths dei file estratti o None
    """
    try:
        assets = release_info.get('assets', [])

        # Determina quale ZIP scaricare basato su flash size e OS
        system = platform.system().lower()
        if system == "darwin":
            platform_suffix = "macos"
        elif system == "windows":
            platform_suffix = "win64"
        else:  # Linux
            platform_suffix = "macos"  # Usa macOS per Linux (compatibile)

        # Normalizza flash size
        flash_size_str = flash_size.upper().replace("MB", "MB").replace(" ", "")
        if "4" in flash_size_str:
            flash_size_str = "4MB"
        elif "8" in flash_size_str:
            flash_size_str = "8MB"
        else:
            flash_size_str = "4MB"  # Default

        # Cerca il file ZIP appropriato
        zip_asset = None
        version = get_release_version(release_info)

        # Cerca pattern: msp-firmware-FLASH{size}-v{version}-{platform}.zip
        search_pattern = f"FLASH{flash_size_str}"

        for asset in assets:
            filename = asset['name']
            if filename.endswith('.zip') and search_pattern in filename and platform_suffix in filename:
                zip_asset = asset
                break

        if not zip_asset:
            # Fallback: prova a cercare qualsiasi ZIP con la flash size corretta
            for asset in assets:
                filename = asset['name']
                if filename.endswith('.zip') and search_pattern in filename:
                    zip_asset = asset
                    break

        if not zip_asset:
            print(f"Nessun file ZIP trovato per {flash_size_str}")
            return None

        download_url = zip_asset['browser_download_url']
        filename = zip_asset['name']
        file_size = zip_asset['size']

        # Path completo del file
        os.makedirs(download_dir, exist_ok=True)
        zip_filepath = os.path.join(download_dir, filename)

        if progress_callback:
            progress_callback(f"Download di {filename}...")

        # Download con barra di progresso
        response = requests.get(download_url, stream=True, timeout=30)

        if response.status_code != 200:
            return None

        downloaded = 0
        chunk_size = 8192

        with open(zip_filepath, 'wb') as f:
            for chunk in response.iter_content(chunk_size=chunk_size):
                if chunk:
                    f.write(chunk)
                    downloaded += len(chunk)

                    if progress_callback and file_size > 0:
                        percentage = (downloaded / file_size) * 100
                        progress_callback(f"Download: {percentage:.1f}%")

        if progress_callback:
            progress_callback(f"Estrazione dei file...")

        # Estrai il ZIP
        extract_dir = os.path.join(download_dir, f"firmware_{version}_{flash_size_str}")
        os.makedirs(extract_dir, exist_ok=True)

        with zipfile.ZipFile(zip_filepath, 'r') as zip_ref:
            zip_ref.extractall(extract_dir)

        if progress_callback:
            progress_callback(f"Estrazione completata!")

        # Trova i file necessari nel directory estratto
        firmware_files = {
            'bootloader': None,
            'partitions': None,
            'boot_app0': None,
            'firmware': None,
            'zip_path': zip_filepath,
            'extract_dir': extract_dir
        }

        for root, dirs, files in os.walk(extract_dir):
            for file in files:
                filepath = os.path.join(root, file)
                if 'bootloader' in file and file.endswith('.bin'):
                    firmware_files['bootloader'] = filepath
                elif 'partitions' in file and file.endswith('.bin'):
                    firmware_files['partitions'] = filepath
                elif 'boot_app0' in file and file.endswith('.bin'):
                    firmware_files['boot_app0'] = filepath
                elif file == 'msp-firmware.ino.bin':
                    firmware_files['firmware'] = filepath

        # Verifica che tutti i file siano stati trovati
        missing = [k for k, v in firmware_files.items() if v is None and k not in ['zip_path', 'extract_dir']]
        if missing:
            print(f"File mancanti nell'archivio: {missing}")
            return None

        return firmware_files

    except Exception as e:
        print(f"Errore nel download/estrazione: {e}")
        return None


# Mantieni la funzione legacy per compatibilità
def download_firmware_asset(release_info: dict, download_dir: str,
                           progress_callback=None) -> Optional[str]:
    """
    DEPRECATED: Usa download_firmware_package invece

    Questa funzione scarica solo il .bin ed è mantenuta per compatibilità.
    Per un flash completo usa download_firmware_package che scarica tutto il pacchetto.
    """
    package = download_firmware_package(release_info, download_dir, "4MB", progress_callback)
    if package:
        return package.get('firmware')
    return None


def get_release_version(release_info: dict) -> str:
    """
    Estrae la versione dalla release

    Args:
        release_info: Dizionario con info sulla release

    Returns:
        Stringa con la versione
    """
    return release_info.get('tag_name', 'Unknown')


def get_release_notes(release_info: dict) -> str:
    """
    Estrae le note di rilascio

    Args:
        release_info: Dizionario con info sulla release

    Returns:
        Stringa con le note di rilascio
    """
    return release_info.get('body', 'Nessuna descrizione disponibile')


def check_for_updates(repo: str, current_version: Optional[str] = None) -> Tuple[bool, Optional[dict]]:
    """
    Controlla se c'è una nuova versione disponibile

    Args:
        repo: Nome del repository
        current_version: Versione corrente (opzionale)

    Returns:
        Tuple (has_update: bool, release_info: dict)
    """
    release_info = get_latest_release_info(repo)

    if not release_info:
        return False, None

    if current_version:
        latest_version = get_release_version(release_info)
        has_update = latest_version != current_version
        return has_update, release_info
    else:
        return True, release_info
