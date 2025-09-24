ARDUINO_CLI_VERSION := 1.2.2
ARDUINO_CLI_URL := https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh
ARDUINO_LINT_VERSION := 1.2.1
ARDUINO_LINT_URL := https://raw.githubusercontent.com/arduino/arduino-lint/main/etc/install.sh

SKETCH := msp-firmware
BOARD := esp32
PORT := /dev/tty.SLAB_USBtoUART

ADDITIONAL_URLS := \
	https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
CORE := esp32:esp32@2.0.17
LIBRARIES := \
	"BSEC Software Library"@1.8.1492 \
	"PMS Library"@1.1.0 \
	SSLClient@1.6.11 \
	U8g2@2.34.22 \
	TinyGSM@0.11.7 \
	ArduinoJson@7.4.2
LIBRARIES_URLS := \
	https://github.com/A-A-Milano-Smart-Park/MiCS6814-I2C-MOD-Library \
	https://github.com/DFRobot/DFRobot_MICS

# The FQBN is the core, followed by the board.
CORE_NAME := $(shell echo $(CORE) | cut -f1 -d@)

# Flash size configuration (can be overridden: make build FLASH_SIZE=4MB)
ifndef FLASH_SIZE
FLASH_SIZE := 8M
endif

# Set partition scheme based on flash size
ifeq ($(FLASH_SIZE),4MB)
PARTITION_SCHEME := partitions_4mb
FQBN := $(CORE_NAME):$(BOARD):FlashMode=dio,FlashFreq=40,FlashSize=4M,PartitionScheme=partitions_4mb,PSRAM=enabled
else ifeq ($(FLASH_SIZE),4M)
PARTITION_SCHEME := partitions_4mb
FQBN := $(CORE_NAME):$(BOARD):FlashMode=dio,FlashFreq=40,FlashSize=4M,PartitionScheme=partitions_4mb,PSRAM=enabled
else
PARTITION_SCHEME := partitions_8mb
FQBN := $(CORE_NAME):$(BOARD):FlashMode=dio,FlashFreq=40,FlashSize=8M,PartitionScheme=partitions_8mb,PSRAM=enabled
endif

# Treat all warnings as errors.
BUILDPROP := compiler.warning_flags.all='-Wall -Wextra'

################################################################################

ROOT := $(PWD)
BINDIR := $(ROOT)/bin
ETCDIR := $(ROOT)/etc
SRCDIR := $(ROOT)
VARDIR := $(ROOT)/var
LOGDIR := $(VARDIR)/log
BUILDDIR := $(VARDIR)/build

# Build a list of source files for dependency management.
SRCS := $(shell find $(SRCDIR) -name "*.ino" -or -name "*.cpp" -or -name "*.c" -or -name "*.h")

# Compute version string from git sandbox status
VERSION_STRING := $(shell git describe --always --tags --dirty)

ifdef API_SECRET_SALT
CPP_EXTRA_FLAGS := -DAPI_SECRET_SALT="$(API_SECRET_SALT)"
endif

ifdef API_SERVER
CPP_EXTRA_FLAGS += -DAPI_SERVER="$(API_SERVER)"
endif

ifndef CUSTOM_DEBUG_LEVEL
CUSTOM_DEBUG_LEVEL := 5
endif

# Set the location of the Arduino environment.
export ARDUINO_DATA_DIR = $(VARDIR)

################################################################################

.PHONY: all help env print-core-version properties lint build upload clean clean-all

all: build

################################################################################

##
# Tell the user what the targets are.
##

help:
	@echo
	@echo "Targets:"
	@echo "   env        Install the Arduino CLI environment."
	@echo "   properties Show all build properties used instead of compiling."
	@echo "   lint       Validate the sketch with arduino-lint."
	@echo "   build      Compile the sketch (default: 8MB flash)."
	@echo "   upload     Upload to the board."
	@echo "   clean      Remove only files ignored by Git."
	@echo "   clean-all  Remove all untracked files."
	@echo
	@echo "Flash Size Options:"
	@echo "   make build FLASH_SIZE=4MB   Build for 4MB ESP32 (1.75MB app x2, dual OTA, no SPIFFS)"
	@echo "   make build FLASH_SIZE=8M    Build for 8MB ESP32 (2.7MB app x2, dual OTA, rollback enabled)"
	@echo
	@echo "Flashing Workflow:"
	@echo "   1. Build: make build FLASH_SIZE=4MB (or 8M)"
	@echo "   2. Flash: ./scripts/detect-and-flash.sh (auto-copies binaries)"
	@echo ""
	@echo "Flashing Scripts:"
	@echo "   ./scripts/copy-binaries.sh           Copy binaries to scripts directory"
	@echo "   ./scripts/detect-and-flash.sh        Auto-detect and flash"
	@echo "   ./scripts/flash-msp-firmware-4mb.sh  Flash for 4MB devices"
	@echo "   ./scripts/flash-msp-firmware-8mb.sh  Flash for 8MB devices"
	@echo

################################################################################

# Run in --silent mode unless the user sets VERBOSE=1 on the
# command-line.

ifndef VERBOSE
.SILENT:
else
ARGS_VERBOSE := --verbose
endif

# curl|sh really is the documented install method.
# https://arduino.github.io/arduino-cli/latest/installation/
# https://arduino.github.io/arduino-lint/latest/installation/

$(BINDIR)/arduino-cli:
	mkdir -p $(BINDIR) $(ETCDIR)
	curl -fsSL $(ARDUINO_CLI_URL) | BINDIR=$(BINDIR) sh -s $(ARDUINO_CLI_VERSION)

$(BINDIR)/arduino-lint:
	mkdir -p $(BINDIR) $(ETCDIR)
	curl -fsSL $(ARDUINO_LINT_URL) | BINDIR=$(BINDIR) sh -s $(ARDUINO_LINT_VERSION)

$(ETCDIR)/arduino-cli.yaml: $(BINDIR)/arduino-cli
	mkdir -p $(ETCDIR) $(VARDIR) $(LOGDIR)
	$(BINDIR)/arduino-cli config init --log-file $(LOGDIR)/arduino.log $(ARGS_VERBOSE)
	mv $(VARDIR)/arduino-cli.yaml $(ETCDIR)
	sed -i -e 's%\(  user:\)\(.*\)%\1 $(VARDIR)%' $(ETCDIR)/arduino-cli.yaml
ifdef ADDITIONAL_URLS
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml config add board_manager.additional_urls $(ADDITIONAL_URLS)
endif
# Installing libraries from repositories is considered unsafe
# https://arduino.github.io/arduino-cli/0.16/configuration/#configuration-keys
ifdef LIBRARIES_URLS
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml config set library.enable_unsafe_install true
endif

print-core-version:
	@echo $$($(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml core list | grep "$(CORE_NAME)" | cut -f2 -d' ') ; \

env: $(BINDIR)/arduino-cli $(BINDIR)/arduino-lint $(ETCDIR)/arduino-cli.yaml
	mkdir -p $(BUILDDIR)
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml core update-index
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml core install $(CORE)
ifdef LIBRARIES
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml lib update-index
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml lib install $(LIBRARIES)
endif
ifdef LIBRARIES_URLS
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml lib install --git-url $(LIBRARIES_URLS)
endif
	# Always copy custom partition tables (force update)
	cp $(SRCDIR)/partition_tables/partitions_4mb.csv $(VARDIR)/packages/esp32/hardware/esp32/2.0.17/tools/partitions/
	cp $(SRCDIR)/partition_tables/partitions_8mb.csv $(VARDIR)/packages/esp32/hardware/esp32/2.0.17/tools/partitions/
	# Backup and restore boards.txt with updated partition schemes
	@BOARDS_FILE="$(VARDIR)/packages/esp32/hardware/esp32/2.0.17/boards.txt"; \
	BOARDS_BACKUP="$(VARDIR)/packages/esp32/hardware/esp32/2.0.17/boards.txt.backup"; \
	if [ ! -f "$$BOARDS_BACKUP" ]; then \
		echo "Creating backup of original boards.txt..."; \
		cp "$$BOARDS_FILE" "$$BOARDS_BACKUP"; \
	fi; \
	echo "Restoring boards.txt from backup and adding custom partitions..."; \
	cp "$$BOARDS_BACKUP" "$$BOARDS_FILE"; \
	echo "esp32.menu.PartitionScheme.partitions_4mb=4MB OTA Dual (1.75MB APP x2/no SPIFFS)" > /tmp/custom_partitions.txt; \
	echo "esp32.menu.PartitionScheme.partitions_4mb.build.partitions=partitions_4mb" >> /tmp/custom_partitions.txt; \
	echo "esp32.menu.PartitionScheme.partitions_4mb.upload.maximum_size=1835008" >> /tmp/custom_partitions.txt; \
	echo "esp32.menu.PartitionScheme.partitions_8mb=8MB OTA Dual (2.7MB APP x2/1MB SPIFFS)" >> /tmp/custom_partitions.txt; \
	echo "esp32.menu.PartitionScheme.partitions_8mb.build.partitions=partitions_8mb" >> /tmp/custom_partitions.txt; \
	echo "esp32.menu.PartitionScheme.partitions_8mb.upload.maximum_size=2752512" >> /tmp/custom_partitions.txt; \
	sed -i '' '/esp32.menu.PartitionScheme.rainmaker.upload.maximum_size=3145728/r /tmp/custom_partitions.txt' "$$BOARDS_FILE"; \
	rm /tmp/custom_partitions.txt; \
	echo "boards.txt updated with latest partition configurations"

sketch:
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml sketch new $(SRCDIR)

properties:
	mkdir -p $(BUILDDIR)
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml compile \
	--build-path $(BUILDDIR) \
	--build-property $(BUILDPROP) \
	--build-property 'compiler.cpp.extra_flags=-DVERSION_STRING="$(VERSION_STRING)" $(CPP_EXTRA_FLAGS)' \
	--warnings all --log-file $(LOGDIR)/build.log --log-level debug $(ARGS_VERBOSE) \
	--fqbn $(FQBN) $(SRCDIR) --show-properties

lint:
	$(BINDIR)/arduino-lint $(SRCDIR)

$(BUILDDIR)/$(SKETCH).ino.elf: $(SRCS)
	mkdir -p $(BUILDDIR)
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml compile \
	--build-path $(BUILDDIR) \
	--build-property $(BUILDPROP) \
	--build-property 'compiler.cpp.extra_flags=-DVERSION_STRING="$(VERSION_STRING)" $(CPP_EXTRA_FLAGS)' \
	--build-property 'build.code_debug=$(CUSTOM_DEBUG_LEVEL)' \
	--warnings all --log-file $(LOGDIR)/build.log --log-level debug $(ARGS_VERBOSE) \
	--fqbn $(FQBN) $(SRCDIR)

build: $(BUILDDIR)/$(SKETCH).ino.elf
	rm -rf $(SRCDIR)/build

upload: build
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml upload \
	--log-file $(LOGDIR)/upload.log --log-level debug $(ARGS_VERBOSE) \
	--port $(PORT) --fqbn $(FQBN) --input-file $(BUILDDIR)/$(SKETCH).ino.bin

clean:
	rm -rf $(BUILDDIR)

clean-all:
	git clean -dxf

################################################################################
