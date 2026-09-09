//go:build tinygo && !modemonly

package main

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"net/url"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/dimajolkin/sms-telegram/firmware/swtls"
)

type Telegram struct {
	token  string
	client http.Client
	tr     *swtls.Transport
}

type Update struct {
	UpdateID int64      `json:"update_id"`
	Message  *TGMessage `json:"message"`
}

type TGMessage struct {
	MessageID int64  `json:"message_id"`
	Text      string `json:"text"`
	Chat      TGChat `json:"chat"`
}

type TGChat struct {
	ID int64 `json:"id"`
}

func (t *Telegram) apiURL(method string) string {
	return "https://api.telegram.org/bot" + t.token + "/" + method
}

func (t *Telegram) Send(chatID, text string) error {
	form := url.Values{}
	form.Set("chat_id", chatID)
	form.Set("text", text)
	req, err := http.NewRequest(http.MethodPost, t.apiURL("sendMessage"), strings.NewReader(form.Encode()))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("User-Agent", "sms-telegram/1.0")
	body, code, err := t.do(req, 512)
	if err != nil {
		return err
	}
	if code >= 300 {
		return errStr("telegram http " + strconv.Itoa(code) + " " + preview(body))
	}
	return nil
}

func (t *Telegram) GetUpdates(offset int64, timeoutSec int) ([]Update, int64, error) {
	q := url.Values{}
	q.Set("timeout", strconv.Itoa(timeoutSec))
	q.Set("limit", "1")
	if offset > 0 {
		q.Set("offset", strconv.FormatInt(offset, 10))
	}
	u := t.apiURL("getUpdates") + "?" + q.Encode()
	req, err := http.NewRequest(http.MethodGet, u, nil)
	if err != nil {
		return nil, offset, err
	}
	req.Header.Set("User-Agent", "sms-telegram/1.0")
	body, code, err := t.do(req, 4*1024)
	if err != nil {
		return nil, offset, err
	}
	if code >= 300 {
		return nil, offset, errStr("http " + strconv.Itoa(code) + " " + preview(body))
	}
	if len(body) == 0 {
		return nil, offset, errStr("empty body")
	}
	// пустой poll без json.Unmarshal — экономит heap
	if bytes.Contains(body, []byte(`"result":[]`)) {
		return nil, offset, nil
	}
	var parsed struct {
		OK          bool     `json:"ok"`
		Result      []Update `json:"result"`
		Description string   `json:"description"`
	}
	if err := json.Unmarshal(body, &parsed); err != nil {
		return nil, offset, errStr(err.Error() + " " + preview(body))
	}
	if !parsed.OK {
		return nil, offset, errStr("api: " + parsed.Description)
	}
	next := offset
	for _, u := range parsed.Result {
		if u.UpdateID >= next {
			next = u.UpdateID + 1
		}
	}
	return parsed.Result, next, nil
}

func (t *Telegram) Ping() error {
	req, err := http.NewRequest(http.MethodGet, t.apiURL("getMe"), nil)
	if err != nil {
		return err
	}
	req.Header.Set("User-Agent", "sms-telegram/1.0")
	body, code, err := t.do(req, 512)
	if err != nil {
		return err
	}
	if code >= 300 {
		return errStr("getMe http " + strconv.Itoa(code) + " " + preview(body))
	}
	if len(body) == 0 || body[0] != '{' {
		return errStr("getMe not json " + preview(body))
	}
	println("tg getMe:", preview(body))
	return nil
}

// SetMyCommands ставит меню «/» из botCommands.
func (t *Telegram) SetMyCommands() error {
	// компактный JSON без encoding/json — меньше heap
	var b strings.Builder
	b.WriteString(`{"commands":[`)
	for i, c := range botCommands {
		if i > 0 {
			b.WriteByte(',')
		}
		b.WriteString(`{"command":"`)
		b.WriteString(c.Cmd)
		b.WriteString(`","description":"`)
		b.WriteString(escapeJSON(c.Desc))
		b.WriteString(`"}`)
	}
	b.WriteString(`]}`)
	payload := b.String()
	req, err := http.NewRequest(http.MethodPost, t.apiURL("setMyCommands"), strings.NewReader(payload))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("User-Agent", "sms-telegram/1.0")
	body, code, err := t.do(req, 256)
	if err != nil {
		return err
	}
	if code >= 300 {
		return errStr("setMyCommands http " + strconv.Itoa(code) + " " + preview(body))
	}
	return nil
}

func escapeJSON(s string) string {
	s = strings.ReplaceAll(s, `\`, `\\`)
	s = strings.ReplaceAll(s, `"`, `\"`)
	return s
}

func (t *Telegram) do(req *http.Request, maxBody int) ([]byte, int, error) {
	t.ensureClient()
	resp, err := t.client.Do(req)
	if err != nil {
		t.tr.CloseIdle()
		runtime.GC()
		return nil, 0, err
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, int64(maxBody)))
	_ = resp.Body.Close()
	// Не держим keep-alive: Conn+bufio держат десятки KB, UI/модем не влезают.
	t.tr.CloseIdle()
	runtime.GC()
	if err != nil {
		return nil, resp.StatusCode, err
	}
	return body, resp.StatusCode, nil
}

func (t *Telegram) ensureClient() {
	if t.tr == nil {
		t.tr = &swtls.Transport{Timeout: 45 * time.Second}
		t.client.Transport = t.tr
		t.client.Timeout = 45 * time.Second
	}
}

func preview(b []byte) string {
	s := string(b)
	s = strings.ReplaceAll(s, "\n", " ")
	s = strings.ReplaceAll(s, "\r", "")
	if len(s) > 80 {
		s = s[:80] + "…"
	}
	return strconv.Quote(s)
}
