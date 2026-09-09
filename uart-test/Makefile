.PHONY: flash monitor bin esptool

TINYGO_TARGET ?= xiao-esp32s3
PORT ?= /dev/cu.usbmodem2101
BAUD ?= 9600
BIN ?= /tmp/uart-test.bin

flash:
	tinygo flash -target=$(TINYGO_TARGET) -port=$(PORT) -monitor ./

monitor:
	tinygo monitor -port=$(PORT)

bin:
	tinygo build -o $(BIN) -target=$(TINYGO_TARGET) ./
	@echo "esptool.py --chip esp32s3 --port $(PORT) --baud 115200 write-flash 0x0 $(BIN)"

esptool: bin
	esptool.py --chip esp32s3 --port $(PORT) --baud 115200 write-flash 0x0 $(BIN)
