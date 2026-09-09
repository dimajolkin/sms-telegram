//go:build tinygo && xiao_esp32s3

package main

import "machine"

// Seeed XIAO ESP32S3 — D0…D10.
// UART0 IO_MUX (после patches/tinygo): D6=TX→Modem RX, D7=RX←Modem TX.
const (
	uartTX = machine.GPIO43 // D6
	uartRX = machine.GPIO44 // D7

	pinTFTSCK   = machine.GPIO7  // D8
	pinTFTMOSI  = machine.GPIO9  // D10
	pinTFTCS    = machine.GPIO8  // D9
	pinTFTDC    = machine.GPIO5  // D4
	pinTFTRST   = machine.GPIO6  // D5
	pinTFTBL    = machine.GPIO4  // D3
	pinBtnNext  = machine.GPIO1  // D0
	pinBtnOK    = machine.GPIO2  // D1
	pinModemRST = machine.GPIO3  // D2 → SIM800L RST
)
