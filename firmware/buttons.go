//go:build tinygo && !modemonly

package main

import (
	"machine"
	"runtime/volatile"
	"time"
)

var (
	btnPendNext volatile.Register8
	btnPendOK   volatile.Register8
)

type Buttons struct {
	next, ok           machine.Pin
	lastNext, lastOK   bool // true = high / отпущена (pull-up)
	nextAt, okAt       time.Time
	debounce           time.Duration
}

func NewButtons() *Buttons {
	b := &Buttons{
		next:     pinBtnNext,
		ok:       pinBtnOK,
		debounce: 40 * time.Millisecond,
		lastNext: true,
		lastOK:   true,
	}
	b.next.Configure(machine.PinConfig{Mode: machine.PinInputPullup})
	b.ok.Configure(machine.PinConfig{Mode: machine.PinInputPullup})
	b.lastNext = b.next.Get()
	b.lastOK = b.ok.Get()

	// IRQ — бонус; основной путь — опрос уровня (WiFi может сбрасывать INTENABLE).
	_ = b.next.SetInterrupt(machine.PinFalling, func(machine.Pin) {
		btnPendNext.Set(1)
	})
	_ = b.ok.SetInterrupt(machine.PinFalling, func(machine.Pin) {
		btnPendOK.Set(1)
	})
	return b
}

func (b *Buttons) Pending() bool {
	if btnPendNext.Get() != 0 || btnPendOK.Get() != 0 {
		return true
	}
	// зажата прямо сейчас
	return !b.next.Get() || !b.ok.Get()
}

// Poll: active-low на GND. IRQ-флаг или фронт high→low по Get().
func (b *Buttons) Poll() (pressedNext, pressedOK bool) {
	now := time.Now()

	if btnPendNext.Get() != 0 {
		btnPendNext.Set(0)
		if now.Sub(b.nextAt) > b.debounce {
			pressedNext = true
			b.nextAt = now
			b.lastNext = false
		}
	}
	if btnPendOK.Get() != 0 {
		btnPendOK.Set(0)
		if now.Sub(b.okAt) > b.debounce {
			pressedOK = true
			b.okAt = now
			b.lastOK = false
		}
	}

	n := b.next.Get()
	o := b.ok.Get()
	if !pressedNext && !n && b.lastNext && now.Sub(b.nextAt) > b.debounce {
		pressedNext = true
		b.nextAt = now
	}
	if !pressedOK && !o && b.lastOK && now.Sub(b.okAt) > b.debounce {
		pressedOK = true
		b.okAt = now
	}
	b.lastNext = n
	b.lastOK = o
	return
}
