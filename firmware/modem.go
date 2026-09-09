//go:build tinygo

package main

import (
	"machine"
	"strconv"
	"strings"
	"time"
)

type SMS struct {
	Index int
	From  string
	Text  string
}

type serialBus interface {
	WriteByte(c byte) error
	ReadByte() (byte, error)
	Buffered() int
}

type Modem struct {
	uart serialBus
}

func (m *Modem) Init() error {
	m.ResetHW()
	if err := m.syncAT(12); err != nil {
		return err
	}
	println("modem synced")
	_, _ = m.AT("ATE0", 2*time.Second)
	if _, err := m.AT("AT+CMGF=1", 3*time.Second); err != nil {
		return err
	}
	_, _ = m.AT(`AT+CSCS="GSM"`, 3*time.Second)
	_, _ = m.AT("AT+CNMI=2,1,0,0,0", 3*time.Second)
	if err := m.WaitNetwork(90 * time.Second); err != nil {
		return err
	}
	return m.SelfTest()
}

// SelfTest — обязательные AT до выхода из stage1 (не только один AT).
func (m *Modem) SelfTest() error {
	println("modem selftest…")
	checks := []struct {
		cmd string
		ok  string // подстрока в ответе; пусто = любой OK
	}{
		{"AT", ""},
		{"ATI", ""},
		{"AT+CSQ", "+CSQ:"},
		{"AT+CREG?", "+CREG:"},
		{"AT+CPMS?", "+CPMS:"},
		{"AT+CMGF?", "+CMGF:"},
	}
	for _, c := range checks {
		resp, err := m.AT(c.cmd, 4*time.Second)
		if err != nil {
			println("modem selftest FAIL", c.cmd, err.Error())
			return err
		}
		if c.ok != "" && !strings.Contains(resp, c.ok) {
			println("modem selftest FAIL", c.cmd, strconv.Quote(resp))
			return errStr("selftest " + c.cmd)
		}
		println("modem selftest OK", c.cmd)
	}
	// три подряд AT — как keepalive
	for i := 0; i < 3; i++ {
		if _, err := m.AT("AT", 2*time.Second); err != nil {
			println("modem selftest keepalive FAIL", err.Error())
			return err
		}
	}
	println("modem selftest OK keepalive×3")
	return nil
}

// ResetHW: RST idle HIGH (active low). Без импульса — uart-test работал так;
// короткий LOW ронял модем.
func (m *Modem) ResetHW() {
	pinModemRST.Configure(machine.PinConfig{Mode: machine.PinOutput})
	pinModemRST.High()
	println("modem RST idle HIGH")
}

// ResetHWPulse — явный сброс, если понадобится.
func (m *Modem) ResetHWPulse() {
	pinModemRST.Configure(machine.PinConfig{Mode: machine.PinOutput})
	println("modem RST pulse")
	pinModemRST.Low()
	time.Sleep(200 * time.Millisecond)
	pinModemRST.High()
	time.Sleep(4 * time.Second)
}

// syncAT — UART0 IO_MUX: D6=TX→SIM RX, D7=RX←SIM TX @ 9600.
func (m *Modem) syncAT(tries int) error {
	n := tries
	if n < 1 {
		n = 20
	}
	if n > 20 {
		n = 20
	}
	var last string
	for i := 0; i < n; i++ {
		m.drain()
		m.writeRaw("AT\r")
		resp, err := m.readUntilOK(1200 * time.Millisecond)
		last = resp
		if err == nil {
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	if last != "" {
		println("modem sync fail rx:", strconv.Quote(last))
	} else {
		println("modem sync fail: no rx")
	}
	return errStr("AT timeout")
}

func (m *Modem) WaitNetwork(timeout time.Duration) error {
	println("modem wait network…")
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		resp, err := m.AT("AT+CREG?", 3*time.Second)
		if err == nil && (strings.Contains(resp, ",1") || strings.Contains(resp, ",5")) {
			println("modem network OK", strconv.Quote(strings.TrimSpace(resp)))
			return nil
		}
		time.Sleep(2 * time.Second)
	}
	return errStr("network timeout")
}

func (m *Modem) ReadUnreadSMS() ([]SMS, error) {
	resp, err := m.AT(`AT+CMGL="REC UNREAD"`, 10*time.Second)
	if err != nil {
		return nil, err
	}
	return parseCMGL(resp), nil
}

func (m *Modem) ListSMS(limit int) ([]SMS, error) {
	resp, err := m.AT(`AT+CMGL="ALL"`, 15*time.Second)
	if err != nil {
		return nil, err
	}
	all := parseCMGL(resp)
	if limit > 0 && len(all) > limit {
		return all[len(all)-limit:], nil
	}
	return all, nil
}

// SignalQuality returns CSQ 0–31 (99 = unknown) and bars 0–4.
func (m *Modem) SignalQuality() (csq int, bars int, err error) {
	resp, err := m.AT("AT+CSQ", 3*time.Second)
	if err != nil {
		return 99, 0, err
	}
	csq = 99
	if i := strings.Index(resp, "+CSQ:"); i >= 0 {
		rest := strings.TrimSpace(resp[i+5:])
		parts := strings.Split(rest, ",")
		if len(parts) > 0 {
			n, e := strconv.Atoi(strings.TrimSpace(parts[0]))
			if e == nil {
				csq = n
			}
		}
	}
	bars = csqToBars(csq)
	return csq, bars, nil
}

func csqToBars(csq int) int {
	if csq == 99 || csq < 0 {
		return 0
	}
	switch {
	case csq >= 20:
		return 4
	case csq >= 15:
		return 3
	case csq >= 10:
		return 2
	case csq >= 5:
		return 1
	default:
		return 0
	}
}

func (m *Modem) Operator() string {
	resp, err := m.AT("AT+COPS?", 5*time.Second)
	if err != nil {
		return "?"
	}
	// +COPS: 0,0,"MTS"
	if i := strings.Index(resp, "\""); i >= 0 {
		rest := resp[i+1:]
		if j := strings.Index(rest, "\""); j >= 0 {
			name := rest[:j]
			if name != "" {
				return name
			}
		}
	}
	creg, _ := m.AT("AT+CREG?", 3*time.Second)
	if strings.Contains(creg, ",1") || strings.Contains(creg, ",5") {
		return "registered"
	}
	return "no net"
}

func (m *Modem) SMSCount() int {
	// CPMS вместо CMGL ALL — иначе UI блокирует UART на секунды и ломает следующий AT.
	resp, err := m.AT("AT+CPMS?", 3*time.Second)
	if err != nil {
		return -1
	}
	// +CPMS: "SM",used,total,...
	if i := strings.Index(resp, "+CPMS:"); i >= 0 {
		rest := resp[i+6:]
		parts := strings.Split(rest, ",")
		if len(parts) >= 2 {
			n, e := strconv.Atoi(strings.TrimSpace(parts[1]))
			if e == nil {
				return n
			}
		}
	}
	return -1
}

// MissedCount — used entries in MC phonebook (без полного CPBR).
func (m *Modem) MissedCount() int {
	if _, err := m.AT(`AT+CPBS="MC"`, 3*time.Second); err != nil {
		_, _ = m.AT(`AT+CPBS="SM"`, 3*time.Second)
		return -1
	}
	resp, err := m.AT("AT+CPBS?", 3*time.Second)
	_, _ = m.AT(`AT+CPBS="SM"`, 3*time.Second)
	if err != nil {
		return -1
	}
	// +CPBS: "MC",used,total
	if i := strings.Index(resp, "+CPBS:"); i >= 0 {
		rest := resp[i+6:]
		parts := strings.Split(rest, ",")
		if len(parts) >= 2 {
			n, e := strconv.Atoi(strings.TrimSpace(parts[1]))
			if e == nil {
				return n
			}
		}
	}
	return -1
}

func (m *Modem) DeleteSMS(index int) error {
	_, err := m.AT("AT+CMGD="+strconv.Itoa(index), 5*time.Second)
	return err
}

type MissedCall struct {
	Index  int
	Number string
	Name   string
}

// ListMissedCalls — телефонная книга MC (missed calls) на модуле.
func (m *Modem) ListMissedCalls(limit int) ([]MissedCall, error) {
	if limit <= 0 {
		limit = 20
	}
	if _, err := m.AT(`AT+CPBS="MC"`, 3*time.Second); err != nil {
		return nil, err
	}
	resp, err := m.AT("AT+CPBR=1,"+strconv.Itoa(limit), 10*time.Second)
	// вернуть SMS-хранилище, чтобы не ломать CMGL
	_, _ = m.AT(`AT+CPBS="SM"`, 3*time.Second)
	if err != nil {
		return nil, err
	}
	return parseCPBR(resp), nil
}

// +CPBR: 1,"+7900…",145,"name"
func parseCPBR(resp string) []MissedCall {
	var out []MissedCall
	for _, line := range strings.Split(resp, "\n") {
		line = strings.TrimSpace(strings.TrimSuffix(line, "\r"))
		if !strings.HasPrefix(line, "+CPBR:") {
			continue
		}
		rest := strings.TrimSpace(strings.TrimPrefix(line, "+CPBR:"))
		// index,"number",type,"name"
		idxComma := strings.Index(rest, ",")
		if idxComma < 0 {
			continue
		}
		idx, err := strconv.Atoi(strings.TrimSpace(rest[:idxComma]))
		if err != nil {
			continue
		}
		rest = rest[idxComma+1:]
		q1 := strings.Index(rest, `"`)
		if q1 < 0 {
			continue
		}
		q2 := strings.Index(rest[q1+1:], `"`)
		if q2 < 0 {
			continue
		}
		num := rest[q1+1 : q1+1+q2]
		rest = rest[q1+1+q2+1:]
		name := ""
		if q3 := strings.Index(rest, `"`); q3 >= 0 {
			rest2 := rest[q3+1:]
			if q4 := strings.Index(rest2, `"`); q4 >= 0 {
				name = rest2[:q4]
			}
		}
		out = append(out, MissedCall{Index: idx, Number: num, Name: name})
	}
	return out
}

func (m *Modem) SendSMS(number, text string) error {
	m.drain()
	m.writeRaw("AT+CMGS=\"" + number + "\"\r")
	deadline := time.Now().Add(8 * time.Second)
	var buf strings.Builder
	for time.Now().Before(deadline) {
		for m.uart.Buffered() > 0 {
			b, err := m.uart.ReadByte()
			if err != nil {
				break
			}
			buf.WriteByte(b)
			if b == '>' {
				m.writeRaw(text)
				m.writeRaw(string(rune(0x1A)))
				resp, err := m.readUntilOK(60 * time.Second)
				if err != nil {
					return err
				}
				if strings.Contains(resp, "ERROR") {
					return errStr("CMGS ERROR")
				}
				return nil
			}
		}
		time.Sleep(20 * time.Millisecond)
	}
	return errStr("CMGS prompt timeout")
}

func (m *Modem) USSD(code string) (string, error) {
	// dcs 15 = GSM 7-bit; ответ оператора часто всё равно приходит UCS2 hex.
	cmd := `AT+CUSD=1,"` + code + `",15`
	m.drain()
	m.writeRaw(cmd + "\r")
	deadline := time.Now().Add(45 * time.Second)
	var buf strings.Builder
	gotOK := false
	for time.Now().Before(deadline) {
		for m.uart.Buffered() > 0 {
			b, err := m.uart.ReadByte()
			if err != nil {
				break
			}
			buf.WriteByte(b)
		}
		s := buf.String()
		if strings.Contains(s, "ERROR") && !strings.Contains(s, "+CUSD:") {
			return s, errStr("AT ERROR")
		}
		if text, ok := parseCUSD(s); ok {
			return text, nil
		}
		if !gotOK && (strings.Contains(s, "\nOK") || strings.Contains(s, "\r\nOK")) {
			gotOK = true
		}
		time.Sleep(20 * time.Millisecond)
	}
	if gotOK {
		return "", errStr("USSD: OK без +CUSD")
	}
	return buf.String(), errStr("AT timeout")
}

// parseCUSD ждёт полную строку +CUSD: …\r\n и достаёт текст (с UCS2 hex → UTF-8).
func parseCUSD(s string) (string, bool) {
	i := strings.Index(s, "+CUSD:")
	if i < 0 {
		return "", false
	}
	line := s[i:]
	eol := strings.IndexAny(line, "\r\n")
	if eol < 0 {
		return "", false // строка ещё не дочитана
	}
	line = line[:eol]

	// +CUSD: <m>,"text",<dcs>
	q1 := strings.Index(line, `"`)
	if q1 < 0 {
		return strings.TrimSpace(line), true
	}
	q2 := strings.Index(line[q1+1:], `"`)
	if q2 < 0 {
		return "", false // кавычка не закрыта
	}
	raw := line[q1+1 : q1+1+q2]
	if raw == "" {
		return strings.TrimSpace(line), true
	}
	return decodeUSSDText(raw), true
}

func decodeUSSDText(s string) string {
	if decoded, ok := decodeUCS2Hex(s); ok {
		return decoded
	}
	return s
}

func decodeUCS2Hex(s string) (string, bool) {
	if len(s) < 4 || len(s)%4 != 0 {
		return "", false
	}
	for i := 0; i < len(s); i++ {
		c := s[i]
		switch {
		case c >= '0' && c <= '9', c >= 'A' && c <= 'F', c >= 'a' && c <= 'f':
		default:
			return "", false
		}
	}
	out := make([]rune, 0, len(s)/4)
	for i := 0; i+4 <= len(s); i += 4 {
		v, err := strconv.ParseUint(s[i:i+4], 16, 16)
		if err != nil {
			return "", false
		}
		out = append(out, rune(v))
	}
	return string(out), true
}

func (m *Modem) AT(cmd string, timeout time.Duration) (string, error) {
	m.drain()
	m.writeRaw(cmd + "\r") // SIM800L: только CR
	resp, err := m.readUntilOK(timeout)
	if err != nil && err.Error() == "AT timeout" {
		_ = rebindModemUART(m, uartBaud)
		m.drain()
		m.writeRaw(cmd + "\r")
		return m.readUntilOK(timeout)
	}
	return resp, err
}

func (m *Modem) readUntilOK(timeout time.Duration) (string, error) {
	deadline := time.Now().Add(timeout)
	var buf strings.Builder
	for time.Now().Before(deadline) {
		for m.uart.Buffered() > 0 {
			b, err := m.uart.ReadByte()
			if err != nil {
				break
			}
			buf.WriteByte(b)
		}
		s := buf.String()
		if strings.Contains(s, "\nOK") || strings.HasSuffix(s, "OK\r") || strings.Contains(s, "\r\nOK") {
			return s, nil
		}
		if strings.Contains(s, "ERROR") {
			return s, errStr("AT ERROR")
		}
		time.Sleep(5 * time.Millisecond)
	}
	return buf.String(), errStr("AT timeout")
}

func (m *Modem) writeRaw(s string) {
	for i := 0; i < len(s); i++ {
		_ = m.uart.WriteByte(s[i])
	}
}

func (m *Modem) drain() {
	for m.uart.Buffered() > 0 {
		_, _ = m.uart.ReadByte()
	}
}

func parseCMGL(resp string) []SMS {
	lines := strings.Split(resp, "\n")
	var out []SMS
	var cur *SMS
	for _, raw := range lines {
		line := strings.TrimSpace(strings.TrimSuffix(raw, "\r"))
		if strings.HasPrefix(line, "+CMGL:") {
			if cur != nil {
				out = append(out, *cur)
			}
			cur = &SMS{}
			parts := strings.Split(line, ",")
			if len(parts) >= 3 {
				idxPart := strings.TrimSpace(strings.TrimPrefix(parts[0], "+CMGL:"))
				if n, err := strconv.Atoi(idxPart); err == nil {
					cur.Index = n
				}
				cur.From = strings.Trim(parts[2], `"`)
			}
			continue
		}
		if cur != nil && line != "" && line != "OK" && !strings.HasPrefix(line, "AT") {
			if cur.Text == "" {
				cur.Text = line
			} else {
				cur.Text += "\n" + line
			}
		}
	}
	if cur != nil && cur.Text != "" {
		out = append(out, *cur)
	}
	return out
}

type atError string

func (e atError) Error() string { return string(e) }

func errStr(s string) error { return atError(s) }
