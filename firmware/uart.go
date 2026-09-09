//go:build tinygo

package main

import "machine"

// openModemUART — UART0 IO_MUX на D6/D7 (нужен patches/tinygo/apply.sh).
func openModemUART(baud uint32) (*machine.UART, error) {
	uart := machine.DefaultUART
	err := uart.Configure(machine.UARTConfig{
		BaudRate: baud,
		TX:       uartTX,
		RX:       uartRX,
	})
	return uart, err
}

// rebindModemUART — повторный Configure (после WiFi blob / AT timeout).
func rebindModemUART(m *Modem, baud uint32) error {
	println("modem uart rebind…")
	uart, err := openModemUART(baud)
	if err != nil {
		return err
	}
	m.uart = uart
	if err := m.syncAT(12); err != nil {
		return err
	}
	println("modem uart rebound OK")
	return nil
}
