//go:build tinygo && !modemonly

package main

import (
	"machine"
	"time"
)

type Buttons struct {
	next, ok           machine.Pin
	lastNext, lastOK   bool
	nextAt, okAt       time.Time
	debounce           time.Duration
}

func NewButtons() *Buttons {
	b := &Buttons{
		next:     pinBtnNext,
		ok:       pinBtnOK,
		debounce: 40 * time.Millisecond,
	}
	b.next.Configure(machine.PinConfig{Mode: machine.PinInputPullup})
	b.ok.Configure(machine.PinConfig{Mode: machine.PinInputPullup})
	b.lastNext = true
	b.lastOK = true
	return b
}

// Poll returns pressedNext, pressedOK (edge: release→press, active low).
func (b *Buttons) Poll() (pressedNext, pressedOK bool) {
	now := time.Now()
	n := b.next.Get()
	o := b.ok.Get()

	if !n && b.lastNext && now.Sub(b.nextAt) > b.debounce {
		pressedNext = true
		b.nextAt = now
	}
	if !o && b.lastOK && now.Sub(b.okAt) > b.debounce {
		pressedOK = true
		b.okAt = now
	}
	b.lastNext = n
	b.lastOK = o
	return
}
