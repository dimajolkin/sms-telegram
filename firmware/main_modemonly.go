//go:build tinygo && modemonly

// Минимальный прогон: только hardware UART + SIM800L (без SPI/WiFi/net).
package main

import (
	"machine"
	"time"
)

const uartBaud = 9600

func main() {
	time.Sleep(1 * time.Second)
	println("modemonly: TinyGo DefaultUART only, no SPI/WiFi")

	pinModemRST.Configure(machine.PinConfig{Mode: machine.PinOutput})
	pinModemRST.High()
	println("modem RST idle HIGH")

	uart, err := openModemUART(uartBaud)
	if err != nil {
		println("uart:", err.Error())
		return
	}
	println("modem uart: UART0 IO_MUX D6/D7 @", uartBaud)
	modem := &Modem{uart: uart}

	for {
		modem.ResetHW()
		if err := modem.syncAT(20); err != nil {
			println("sync:", err.Error())
			time.Sleep(2 * time.Second)
			continue
		}
		println("modem AT OK")
		if _, err := modem.AT("ATI", 3*time.Second); err != nil {
			println("ATI:", err.Error())
		}
		for {
			time.Sleep(5 * time.Second)
			resp, err := modem.AT("AT", 2*time.Second)
			if err != nil {
				println("keepalive fail:", err.Error())
				break
			}
			println("keepalive", len(resp))
		}
	}
}
