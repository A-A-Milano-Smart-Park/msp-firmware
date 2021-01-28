ARDUINO_CLI_VERSION :=
ARDUINO_CLI_URL := https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh
ARDUINO_LINT_VERSION :=
ARDUINO_LINT_URL := https://raw.githubusercontent.com/arduino/arduino-lint/main/etc/install.sh

SKETCH := msp-firmware
BOARD := esp32
PORT := /dev/ttyACM0

ADDITIONAL_URLS := \
	https://dl.espressif.com/dl/package_esp32_index.json \
	http://arduino.esp8266.com/stable/package_esp8266com_index.json
CORES := esp32:esp32
LIBRARIES := \
	"BSEC Software Library" \
	MiCS6814-I2C \
	"PMS Library" \
	SSLClient U8g2

# The FQBN is the first specified core, followed by the board.
FQBN := $(word 1, $(CORES)):$(BOARD)

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
CACHEDIR := $(BUILDDIR)/cache

# Build a list of source files for dependency management.
SRCS := $(shell find $(SRCDIR) -name "*.ino" -or -name "*.cpp" -or -name "*.c" -or -name "*.h")

# Set the location of the Arduino environment.
export ARDUINO_DATA_DIR = $(VARDIR)

################################################################################

.PHONY: all help env sketch properties lint build upload clean clean-all

all: build

################################################################################

##
# Tell the user what the targets are.
##

help:
	@echo
	@echo "Targets:"
	@echo "   env        Install the Arduino CLI environment."
	@echo "   sketch     Initialise a new sketch."
	@echo "   properties Show all build properties used instead of compiling."
	@echo "   lint       Validate the sketch with arduino-lint."
	@echo "   build      Compile the sketch."
	@echo "   upload     Upload to the board."
	@echo "   clean      Remove only files ignored by Git."
	@echo "   clean-all  Remove all untracked files."
	@echo

################################################################################

# Run in --silent mode unless the user sets VERBOSE=1 on the
# command-line.

ifndef VERBOSE
.SILENT:
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
	$(BINDIR)/arduino-cli config init --log-file $(LOGDIR)/arduino.log --verbose
	mv $(VARDIR)/arduino-cli.yaml $(ETCDIR)
	sed -i -e 's%\(  user:\)\(.*\)%\1 $(VARDIR)%' $(ETCDIR)/arduino-cli.yaml
ifdef ADDITIONAL_URLS
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml config add board_manager.additional_urls $(ADDITIONAL_URLS)
endif

env: $(BINDIR)/arduino-cli $(BINDIR)/arduino-lint $(ETCDIR)/arduino-cli.yaml
	mkdir -p $(BUILDDIR)
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml core update-index
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml core install $(CORES)
	# add platform.local.txt to installed esp32 platform
	# required by BSEC Arduino Library
	# https://github.com/BoschSensortec/BSEC-Arduino-library#3-modify-the-platformtxt-file
	cp platform.local.txt $(VARDIR)/packages/esp32/hardware/esp32/1.0.4/
ifdef LIBRARIES
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml lib update-index
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml lib install $(LIBRARIES)
endif

sketch:
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml sketch new $(SRCDIR)

properties:
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml compile \
	--build-path $(BUILDDIR) --build-cache-path $(CACHEDIR) \
	--build-property $(BUILDPROP) \
	--warnings all --log-file $(LOGDIR)/build.log --log-level debug --verbose \
	--fqbn $(FQBN) $(SRCDIR) --show-properties

lint:
	$(BINDIR)/arduino-lint $(SRCDIR)

$(BUILDDIR)/$(SKETCH).ino.elf: $(SRCS)
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml compile \
	--build-path $(BUILDDIR) --build-cache-path $(CACHEDIR) \
	--build-property $(BUILDPROP) \
	--warnings all --log-file $(LOGDIR)/build.log --log-level debug --verbose \
	--fqbn $(FQBN) $(SRCDIR)

build: $(BUILDDIR)/$(SKETCH).ino.elf
	rm -rf $(SRCDIR)/build

upload: build
	$(BINDIR)/arduino-cli --config-file $(ETCDIR)/arduino-cli.yaml upload \
	--log-file $(LOGDIR)/upload.log --log-level debug --verbose \
	--port $(PORT) --fqbn $(FQBN) --input-file $(BUILDDIR)/$(SKETCH).ino.hex

clean:
	rm -rf $(BUILDDIR)

clean-all:
	git clean -dxf

################################################################################
