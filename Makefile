.PHONY: firmware flash flash-modem tidy monitor

# Seeed XIAO ESP32S3 (опционально: TINYGO_TARGET=esp32s3-generic)
TINYGO_TARGET ?= xiao-esp32s3
PORT ?= /dev/cu.usbmodem2101

-include secrets.mk

WIFI_SSID ?=
WIFI_PASSWORD ?=
TELEGRAM_BOT_TOKEN ?=
TELEGRAM_CHAT_ID ?=

LDFLAGS = -X main.ssid=$(WIFI_SSID) \
	-X main.password=$(WIFI_PASSWORD) \
	-X main.botToken=$(TELEGRAM_BOT_TOKEN) \
	-X main.chatID=$(TELEGRAM_CHAT_ID)

# 12kb: больше heap под повторный TLS handshake (16/32kb тесны на S3+WiFi).
STACK_SIZE ?= 12kb

TINYGO_FLAGS = -target=$(TINYGO_TARGET) -size short -stack-size=$(STACK_SIZE) \
	-ldflags="$(LDFLAGS)"

firmware:
	mkdir -p bin
	tinygo build -o bin/firmware.elf $(TINYGO_FLAGS) ./firmware

flash:
	tinygo flash -port=$(PORT) -monitor $(TINYGO_FLAGS) ./firmware

# Только HW UART + модем (~22KB), без SPI/WiFi/net
flash-modem:
	tinygo flash -target=$(TINYGO_TARGET) -port=$(PORT) -tags=modemonly -size short -stack-size=8kb -monitor ./firmware

monitor:
	tinygo monitor -port=$(PORT)

tidy:
	go mod tidy
