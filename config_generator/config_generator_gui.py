#!/usr/bin/env python3
"""
MSP Firmware Config Generator - Versione Modulare
A cross-platform GUI tool to generate config_v4.json files and flash ESP32
"""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog, scrolledtext
import json
import threading
from pathlib import Path

# Import moduli locali
from config_data import (
    DEFAULT_VALUES, WIFI_POWER_VALUES, AVERAGE_MEASUREMENTS_VALUES,
    GAS_SENSOR_TYPES, HELP_TEXTS, JSON_HELP_TEXTS, GITHUB_REPO
)
from esp32_utils import (
    get_available_ports, detect_flash_size, get_chip_info,
    get_chip_info_verbose, flash_firmware, flash_firmware_package, erase_flash
)
from github_utils import (
    get_latest_release_info, download_firmware_package,
    get_release_version, get_release_notes
)


class ConfigGeneratorGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("MSP Firmware Config Generator & Flasher")
        self.root.geometry("950x900")

        # Variables
        self.vars = {}
        self.firmware_path = None  # Legacy: path singolo file .bin
        self.firmware_files = None  # Nuovo: dizionario con tutti i file del package
        self.release_info = None
        self.detected_flash_size = "4MB"  # Flash size rilevata dal dispositivo

        # Create notebook (tabs)
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # Tab 1: Configuration
        self.config_tab = ttk.Frame(self.notebook)
        self.notebook.add(self.config_tab, text="📝 Configuration")

        # Tab 2: ESP32 Flash
        self.flash_tab = ttk.Frame(self.notebook)
        self.notebook.add(self.flash_tab, text="⚡ ESP32 Flash")

        # Create UI for both tabs
        self.create_config_tab()
        self.create_flash_tab()

    def create_config_tab(self):
        """Crea la tab di configurazione"""
        # Create main canvas and scrollbar
        main_frame = ttk.Frame(self.config_tab)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        canvas = tk.Canvas(main_frame)
        scrollbar = ttk.Scrollbar(main_frame, orient="vertical", command=canvas.yview)
        scrollable_frame = ttk.Frame(canvas)

        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )

        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        # Title
        title_label = ttk.Label(scrollable_frame, text="MSP Firmware Configuration",
                               font=("Arial", 14, "bold"))
        title_label.grid(row=0, column=0, columnspan=4, pady=10)

        row = 1

        # Network Settings
        row = self.create_section(scrollable_frame, row, "Network Settings")
        row = self.create_entry(scrollable_frame, row, "SSID", "ssid")
        row = self.create_password_entry(scrollable_frame, row, "Password", "password")
        row = self.create_combobox(scrollable_frame, row, "WiFi Power", "wifi_power",
                                   WIFI_POWER_VALUES)

        # Device Settings
        row = self.create_section(scrollable_frame, row, "Device Settings")
        row = self.create_entry(scrollable_frame, row, "Device ID", "device_id")
        row = self.create_entry(scrollable_frame, row, "Upload Server", "upload_server")

        # Sensor Settings
        row = self.create_section(scrollable_frame, row, "Sensor Settings")
        row = self.create_combobox(scrollable_frame, row, "Gas Sensor Type", "gas_sensor_type",
                                   list(GAS_SENSOR_TYPES.keys()))
        row = self.create_entry(scrollable_frame, row, "O3 Zero Value", "o3_zero_value")
        row = self.create_combobox(scrollable_frame, row, "Average Measurements", "average_measurements",
                                   AVERAGE_MEASUREMENTS_VALUES)
        row = self.create_entry(scrollable_frame, row, "Sea Level Altitude (m)", "sea_level_altitude")

        # MICS Calibration Values
        row = self.create_section(scrollable_frame, row, "MICS Calibration Values")
        row = self.create_entry(scrollable_frame, row, "RED Calibration", "mics_cal_red")
        row = self.create_entry(scrollable_frame, row, "OX Calibration", "mics_cal_ox")
        row = self.create_entry(scrollable_frame, row, "NH3 Calibration", "mics_cal_nh3")

        # MICS Measurement Offsets
        row = self.create_section(scrollable_frame, row, "MICS Measurement Offsets")
        row = self.create_entry(scrollable_frame, row, "RED Offset", "mics_offset_red")
        row = self.create_entry(scrollable_frame, row, "OX Offset", "mics_offset_ox")
        row = self.create_entry(scrollable_frame, row, "NH3 Offset", "mics_offset_nh3")

        # Compensation Factors
        row = self.create_section(scrollable_frame, row, "Compensation Factors")
        row = self.create_entry(scrollable_frame, row, "Humidity Compensation", "comp_h")
        row = self.create_entry(scrollable_frame, row, "Temperature Compensation", "comp_t")
        row = self.create_entry(scrollable_frame, row, "Pressure Compensation", "comp_p")

        # Modem Settings
        row = self.create_section(scrollable_frame, row, "Modem Settings (Optional)")
        row = self.create_checkbox(scrollable_frame, row, "Use Modem", "use_modem")
        row = self.create_entry(scrollable_frame, row, "Modem APN", "modem_apn")

        # Time Settings
        row = self.create_section(scrollable_frame, row, "Time Settings")
        row = self.create_entry(scrollable_frame, row, "NTP Server", "ntp_server")
        row = self.create_entry(scrollable_frame, row, "Timezone", "timezone")

        # Firmware Settings
        row = self.create_section(scrollable_frame, row, "Firmware Settings")
        row = self.create_checkbox(scrollable_frame, row, "Auto Firmware Upgrade", "fw_auto_upgrade")

        # Buttons
        button_frame = ttk.Frame(scrollable_frame)
        button_frame.grid(row=row, column=0, columnspan=4, pady=20)

        ttk.Button(button_frame, text="Generate Config", command=self.generate_config).pack(side="left", padx=5)
        ttk.Button(button_frame, text="Load Config", command=self.load_config).pack(side="left", padx=5)
        ttk.Button(button_frame, text="Reset to Defaults", command=self.set_defaults).pack(side="left", padx=5)

        # Set default values
        self.set_defaults()

    def create_flash_tab(self):
        """Crea la tab per il flash ESP32"""
        main_frame = ttk.Frame(self.flash_tab)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # Title
        title_label = ttk.Label(main_frame, text="ESP32 Firmware Flasher",
                               font=("Arial", 14, "bold"))
        title_label.pack(pady=10)

        # Frame per selezione porta
        port_frame = ttk.LabelFrame(main_frame, text="Selezione Dispositivo", padding=10)
        port_frame.pack(fill=tk.X, pady=5)

        ttk.Label(port_frame, text="Porta COM:").grid(row=0, column=0, sticky="w", padx=5, pady=5)

        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(port_frame, textvariable=self.port_var, width=50, state="readonly")
        self.port_combo.grid(row=0, column=1, padx=5, pady=5)

        ttk.Button(port_frame, text="🔄 Refresh", command=self.refresh_ports).grid(row=0, column=2, padx=5)
        ttk.Button(port_frame, text="🔍 Detect", command=self.detect_device).grid(row=0, column=3, padx=5)

        # Info dispositivo
        info_frame = ttk.LabelFrame(main_frame, text="Informazioni Dispositivo", padding=10)
        info_frame.pack(fill=tk.X, pady=5)

        self.chip_info_text = tk.Text(info_frame, height=4, width=80, state='disabled')
        self.chip_info_text.pack(fill=tk.X, padx=5, pady=5)

        # Frame firmware
        firmware_frame = ttk.LabelFrame(main_frame, text="Firmware", padding=10)
        firmware_frame.pack(fill=tk.X, pady=5)

        ttk.Button(firmware_frame, text="📥 Download Latest Release from GitHub",
                  command=self.download_latest_firmware, width=40).pack(pady=5)

        ttk.Label(firmware_frame, text="o").pack(pady=2)

        ttk.Button(firmware_frame, text="📂 Select Local .bin File",
                  command=self.select_local_firmware, width=40).pack(pady=5)

        self.firmware_label = ttk.Label(firmware_frame, text="Nessun firmware selezionato",
                                       foreground="gray")
        self.firmware_label.pack(pady=5)

        # Frame azioni
        action_frame = ttk.LabelFrame(main_frame, text="Azioni", padding=10)
        action_frame.pack(fill=tk.X, pady=5)

        btn_frame = ttk.Frame(action_frame)
        btn_frame.pack()

        self.flash_btn = ttk.Button(btn_frame, text="⚡ Flash Firmware",
                                    command=self.flash_device, state='disabled')
        self.flash_btn.pack(side=tk.LEFT, padx=5)

        ttk.Button(btn_frame, text="🗑️ Erase Flash", command=self.erase_device_flash).pack(side=tk.LEFT, padx=5)

        # Log area
        log_frame = ttk.LabelFrame(main_frame, text="Output Log", padding=5)
        log_frame.pack(fill=tk.BOTH, expand=True, pady=5)

        self.log_text = scrolledtext.ScrolledText(log_frame, height=15, width=80, state='disabled')
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # Refresh ports iniziale
        self.refresh_ports()

    def create_section(self, parent, row, title):
        """Create a section header"""
        separator = ttk.Separator(parent, orient="horizontal")
        separator.grid(row=row, column=0, columnspan=4, sticky="ew", pady=(15, 5))
        row += 1

        label = ttk.Label(parent, text=title, font=("Arial", 11, "bold"))
        label.grid(row=row, column=0, columnspan=4, sticky="w", pady=(0, 8))
        return row + 1

    def create_entry(self, parent, row, label_text, var_name, show=None):
        """Create a labeled entry with help text"""
        label = ttk.Label(parent, text=label_text + ":")
        label.grid(row=row, column=0, sticky="w", padx=5, pady=5)

        self.vars[var_name] = tk.StringVar()
        entry = ttk.Entry(parent, textvariable=self.vars[var_name], width=40, show=show)
        entry.grid(row=row, column=1, sticky="ew", padx=5, pady=5)

        help_text = HELP_TEXTS.get(var_name, "Nessun aiuto disponibile")
        help_btn = ttk.Button(parent, text="?", width=3,
                             command=lambda: messagebox.showinfo("Help", help_text))
        help_btn.grid(row=row, column=2, padx=5, pady=5)

        parent.columnconfigure(1, weight=1)
        return row + 1

    def create_password_entry(self, parent, row, label_text, var_name):
        """Create a password entry with show/hide toggle button"""
        label = ttk.Label(parent, text=label_text + ":")
        label.grid(row=row, column=0, sticky="w", padx=5, pady=5)

        self.vars[var_name] = tk.StringVar()
        entry = ttk.Entry(parent, textvariable=self.vars[var_name], width=40, show="*")
        entry.grid(row=row, column=1, sticky="ew", padx=5, pady=5)

        # Toggle button per mostrare/nascondere password
        def toggle_password():
            if entry.cget('show') == '*':
                entry.config(show='')
                toggle_btn.config(text="🔒")
            else:
                entry.config(show='*')
                toggle_btn.config(text="👁️")

        toggle_btn = ttk.Button(parent, text="👁️", width=3, command=toggle_password)
        toggle_btn.grid(row=row, column=2, padx=2, pady=5)

        help_text = HELP_TEXTS.get(var_name, "Nessun aiuto disponibile")
        help_btn = ttk.Button(parent, text="?", width=3,
                             command=lambda: messagebox.showinfo("Help", help_text))
        help_btn.grid(row=row, column=3, padx=5, pady=5)

        parent.columnconfigure(1, weight=1)
        return row + 1

    def create_combobox(self, parent, row, label_text, var_name, values):
        """Create a labeled combobox with help text"""
        label = ttk.Label(parent, text=label_text + ":")
        label.grid(row=row, column=0, sticky="w", padx=5, pady=5)

        self.vars[var_name] = tk.StringVar()
        combo = ttk.Combobox(parent, textvariable=self.vars[var_name], values=values,
                            width=37, state="readonly")
        combo.grid(row=row, column=1, sticky="ew", padx=5, pady=5)

        help_text = HELP_TEXTS.get(var_name, "Nessun aiuto disponibile")
        help_btn = ttk.Button(parent, text="?", width=3,
                             command=lambda: messagebox.showinfo("Help", help_text))
        help_btn.grid(row=row, column=2, padx=5, pady=5)

        parent.columnconfigure(1, weight=1)
        return row + 1

    def create_checkbox(self, parent, row, label_text, var_name):
        """Create a labeled checkbox with help text"""
        label = ttk.Label(parent, text=label_text + ":")
        label.grid(row=row, column=0, sticky="w", padx=5, pady=5)

        self.vars[var_name] = tk.BooleanVar()
        check = ttk.Checkbutton(parent, variable=self.vars[var_name])
        check.grid(row=row, column=1, sticky="w", padx=5, pady=5)

        help_text = HELP_TEXTS.get(var_name, "Nessun aiuto disponibile")
        help_btn = ttk.Button(parent, text="?", width=3,
                             command=lambda: messagebox.showinfo("Help", help_text))
        help_btn.grid(row=row, column=2, padx=5, pady=5)

        return row + 1

    def set_defaults(self):
        """Set default values from config_data"""
        for key, value in DEFAULT_VALUES.items():
            if key in self.vars:
                self.vars[key].set(value)

    def validate_and_get_config(self):
        """Validate all fields and return config dictionary"""
        try:
            gas_sensor_text = self.vars["gas_sensor_type"].get()
            gas_sensor_value = GAS_SENSOR_TYPES[gas_sensor_text]

            config = {
                "config": {
                    "ssid": self.vars["ssid"].get(),
                    "password": self.vars["password"].get(),
                    "device_id": self.vars["device_id"].get(),
                    "wifi_power": self.vars["wifi_power"].get() + "dBm",
                    "o3_zero_value": int(self.vars["o3_zero_value"].get()),
                    "average_measurements": int(self.vars["average_measurements"].get()),
                    "sea_level_altitude": float(self.vars["sea_level_altitude"].get()),
                    "upload_server": self.vars["upload_server"].get(),
                    "mics_calibration_values": {
                        "RED": int(self.vars["mics_cal_red"].get()),
                        "OX": int(self.vars["mics_cal_ox"].get()),
                        "NH3": int(self.vars["mics_cal_nh3"].get())
                    },
                    "mics_measurements_offsets": {
                        "RED": int(self.vars["mics_offset_red"].get()),
                        "OX": int(self.vars["mics_offset_ox"].get()),
                        "NH3": int(self.vars["mics_offset_nh3"].get())
                    },
                    "compensation_factors": {
                        "compH": float(self.vars["comp_h"].get()),
                        "compT": float(self.vars["comp_t"].get()),
                        "compP": float(self.vars["comp_p"].get())
                    },
                    "use_modem": self.vars["use_modem"].get(),
                    "modem_apn": self.vars["modem_apn"].get(),
                    "ntp_server": self.vars["ntp_server"].get(),
                    "timezone": self.vars["timezone"].get(),
                    "fw_auto_upgrade": self.vars["fw_auto_upgrade"].get(),
                    "gas_sensor_type": gas_sensor_value
                },
                "help": JSON_HELP_TEXTS
            }

            return config
        except ValueError as e:
            messagebox.showerror("Validation Error", f"Invalid value in one of the fields: {e}")
            return None

    def generate_config(self):
        """Generate and save the config file"""
        config = self.validate_and_get_config()
        if config is None:
            return

        file_path = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialfile="config_v4.json"
        )

        if not file_path:
            return

        try:
            with open(file_path, 'w') as f:
                json.dump(config, f, indent=2)

            messagebox.showinfo("Success", f"Configuration saved successfully to:\n{file_path}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save configuration:\n{e}")

    def load_config(self):
        """Load an existing config file"""
        file_path = filedialog.askopenfilename(
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")]
        )

        if not file_path:
            return

        try:
            with open(file_path, 'r') as f:
                data = json.load(f)

            config = data.get("config", {})

            self.vars["ssid"].set(config.get("ssid", ""))
            self.vars["password"].set(config.get("password", ""))
            self.vars["device_id"].set(config.get("device_id", ""))

            wifi_power = config.get("wifi_power", "17dBm").replace("dBm", "")
            self.vars["wifi_power"].set(wifi_power)

            self.vars["o3_zero_value"].set(str(config.get("o3_zero_value", -1)))
            self.vars["average_measurements"].set(str(config.get("average_measurements", 30)))
            self.vars["sea_level_altitude"].set(str(config.get("sea_level_altitude", 122.00)))
            self.vars["upload_server"].set(config.get("upload_server", ""))

            mics_cal = config.get("mics_calibration_values", {})
            self.vars["mics_cal_red"].set(str(mics_cal.get("RED", 955)))
            self.vars["mics_cal_ox"].set(str(mics_cal.get("OX", 900)))
            self.vars["mics_cal_nh3"].set(str(mics_cal.get("NH3", 163)))

            mics_offset = config.get("mics_measurements_offsets", {})
            self.vars["mics_offset_red"].set(str(mics_offset.get("RED", 0)))
            self.vars["mics_offset_ox"].set(str(mics_offset.get("OX", 0)))
            self.vars["mics_offset_nh3"].set(str(mics_offset.get("NH3", 0)))

            comp = config.get("compensation_factors", {})
            self.vars["comp_h"].set(str(comp.get("compH", 0.6)))
            self.vars["comp_t"].set(str(comp.get("compT", 1.352)))
            self.vars["comp_p"].set(str(comp.get("compP", 0.0132)))

            self.vars["use_modem"].set(config.get("use_modem", False))
            self.vars["modem_apn"].set(config.get("modem_apn", ""))

            self.vars["ntp_server"].set(config.get("ntp_server", "pool.ntp.org"))
            self.vars["timezone"].set(config.get("timezone", "GMT0"))

            self.vars["fw_auto_upgrade"].set(config.get("fw_auto_upgrade", True))

            gas_type_value = config.get("gas_sensor_type", 0)
            gas_type_text = "MICS6814"
            for text, value in GAS_SENSOR_TYPES.items():
                if value == gas_type_value:
                    gas_type_text = text
                    break
            self.vars["gas_sensor_type"].set(gas_type_text)

            messagebox.showinfo("Success", "Configuration loaded successfully")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load configuration:\n{e}")

    # ========================================================================
    # ESP32 Flash Tab Methods
    # ========================================================================

    def log(self, message):
        """Aggiunge un messaggio al log"""
        self.log_text.config(state='normal')
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)
        self.log_text.config(state='disabled')
        self.root.update_idletasks()

    def refresh_ports(self):
        """Aggiorna la lista delle porte disponibili"""
        ports = get_available_ports()

        if not ports:
            self.port_combo['values'] = ["Nessuna porta trovata"]
            self.port_var.set("Nessuna porta trovata")
            self.log("❌ Nessuna porta seriale trovata")
        else:
            port_descriptions = [f"{port} - {desc}" for port, desc in ports]
            self.port_combo['values'] = port_descriptions
            self.port_var.set(port_descriptions[0])
            self.log(f"✅ Trovate {len(ports)} porte seriali")

    def get_selected_port(self):
        """Estrae il nome della porta dalla selezione"""
        selection = self.port_var.get()
        if " - " in selection:
            return selection.split(" - ")[0]
        return None

    def detect_device(self):
        """Rileva informazioni sul dispositivo ESP32"""
        port = self.get_selected_port()
        if not port:
            messagebox.showwarning("Warning", "Seleziona una porta COM valida")
            return

        self.log(f"🔍 Rilevamento dispositivo su {port}...")

        def detect_thread():
            # Prima prova con il metodo standard
            info = get_chip_info(port)

            # Se il rilevamento ha dato risultati parziali o Unknown, prova il metodo verbose
            if info and (info.get('chip') == 'Unknown' or info.get('flash_size') == 'Unknown'):
                self.log("⚙️  Rilevamento standard incompleto, provo metodo alternativo...")
                verbose_info = get_chip_info_verbose(port)

                # Unisci le informazioni, preferendo quelle del metodo verbose se più complete
                if verbose_info:
                    if verbose_info.get('chip') != 'Unknown':
                        info['chip'] = verbose_info['chip']
                    if verbose_info.get('flash_size') != 'Unknown':
                        info['flash_size'] = verbose_info['flash_size']
                    if verbose_info.get('mac') != 'Unknown':
                        info['mac'] = verbose_info['mac']

                    # Log dell'output di debug se disponibile
                    if 'debug_output' in verbose_info:
                        self.log("📋 Output debug esptool:")
                        for line in verbose_info['debug_output'].split('\n'):
                            if line.strip() and any(keyword in line for keyword in
                                ['Chip is', 'MAC:', 'Detected', 'Serial']):
                                self.log(f"   {line.strip()}")

            if info:
                self.chip_info_text.config(state='normal')
                self.chip_info_text.delete(1.0, tk.END)

                chip_type = info.get('chip', 'Unknown')
                mac_addr = info.get('mac', 'Unknown')
                flash_size = info.get('flash_size', 'Unknown')

                # Salva flash size rilevata (normalizza a 4MB o 8MB)
                if '8' in flash_size.upper():
                    self.detected_flash_size = "8MB"
                elif '4' in flash_size.upper() or 'stimato' in flash_size.lower():
                    self.detected_flash_size = "4MB"
                else:
                    self.detected_flash_size = "4MB"  # Default

                self.log(f"💾 Flash size rilevata: {self.detected_flash_size}")

                self.chip_info_text.insert(tk.END, f"Chip: {chip_type}\n")
                self.chip_info_text.insert(tk.END, f"MAC: {mac_addr}\n")
                self.chip_info_text.insert(tk.END, f"Flash Size: {flash_size}\n")

                # Aggiungi nota se stimato
                if 'stimato' in flash_size.lower() or 'default' in flash_size.lower():
                    self.chip_info_text.insert(tk.END, "\nℹ️  Flash size stimata (4MB è il valore comune per ESP32)")

                self.chip_info_text.config(state='disabled')

                if chip_type != 'Unknown':
                    self.log(f"✅ Dispositivo rilevato: {chip_type}")
                else:
                    self.log("⚠️  Dispositivo rilevato ma tipo sconosciuto")
                    self.log("💡 Tip: Il flash funzionerà comunque con i parametri standard ESP32")
            else:
                self.log("❌ Impossibile rilevare il dispositivo")
                self.log("💡 Verifica che:")
                self.log("   - Il dispositivo sia collegato correttamente")
                self.log("   - I driver USB siano installati (CH340/CP2102)")
                self.log("   - Nessun altro programma stia usando la porta")
                messagebox.showerror("Error",
                    "Impossibile comunicare con il dispositivo.\n\n" +
                    "Verifica connessione e driver USB.")

        threading.Thread(target=detect_thread, daemon=True).start()

    def download_latest_firmware(self):
        """Scarica l'ultima release da GitHub"""
        self.log(f"📥 Recupero info release da {GITHUB_REPO}...")

        def download_thread():
            self.release_info = get_latest_release_info(GITHUB_REPO)

            if not self.release_info:
                self.log("❌ Impossibile recuperare info release")
                messagebox.showerror("Error", "Impossibile connettersi a GitHub")
                return

            version = get_release_version(self.release_info)
            self.log(f"📦 Ultima versione disponibile: {version}")
            self.log(f"💾 Scaricamento pacchetto per flash size: {self.detected_flash_size}")

            # Download directory
            download_dir = Path.home() / "Downloads" / "msp-firmware"

            # Scarica il pacchetto completo (ZIP con tutti i file)
            self.firmware_files = download_firmware_package(
                self.release_info,
                str(download_dir),
                flash_size=self.detected_flash_size,
                progress_callback=self.log
            )

            if self.firmware_files:
                # Mostra info sul pacchetto scaricato
                zip_name = Path(self.firmware_files['zip_path']).name if 'zip_path' in self.firmware_files else f"firmware-{self.detected_flash_size}"

                self.firmware_label.config(
                    text=f"✅ {zip_name} (v{version})",
                    foreground="green"
                )
                self.flash_btn.config(state='normal')

                self.log(f"✅ Pacchetto firmware scaricato ed estratto:")
                self.log(f"   • Bootloader: {Path(self.firmware_files['bootloader']).name}")
                self.log(f"   • Partitions: {Path(self.firmware_files['partitions']).name}")
                self.log(f"   • Boot App0: {Path(self.firmware_files['boot_app0']).name}")
                self.log(f"   • Firmware: {Path(self.firmware_files['firmware']).name}")

                # Mantieni compatibilità backward per firmware_path
                self.firmware_path = self.firmware_files['firmware']
            else:
                self.log("❌ Errore durante il download del pacchetto firmware")
                messagebox.showerror("Error", "Impossibile scaricare il firmware da GitHub")

        threading.Thread(target=download_thread, daemon=True).start()

    def select_local_firmware(self):
        """Seleziona un file firmware locale (.zip o .bin)"""
        file_path = filedialog.askopenfilename(
            title="Seleziona file firmware (.zip consigliato, .bin solo per update)",
            filetypes=[
                ("ZIP files", "*.zip"),
                ("Binary files", "*.bin"),
                ("All files", "*.*")
            ]
        )

        if file_path:
            if file_path.endswith('.zip'):
                # Estrai il ZIP e trova i file
                import zipfile
                import tempfile

                try:
                    extract_dir = tempfile.mkdtemp(prefix="msp_firmware_")
                    with zipfile.ZipFile(file_path, 'r') as zip_ref:
                        zip_ref.extractall(extract_dir)

                    # Trova i file necessari
                    self.firmware_files = {
                        'bootloader': None,
                        'partitions': None,
                        'boot_app0': None,
                        'firmware': None,
                        'zip_path': file_path,
                        'extract_dir': extract_dir
                    }

                    for root, dirs, files in os.walk(extract_dir):
                        for file in files:
                            filepath = os.path.join(root, file)
                            if 'bootloader' in file and file.endswith('.bin'):
                                self.firmware_files['bootloader'] = filepath
                            elif 'partitions' in file and file.endswith('.bin'):
                                self.firmware_files['partitions'] = filepath
                            elif 'boot_app0' in file and file.endswith('.bin'):
                                self.firmware_files['boot_app0'] = filepath
                            elif file == 'msp-firmware.ino.bin':
                                self.firmware_files['firmware'] = filepath

                    # Verifica che tutti i file siano stati trovati
                    missing = [k for k, v in self.firmware_files.items() if v is None and k not in ['zip_path', 'extract_dir']]
                    if missing:
                        self.log(f"⚠️  File mancanti nello ZIP: {missing}")
                        messagebox.showwarning("Warning", f"ZIP incompleto. File mancanti: {missing}")
                        return

                    self.firmware_path = self.firmware_files['firmware']  # Compatibilità
                    self.firmware_label.config(
                        text=f"✅ {Path(file_path).name} (pacchetto completo)",
                        foreground="green"
                    )
                    self.flash_btn.config(state='normal')
                    self.log(f"✅ Pacchetto firmware selezionato: {Path(file_path).name}")
                    self.log("   ✓ Bootloader, partitions, boot_app0 e firmware trovati")

                except Exception as e:
                    self.log(f"❌ Errore estrazione ZIP: {e}")
                    messagebox.showerror("Error", f"Impossibile estrarre il file ZIP:\n{e}")

            else:
                # File .bin singolo (solo update, non flash completo)
                self.firmware_path = file_path
                self.firmware_files = None  # Nessun pacchetto completo
                self.firmware_label.config(
                    text=f"⚠️  {Path(file_path).name} (solo update, no bootloader)",
                    foreground="orange"
                )
                self.flash_btn.config(state='normal')
                self.log(f"⚠️  File .bin singolo selezionato: {file_path}")
                self.log("⚠️  ATTENZIONE: Un file .bin singolo NON include bootloader!")
                self.log("💡 Usa un file .zip per un flash completo dopo erase")
                messagebox.showwarning(
                    "Attenzione",
                    "Hai selezionato un file .bin singolo.\n\n" +
                    "Questo è adatto solo per UPDATE OTA, non per flash completo.\n\n" +
                    "Se hai fatto 'Erase Flash', scarica il pacchetto .zip completo da GitHub!"
                )

    def flash_device(self):
        """Flasha il firmware sul dispositivo"""
        port = self.get_selected_port()
        if not port:
            messagebox.showwarning("Warning", "Seleziona una porta COM valida")
            return

        if not self.firmware_path and not self.firmware_files:
            messagebox.showwarning("Warning", "Seleziona prima un firmware")
            return

        # Determina se abbiamo un pacchetto completo o solo un .bin
        has_full_package = self.firmware_files is not None

        if has_full_package:
            confirm_msg = (
                f"Flashare il firmware completo su {port}?\n\n" +
                f"Questo installerà:\n" +
                f"  • Bootloader (0x1000)\n" +
                f"  • Partition Table (0x8000)\n" +
                f"  • Boot App0 (0xe000)\n" +
                f"  • Firmware principale (0x10000)\n\n" +
                f"Flash size: {self.detected_flash_size}"
            )
        else:
            confirm_msg = (
                f"⚠️ ATTENZIONE: Stai flashando solo il file .bin!\n\n" +
                f"Questo NON include bootloader e partition table.\n" +
                f"È adatto solo per update OTA, non dopo erase_flash.\n\n" +
                f"Vuoi continuare comunque?"
            )

        result = messagebox.askyesno("Conferma Flash", confirm_msg)

        if not result:
            return

        self.log(f"⚡ Inizio flash su {port}...")
        self.log(f"💾 Flash size: {self.detected_flash_size}")
        self.flash_btn.config(state='disabled')

        def flash_thread():
            if has_full_package:
                # Flash pacchetto completo (raccomandato)
                self.log("📦 Flash pacchetto completo (bootloader + firmware)...")
                success, message = flash_firmware_package(
                    port,
                    self.firmware_files,
                    flash_size=self.detected_flash_size,
                    progress_callback=self.log
                )
            else:
                # Flash solo firmware (deprecato, solo per update)
                self.log("⚠️  Flash solo firmware (nessun bootloader)...")
                success, message = flash_firmware(
                    port,
                    self.firmware_path,
                    flash_size=self.detected_flash_size,
                    progress_callback=self.log
                )

            if success:
                self.log(f"✅ {message}")
                messagebox.showinfo("Success", message)
            else:
                self.log(f"❌ {message}")
                messagebox.showerror("Error", message)

            self.flash_btn.config(state='normal')

        threading.Thread(target=flash_thread, daemon=True).start()

    def erase_device_flash(self):
        """Cancella la flash del dispositivo"""
        port = self.get_selected_port()
        if not port:
            messagebox.showwarning("Warning", "Seleziona una porta COM valida")
            return

        result = messagebox.askyesno(
            "Conferma",
            f"Cancellare completamente la flash su {port}?\n\n⚠️ Questa operazione è IRREVERSIBILE!"
        )

        if not result:
            return

        self.log(f"🗑️ Cancellazione flash su {port}...")

        def erase_thread():
            success, message = erase_flash(port, progress_callback=self.log)

            if success:
                self.log(f"✅ {message}")
                messagebox.showinfo("Success", message)
            else:
                self.log(f"❌ {message}")
                messagebox.showerror("Error", message)

        threading.Thread(target=erase_thread, daemon=True).start()


def main():
    root = tk.Tk()
    app = ConfigGeneratorGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
