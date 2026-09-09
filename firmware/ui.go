//go:build tinygo && !modemonly

package main

import "time"

type UIScreen uint8

const (
	screenStatus UIScreen = iota
	screenInbox
	screenDetail
)

const screenTimeout = 30 * time.Second

type UI struct {
	disp   *Display
	btns   *Buttons
	modem  *Modem
	screen UIScreen

	wifiOK      bool
	bars        int
	operator    string
	smsCount    int
	missedCount int
	alive       int
	dirty       bool
	asleep      bool
	lastPoll    time.Time
	lastAlive   time.Time
	lastActive  time.Time

	inbox    []SMS
	inboxIdx int
}

func NewUI(disp *Display, btns *Buttons, modem *Modem) *UI {
	now := time.Now()
	return &UI{
		disp:        disp,
		btns:        btns,
		modem:       modem,
		screen:      screenStatus,
		dirty:       true,
		smsCount:    -1,
		missedCount: -1,
		lastActive:  now,
	}
}

func (u *UI) Wake() {
	u.lastActive = time.Now()
	if !u.asleep {
		return
	}
	u.asleep = false
	u.disp.Wake()
	u.dirty = true
}

func (u *UI) sleepScreen() {
	if u.asleep {
		return
	}
	u.asleep = true
	u.disp.Sleep()
	u.dirty = false
}

func (u *UI) SetWiFi(ok bool) {
	if u.wifiOK != ok {
		u.wifiOK = ok
		if !u.asleep {
			u.dirty = true
		}
	}
}

func (u *UI) RefreshStatus() {
	_, bars, err := u.modem.SignalQuality()
	if err == nil {
		u.bars = bars
	}
	u.operator = u.modem.Operator()
	u.smsCount = u.modem.SMSCount()
	u.missedCount = u.modem.MissedCount()
	u.lastPoll = time.Now()
	if !u.asleep {
		u.dirty = true
	}
}

func (u *UI) loadInbox() {
	list, err := u.modem.ListSMS(8)
	if err != nil {
		u.inbox = nil
	} else {
		u.inbox = list
	}
	u.inboxIdx = 0
	if len(u.inbox) > 0 {
		u.inboxIdx = len(u.inbox) - 1
	}
	u.dirty = true
}

func (u *UI) Tick() {
	n, o := u.btns.Poll()
	now := time.Now()

	if u.asleep {
		if n || o {
			u.Wake()
			n, o = false, false
		} else {
			if u.screen == screenStatus && now.Sub(u.lastPoll) > 15*time.Second {
				u.RefreshStatus()
			}
			return
		}
	}

	if n || o {
		u.lastActive = now
	} else if now.Sub(u.lastActive) > screenTimeout {
		u.sleepScreen()
		return
	}

	// alive pulse ~2 Hz on home screen
	if u.screen == screenStatus && now.Sub(u.lastAlive) > 500*time.Millisecond {
		u.alive = (u.alive + 1) & 3
		u.lastAlive = now
		u.dirty = true
	}

	switch u.screen {
	case screenStatus:
		if n {
			u.loadInbox()
			u.screen = screenInbox
			u.dirty = true
		}
		if o {
			u.RefreshStatus()
		}
		if now.Sub(u.lastPoll) > 15*time.Second {
			u.RefreshStatus()
		}
	case screenInbox:
		if n && len(u.inbox) > 0 {
			u.inboxIdx++
			if u.inboxIdx >= len(u.inbox) {
				u.inboxIdx = 0
			}
			u.dirty = true
		}
		if o {
			if len(u.inbox) == 0 {
				u.screen = screenStatus
				u.dirty = true
			} else {
				u.screen = screenDetail
				u.dirty = true
			}
		}
	case screenDetail:
		if o || n {
			u.screen = screenInbox
			u.dirty = true
		}
	}

	if !u.dirty {
		return
	}
	u.dirty = false
	switch u.screen {
	case screenStatus:
		clk := formatClockHHMM(now)
		// blink colon
		if u.alive%2 == 1 {
			clk = twoDig(now.Hour()) + " " + twoDig(now.Minute())
		}
		u.disp.DrawStatus(StatusView{
			WiFiOK:   u.wifiOK,
			Bars:     u.bars,
			Operator: u.operator,
			SMS:      u.smsCount,
			Missed:   u.missedCount,
			Clock:    clk,
			Alive:    u.alive,
			HintL:    "Inbox",
			HintR:    "Refresh",
		})
	case screenInbox:
		u.disp.DrawInbox(u.inbox, u.inboxIdx)
	case screenDetail:
		if len(u.inbox) > 0 && u.inboxIdx < len(u.inbox) {
			u.disp.DrawSMSDetail(u.inbox[u.inboxIdx])
		}
	}
}
