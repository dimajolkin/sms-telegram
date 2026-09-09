//go:build tinygo

// После patches/tinygo/apply.sh: штатный machine.UART.Configure.
// Проводка: D6→ModemRX, D7←ModemTX (UART0 IO_MUX).
package main

import (
	"machine"
	"time"
)

const baud = 9600

func main() {
	time.Sleep(2 * time.Second)
	machine.Serial.Configure(machine.UARTConfig{})
	println("patched UART0 IO_MUX @", baud)

	uart := machine.DefaultUART
	if err := uart.Configure(machine.UARTConfig{
		BaudRate: baud,
		TX:       uartTX,
		RX:       uartRX,
	}); err != nil {
		println("configure:", err.Error())
		return
	}

	for n := 1; n <= 10; n++ {
		for uart.Buffered() > 0 {
			_, _ = uart.ReadByte()
		}
		for _, b := range []byte("AT\r") {
			_ = uart.WriteByte(b)
		}
		deadline := time.Now().Add(1 * time.Second)
		for time.Now().Before(deadline) {
			if uart.Buffered() > 0 {
				// give a little time for the rest of the reply
				time.Sleep(50 * time.Millisecond)
				break
			}
			time.Sleep(2 * time.Millisecond)
		}
		print(n, " n=", uart.Buffered(), " [")
		for uart.Buffered() > 0 {
			b, err := uart.ReadByte()
			if err != nil {
				break
			}
			switch {
			case b == '\r':
				print("\\r")
			case b == '\n':
				print("\\n")
			case b >= 32 && b < 127:
				print(string(b))
			default:
				print(".")
			}
		}
		println("]")
		time.Sleep(200 * time.Millisecond)
	}
	println("done")
	for {
		time.Sleep(time.Hour)
	}
}
