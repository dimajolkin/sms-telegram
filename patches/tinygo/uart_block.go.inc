// UART block for ESP32-S3 — replace stub in machine_esp32s3.go
// (from `var DefaultUART` through `func (uart *UART) flush() {}`).
//
// Proven on XIAO ESP32S3: UART0 + IO_MUX GPIO43/TX + GPIO44/RX @ 9600 ↔ SIM800L OK.
// USB Serial/JTAG is separate (GPIO19/20), not UART0.
// GPIO matrix path included for non-default pins (less tested than IO_MUX).

var DefaultUART = UART0

var (
	UART0  = &_UART0
	_UART0 = UART{Bus: esp.UART0, Buffer: NewRingBuffer()}
	UART1  = &_UART1
	_UART1 = UART{Bus: esp.UART1, Buffer: NewRingBuffer()}
	UART2  = &_UART2
	_UART2 = UART{Bus: esp.UART2, Buffer: NewRingBuffer()}

	onceUart = sync.Once{}
)

// CPU int for UART RX. TinyGo S3: USB=8, timer=9, GPIO=10(edge).
// Int 11 = profiling. BT uses 13/17. WiFi moved to 18 (see patches/espradio).
// 12 = free level-1 level-triggered — proven with SIM800L.
const cpuInterruptFromUART = 12

const (
	uartInterrupts = esp.UART_INT_ENA_RXFIFO_FULL_INT_ENA |
		esp.UART_INT_ENA_RXFIFO_TOUT_INT_ENA |
		esp.UART_INT_ENA_PARITY_ERR_INT_ENA |
		esp.UART_INT_ENA_FRM_ERR_INT_ENA |
		esp.UART_INT_ENA_RXFIFO_OVF_INT_ENA |
		esp.UART_INT_ENA_GLITCH_DET_INT_ENA

	// XTAL 40MHz — fixed absolute baud (matches former stub clock source).
	uartSCLKHz  = xtalClock
	uartSCLKSel = 3 // 1=APB, 2=RTC, 3=XTAL
)

type UART struct {
	Bus                  *esp.UART_Type
	Buffer               *RingBuffer
	ParityErrorDetected  bool
	DataErrorDetected    bool
	DataOverflowDetected bool
}

type uartRegs struct {
	intrMap       *volatile.Register32
	clkEn         *volatile.Register32
	rstEn         *volatile.Register32
	clkRstMask    uint32
	matrixSignal  uint32
	nativeTX      Pin
	nativeRX      Pin
	hasNativePins bool
}

func (uart *UART) Configure(config UARTConfig) error {
	if config.BaudRate == 0 {
		config.BaudRate = 115200
	}
	if config.TX == 0 && config.RX == 0 {
		config.TX = UART_TX_PIN
		config.RX = UART_RX_PIN
	}
	if config.TX != NoPin && config.TX == config.RX {
		return errors.New("UART: TX and RX must differ")
	}

	regs, err := uart.regs()
	if err != nil {
		return err
	}
	uart.initClock(regs)

	uart.Bus.SetCLK_CONF_TX_SCLK_EN(0)
	uart.Bus.SetCLK_CONF_RX_SCLK_EN(0)
	uart.Bus.SetCLK_CONF_SCLK_SEL(uartSCLKSel)
	uart.Bus.SetCLK_CONF_SCLK_DIV_NUM(0)
	uart.Bus.SetCLK_CONF_SCLK_DIV_A(0)
	uart.Bus.SetCLK_CONF_SCLK_DIV_B(0)
	uart.SetBaudRate(config.BaudRate)

	uart.Bus.SetCONF0_BIT_NUM(3) // 8N1
	uart.Bus.SetCONF0_STOP_BIT_NUM(1)
	uart.Bus.SetCONF0_PARITY_EN(0)
	uart.Bus.SetRS485_CONF_RS485_EN(0)
	uart.Bus.SetRS485_CONF_RS485TX_RX_EN(0)
	uart.Bus.SetRS485_CONF_RS485RXBY_TX_EN(0)
	uart.Bus.SetCONF0_IRDA_EN(0)
	uart.Bus.SetCONF0_TX_FLOW_EN(0)
	uart.Bus.SetCONF1_RX_FLOW_EN(0)
	uart.Bus.SetID_REG_UPDATE(1)

	uart.setupPins(config, regs)
	uart.configureInterrupt(regs.intrMap)
	uart.enableTransmitter()
	uart.enableReceiver()

	uart.Bus.SetCLK_CONF_SCLK_EN(1)
	uart.Bus.SetCLK_CONF_TX_SCLK_EN(1)
	uart.Bus.SetCLK_CONF_RX_SCLK_EN(1)
	return nil
}

func (uart *UART) regs() (uartRegs, error) {
	switch uart.Bus {
	case esp.UART0:
		return uartRegs{
			intrMap:       &esp.INTERRUPT_CORE0.UART_INTR_MAP,
			clkEn:         &esp.SYSTEM.PERIP_CLK_EN0,
			rstEn:         &esp.SYSTEM.PERIP_RST_EN0,
			clkRstMask:    esp.SYSTEM_PERIP_CLK_EN0_UART_CLK_EN,
			matrixSignal:  6,
			nativeTX:      43,
			nativeRX:      44,
			hasNativePins: true,
		}, nil
	case esp.UART1:
		return uartRegs{
			intrMap:       &esp.INTERRUPT_CORE0.UART1_INTR_MAP,
			clkEn:         &esp.SYSTEM.PERIP_CLK_EN0,
			rstEn:         &esp.SYSTEM.PERIP_RST_EN0,
			clkRstMask:    esp.SYSTEM_PERIP_CLK_EN0_UART1_CLK_EN,
			matrixSignal:  9,
			nativeTX:      17,
			nativeRX:      18,
			hasNativePins: true,
		}, nil
	case esp.UART2:
		return uartRegs{
			intrMap:       &esp.INTERRUPT_CORE0.UART2_INTR_MAP,
			clkEn:         &esp.SYSTEM.PERIP_CLK_EN1,
			rstEn:         &esp.SYSTEM.PERIP_RST_EN1,
			clkRstMask:    esp.SYSTEM_PERIP_CLK_EN1_UART2_CLK_EN,
			matrixSignal:  12,
			hasNativePins: false,
		}, nil
	}
	return uartRegs{}, errors.New("UART: unsupported UARTn")
}

func (uart *UART) initClock(regs uartRegs) {
	esp.SYSTEM.PERIP_CLK_EN0.SetBits(esp.SYSTEM_PERIP_CLK_EN0_UART_MEM_CLK_EN)
	regs.clkEn.SetBits(regs.clkRstMask)
	// ESP32-S3: rst_core before uart_rst pulse (ESP-IDF workaround)
	uart.Bus.SetCLK_CONF_RST_CORE(1)
	regs.rstEn.SetBits(regs.clkRstMask)
	regs.rstEn.ClearBits(regs.clkRstMask)
	uart.Bus.SetCLK_CONF_RST_CORE(0)
	uart.Bus.SetID_REG_UPDATE(0)
	esp.RTC_CNTL.SetCLK_CONF_DIG_CLK8M_EN(1)
	for uart.Bus.GetID_REG_UPDATE() > 0 {
	}
}

func (uart *UART) SetBaudRate(baudRate uint32) {
	maxDiv := uint32((1 << 12) - 1)
	sclkDiv := (uartSCLKHz + (maxDiv * baudRate) - 1) / (maxDiv * baudRate)
	clkDiv := (uartSCLKHz << 4) / (baudRate * sclkDiv)
	uart.Bus.SetCLKDIV(clkDiv >> 4)
	uart.Bus.SetCLKDIV_FRAG(clkDiv & 0xf)
	uart.Bus.SetCLK_CONF_SCLK_DIV_NUM(sclkDiv - 1)
}

func (uart *UART) setupPins(config UARTConfig, regs uartRegs) {
	useIOMux := regs.hasNativePins &&
		(config.TX == NoPin && config.RX == NoPin ||
			config.TX == regs.nativeTX && config.RX == regs.nativeRX)

	if useIOMux {
		uart.setupIOMux(regs)
		return
	}
	if config.TX != NoPin {
		config.TX.Configure(PinConfig{Mode: PinOutput})
		config.TX.High()
		config.TX.outFunc().Set(regs.matrixSignal)
	}
	if config.RX != NoPin {
		config.RX.Configure(PinConfig{Mode: PinInputPullup})
		inFunc(regs.matrixSignal).Set(esp.GPIO_FUNC_IN_SEL_CFG_SEL | uint32(config.RX))
	}
}

// setupIOMux connects UART to its dedicated IO_MUX pins (FUNC=0).
// Verified: UART0 on GPIO43/44 (XIAO D6/D7).
func (uart *UART) setupIOMux(regs uartRegs) {
	tx, rx := regs.nativeTX, regs.nativeRX
	// MCU_SEL=0 → U0TXD/U0RXD (or U1 on 17/18)
	tx.ioMuxReg().Set(2 << esp.IO_MUX_GPIO_FUN_DRV_Pos)
	rx.ioMuxReg().Set(esp.IO_MUX_GPIO_FUN_IE | esp.IO_MUX_GPIO_FUN_WPU | (2 << esp.IO_MUX_GPIO_FUN_DRV_Pos))
	if tx < 32 {
		esp.GPIO.ENABLE_W1TS.Set(1 << tx)
	} else {
		esp.GPIO.ENABLE1_W1TS.Set(1 << (tx - 32))
	}
	if rx < 32 {
		esp.GPIO.ENABLE_W1TC.Set(1 << rx)
	} else {
		esp.GPIO.ENABLE1_W1TC.Set(1 << (rx - 32))
	}
}

func (uart *UART) configureInterrupt(intrMapReg *volatile.Register32) {
	uart.Bus.INT_ENA.ClearBits(0x0ffff)
	intrMapReg.Set(cpuInterruptFromUART)
	onceUart.Do(func() {
		_ = interrupt.New(cpuInterruptFromUART, handleUARTInterrupt).Enable()
	})
	// espradio/WiFi blob clears INTENABLE bits; re-arm on every Configure.
	_ = interrupt.New(cpuInterruptFromUART, handleUARTInterrupt).Enable()
}

func handleUARTInterrupt(interrupt.Interrupt) {
	UART0.serveInterrupt()
	UART1.serveInterrupt()
	UART2.serveInterrupt()
}

func (uart *UART) serveInterrupt() {
	flag := uart.Bus.INT_ST.Get()
	if flag&uartInterrupts == 0 {
		return
	}
	uart.Bus.INT_ENA.ClearBits(uartInterrupts)
	// Always drain FIFO on any RX-related IRQ (not only FULL).
	for uart.Bus.GetSTATUS_RXFIFO_CNT() > 0 {
		b := uart.Bus.GetFIFO_RXFIFO_RD_BYTE()
		if !uart.Buffer.Put(byte(b)) {
			uart.DataOverflowDetected = true
		}
	}
	if flag&esp.UART_INT_ENA_PARITY_ERR_INT_ENA != 0 {
		uart.ParityErrorDetected = true
	}
	if flag&esp.UART_INT_ENA_FRM_ERR_INT_ENA != 0 {
		uart.DataErrorDetected = true
	}
	if flag&esp.UART_INT_ENA_RXFIFO_OVF_INT_ENA != 0 {
		uart.DataOverflowDetected = true
	}
	if flag&esp.UART_INT_ENA_GLITCH_DET_INT_ENA != 0 {
		uart.DataErrorDetected = true
	}
	uart.Bus.INT_CLR.SetBits(flag)
	uart.Bus.INT_CLR.ClearBits(flag)
	uart.Bus.INT_ENA.Set(uartInterrupts)
}

func (uart *UART) enableTransmitter() {
	uart.Bus.SetCONF0_TXFIFO_RST(1)
	uart.Bus.SetCONF0_TXFIFO_RST(0)
	uart.Bus.SetCONF1_TXFIFO_EMPTY_THRHD(10)
}

func (uart *UART) enableReceiver() {
	uart.Bus.SetCONF0_RXFIFO_RST(1)
	uart.Bus.SetCONF0_RXFIFO_RST(0)
	uart.Bus.SetCONF1_RXFIFO_FULL_THRHD(1)
	uart.Bus.SetMEM_CONF_RX_TOUT_THRHD(10)
	uart.Bus.SetCONF1_RX_TOUT_EN(1)
	uart.Bus.SetINT_ENA_RXFIFO_FULL_INT_ENA(1)
	uart.Bus.SetINT_ENA_RXFIFO_TOUT_INT_ENA(1)
	uart.Bus.SetINT_ENA_FRM_ERR_INT_ENA(1)
	uart.Bus.SetINT_ENA_PARITY_ERR_INT_ENA(1)
	uart.Bus.SetINT_ENA_GLITCH_DET_INT_ENA(1)
	uart.Bus.SetINT_ENA_RXFIFO_OVF_INT_ENA(1)
}

func (uart *UART) writeByte(b byte) error {
	for (uart.Bus.STATUS.Get()&esp.UART_STATUS_TXFIFO_CNT_Msk)>>esp.UART_STATUS_TXFIFO_CNT_Pos >= 128 {
	}
	uart.Bus.FIFO.Set(uint32(b))
	return nil
}

func (uart *UART) flush() {}
