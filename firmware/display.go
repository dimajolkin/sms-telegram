//go:build tinygo && !modemonly

package main

import (
	"image/color"
	"machine"
	"strconv"
	"time"

	"tinygo.org/x/drivers"
	"tinygo.org/x/drivers/st7789"
	"tinygo.org/x/tinyfont"
	"tinygo.org/x/tinyfont/freemono"
	"tinygo.org/x/tinyfont/proggy"
)

const (
	panelW int16 = 135
	panelH int16 = 240
)

// Nokia-ish phosphor LCD palette.
var (
	colBg     = color.RGBA{R: 12, G: 28, B: 14, A: 255}
	colFg     = color.RGBA{R: 170, G: 220, B: 120, A: 255}
	colMuted  = color.RGBA{R: 70, G: 110, B: 60, A: 255}
	colOk     = color.RGBA{R: 190, G: 240, B: 140, A: 255}
	colWarn   = color.RGBA{R: 200, G: 180, B: 60, A: 255}
	colAccent = color.RGBA{R: 150, G: 210, B: 100, A: 255}
	colBarOn  = color.RGBA{R: 180, G: 230, B: 130, A: 255}
	colBarOff = color.RGBA{R: 35, G: 55, B: 35, A: 255}
	colSoft   = color.RGBA{R: 40, G: 70, B: 40, A: 255}
)

type Display struct {
	dev  st7789.Device
	font tinyfont.Fonter
	w, h int16
}

type StatusView struct {
	WiFiOK   bool
	Bars     int
	Operator string
	SMS      int
	Missed   int
	Clock    string
	Alive    int // 0..3 pulse
	HintL    string
	HintR    string
}

func NewDisplay() *Display {
	if err := machine.SPI0.Configure(machine.SPIConfig{
		Frequency: 20_000_000,
		SCK:       pinTFTSCK,
		SDO:       pinTFTMOSI,
		SDI:       machine.NoPin,
		Mode:      0,
	}); err != nil {
		println("spi:", err.Error())
	}
	d := st7789.New(machine.SPI0, pinTFTRST, pinTFTDC, pinTFTCS, pinTFTBL)
	d.Configure(st7789.Config{
		Width:        panelW,
		Height:       panelH,
		Rotation:     drivers.Rotation90,
		RowOffset:    40,
		ColumnOffset: 53,
	})
	disp := &Display{
		dev:  d,
		font: &proggy.TinySZ8pt7b,
	}
	disp.w, disp.h = d.Size()
	disp.dev.FillScreen(colBg)
	return disp
}

func (d *Display) Clear() {
	d.dev.FillScreen(colBg)
}

func (d *Display) Sleep() {
	d.dev.EnableBacklight(false)
}

func (d *Display) Wake() {
	d.dev.EnableBacklight(true)
}

func (d *Display) line(x, y int16, s string, c color.RGBA) {
	tinyfont.WriteLine(&d.dev, d.font, x, y, s, c)
}

func (d *Display) DrawBoot(msg string) {
	d.Clear()
	d.line(8, 20, "NOKIA-ish", colAccent)
	d.line(8, 48, "SMS GATEWAY", colFg)
	if msg == "" {
		msg = "…"
	}
	d.line(8, 80, msg, colMuted)
	d.drawSoftkeys(" ", " ")
}

func (d *Display) DrawStatus(v StatusView) {
	d.Clear()

	drawAntenna(&d.dev, 6, 8, v.Bars)
	wifi := "WIFI-"
	wc := colWarn
	if v.WiFiOK {
		wifi = "WIFI+"
		wc = colOk
	}
	d.line(70, 18, wifi, wc)

	clk := v.Clock
	if clk == "" {
		clk = "--:--"
	}
	_, clkW := tinyfont.LineWidth(d.font, clk)
	d.line(d.w-18-int16(clkW)-6, 16, clk, colFg)
	drawAlive(&d.dev, d.w-18, 10, v.Alive)

	op := v.Operator
	if op == "" {
		op = "—"
	}
	if len(op) > 22 {
		op = op[:22]
	}
	d.line(8, 48, op, colMuted)

	sms := "?"
	if v.SMS >= 0 {
		sms = strconv.Itoa(v.SMS)
	}
	miss := "?"
	if v.Missed >= 0 {
		miss = strconv.Itoa(v.Missed)
	}
	big := &freemono.Bold12pt7b
	d.line(8, 68, "SMS", colMuted)
	d.line(120, 68, "CALLS", colMuted)
	tinyfont.WriteLine(&d.dev, big, 8, 98, sms, colFg)
	tinyfont.WriteLine(&d.dev, big, 120, 98, miss, colFg)

	hl, hr := v.HintL, v.HintR
	if hl == "" {
		hl = "Inbox"
	}
	if hr == "" {
		hr = "Refresh"
	}
	d.drawSoftkeys(hl, hr)
}

func (d *Display) drawSoftkeys(left, right string) {
	y := d.h - 18
	d.dev.FillRectangle(0, y-4, d.w, 22, colSoft)
	d.line(8, y+10, left, colFg)
	// right-align roughly
	rx := d.w - int16(6*len(right)) - 8
	if rx < d.w/2 {
		rx = d.w/2 + 10
	}
	d.line(rx, y+10, right, colFg)
}

func (d *Display) DrawInbox(list []SMS, idx int) {
	d.Clear()
	d.line(8, 16, "INBOX", colAccent)
	if len(list) == 0 {
		d.line(8, 50, "(empty)", colMuted)
		d.drawSoftkeys(" ", "Back")
		return
	}
	if idx < 0 {
		idx = 0
	}
	if idx >= len(list) {
		idx = len(list) - 1
	}
	d.line(90, 16, strconv.Itoa(idx+1)+"/"+strconv.Itoa(len(list)), colMuted)

	start := idx
	if start > 0 && idx == len(list)-1 {
		start = idx - 1
	}
	y := int16(36)
	for i := start; i < len(list) && y < 100; i++ {
		c := colMuted
		prefix := "  "
		if i == idx {
			c = colFg
			prefix = "> "
		}
		from := list[i].From
		if len(from) > 18 {
			from = from[:18]
		}
		d.line(4, y, prefix+from, c)
		y += 12
		preview := list[i].Text
		if len(preview) > 28 {
			preview = preview[:28]
		}
		d.line(14, y, preview, colMuted)
		y += 16
		if i >= start+1 {
			break
		}
	}
	d.drawSoftkeys("Next", "Open")
}

func (d *Display) DrawSMSDetail(s SMS) {
	d.Clear()
	from := s.From
	if len(from) > 26 {
		from = from[:26]
	}
	d.line(8, 16, from, colAccent)

	text := s.Text
	y := int16(36)
	for len(text) > 0 && y < 100 {
		n := 32
		if n > len(text) {
			n = len(text)
		}
		chunk := text[:n]
		if i := indexByte(chunk, '\n'); i >= 0 {
			chunk = text[:i]
			text = text[i+1:]
		} else {
			text = text[n:]
		}
		d.line(8, y, chunk, colFg)
		y += 12
	}
	d.drawSoftkeys(" ", "Back")
}

func drawAntenna(dev *st7789.Device, x, y int16, bars int) {
	// stem
	dev.FillRectangle(x+2, y+2, 3, 14, colBarOn)
	for i := int16(0); i < 4; i++ {
		h := 4 + i*3
		c := colBarOff
		if int(i) < bars {
			c = colBarOn
		}
		dev.FillRectangle(x+8+i*9, y+(16-h), 6, h, c)
	}
}

func drawAlive(dev *st7789.Device, x, y int16, frame int) {
	// heartbeat block — blinks so you see the loop is alive
	c := colMuted
	if frame%2 == 0 {
		c = colOk
	}
	dev.FillRectangle(x, y, 10, 10, c)
	dev.FillRectangle(x+2, y+2, 6, 6, colBg)
	if frame%2 == 0 {
		dev.FillRectangle(x+3, y+3, 4, 4, c)
	}
}

func indexByte(s string, b byte) int {
	for i := 0; i < len(s); i++ {
		if s[i] == b {
			return i
		}
	}
	return -1
}

// formatClockHHMM for status screen.
func formatClockHHMM(t time.Time) string {
	h, m, s := t.Clock()
	// blink colon via odd/even second handled by caller if needed
	_ = s
	return twoDig(h) + ":" + twoDig(m)
}

func twoDig(n int) string {
	if n < 10 {
		return "0" + strconv.Itoa(n)
	}
	return strconv.Itoa(n)
}
