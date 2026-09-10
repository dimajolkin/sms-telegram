//go:build tinygo && !modemonly

// Seeed XIAO ESP32S3 + SIM800L + ST7789V: SMS/USSD ↔ Telegram.
//
//	make flash
//
// Пины: firmware/pins_xiao_esp32s3.go (D0…D10).
package main

import (
	"machine"
	"runtime"
	"strconv"
	"strings"
	"time"

	"tinygo.org/x/drivers/netdev"
	nl "tinygo.org/x/drivers/netlink"
	link "tinygo.org/x/espradio/netlink"
)

// Injected via -ldflags -X
var (
	ssid     = ""
	password = ""
	botToken = ""
	chatID   = ""
)

const (
	uartBaud      = 9600
	smsPollEvery  = 3 * time.Second
)

func main() {
	time.Sleep(500 * time.Millisecond)
	println("sms-telegram boot")
	println("stages: 1=display  2=WiFi  3=UART+modem  4=Telegram")

	if ssid == "" || botToken == "" {
		fail("set WIFI_SSID / TELEGRAM_BOT_TOKEN via make flash ldflags")
	}

	pinModemRST.Configure(machine.PinConfig{Mode: machine.PinOutput})
	pinModemRST.High()

	// --- stage 1: дисплей (до WiFi, без UART) ---
	println("--- stage1 SPI ---")
	disp := NewDisplay()
	disp.DrawBoot("wifi…")
	println("--- stage1 OK display ---")

	// --- stage 2: WiFi (blob трогает INTENABLE — UART ещё не поднимаем) ---
	println("--- stage2 WiFi ---")
	wifi := link.Esplink{}
	netdev.UseNetdev(&wifi)
	connectWiFi(&wifi)
	disp.DrawBoot("ntp…")
	syncNTP()
	disp.DrawBoot("modem…")
	println("--- stage2 OK wifi ---")

	// --- stage 3: UART + модем после WiFi ---
	println("--- stage3 UART ---")
	uart, err := openModemUART(uartBaud)
	if err != nil {
		fail("uart: " + err.Error())
	}
	println("modem uart: UART0 IO_MUX D6/D7 @", uartBaud)
	modem := &Modem{uart: uart}
	for {
		println("modem init…")
		if err := modem.Init(); err != nil {
			println("modem init:", err.Error())
			time.Sleep(2 * time.Second)
			continue
		}
		break
	}
	disp.DrawBoot("telegram…")
	println("--- stage3 OK modem ---")

	// --- stage 4: Telegram ---
	println("--- stage4 Telegram ---")
	tg := &Telegram{token: botToken}
	if err := tg.Ping(); err != nil {
		println("tg ping:", err.Error())
	} else {
		println("tg ok")
	}
	runtime.GC()
	if err := tg.SetMyCommands(); err != nil {
		println("tg menu:", err.Error())
	} else {
		println("tg menu ok")
	}
	runtime.GC()
	printMem("after ping")

	offset, tgFails := int64(0), 0
	if updates, next, err := tg.GetUpdates(0, 0); err != nil {
		println("tg poll:", err.Error())
	} else {
		println("tg poll ok")
		offset = next
		for _, u := range updates {
			handleUpdate(tg, modem, u)
		}
	}
	runtime.GC()
	printMem("after poll")

	if chatID != "" {
		if err := tg.Send(chatID, "ESP online. /help"); err != nil {
			println("tg send:", err.Error())
		}
		runtime.GC()
		printMem("after hello")
	}

	btns := NewButtons()
	ui := NewUI(disp, btns, modem)
	ui.SetWiFi(true)
	ui.RefreshStatus()
	ui.Tick()
	runtime.GC()
	printMem("after ui")
	println("NumCPU", runtime.NumCPU())
	println("--- stage4 OK ready ---")

	var (
		smsFails   int
		lastSMSTry time.Time
		lastNet    time.Time
	)
	for {
		// ~2с только UI — иначе TLS/модем съедают весь цикл (tasks scheduler).
		uiUntil := time.Now().Add(2 * time.Second)
		for time.Now().Before(uiUntil) {
			ui.Tick()
			if chatID != "" {
				if num, ok := modem.PollMissedCall(); ok {
					println("missed call:", num)
					_ = tg.Send(chatID, "Пропущенный звонок\n\n"+num)
					ui.Wake()
					ui.RefreshStatus()
				}
			}
			if btns.Pending() {
				continue
			}
			time.Sleep(10 * time.Millisecond)
		}

		if chatID != "" && time.Since(lastSMSTry) > smsPollEvery {
			lastSMSTry = time.Now()
			msgs, err := modem.ReadUnreadSMS()
			if err != nil {
				smsFails++
				println("sms read:", err.Error())
				if smsFails >= 2 {
					_ = rebindModemUART(modem, uartBaud)
					smsFails = 0
				}
			} else {
				smsFails = 0
				for _, m := range msgs {
					text := "SMS from " + m.From + "\n\n" + m.Text
					if err := tg.Send(chatID, text); err != nil {
						println("tg send:", err.Error())
						continue
					}
					_ = modem.DeleteSMS(m.Index)
					ui.Wake()
					ui.RefreshStatus()
					runtime.GC()
				}
			}
			ui.Tick()
		}

		if time.Since(lastNet) > 1200*time.Millisecond {
			lastNet = time.Now()
			updates, next, err := tg.GetUpdates(offset, 0)
			if err != nil {
				println("tg poll:", err.Error())
				tgFails++
				ui.SetWiFi(tgFails < 3)
				if tgFails > 10 {
					tgFails = 5
				}
			} else {
				tgFails = 0
				ui.SetWiFi(true)
				offset = next
				for _, u := range updates {
					handleUpdate(tg, modem, u)
					runtime.GC()
				}
			}
			ui.Tick()
		}
	}
}

func connectWiFi(wifi *link.Esplink) {
	println("wifi connecting:", ssid)
	for {
		err := wifi.NetConnect(&nl.ConnectParams{
			Ssid:       ssid,
			Passphrase: password,
		})
		if err == nil {
			println("wifi ok")
			return
		}
		println("wifi:", err.Error())
		time.Sleep(3 * time.Second)
	}
}

func handleUpdate(tg *Telegram, modem *Modem, u Update) {
	if u.Message == nil {
		return
	}
	msg := u.Message
	from := strconv.FormatInt(msg.Chat.ID, 10)
	text := strings.TrimSpace(msg.Text)
	if text == "" {
		return
	}

	if strings.HasPrefix(text, "/start") {
		chatID = from
		_ = tg.Send(chatID, "Подписка активна.\n/help")
		return
	}

	if chatID != "" && from != chatID {
		_ = tg.Send(from, "Этот бот привязан к другому chat id.")
		return
	}
	if chatID == "" {
		chatID = from
	}

	switch {
	case cmdMatch(text, "help"):
		_ = tg.Send(chatID, helpText())
	case cmdMatch(text, "balance"):
		runUSSD(tg, modem, "*100#")
	case cmdMatch(text, "missed"):
		_ = tg.Send(chatID, "Читаю пропущенные…")
		list, err := modem.ListMissedCalls(20)
		n := modem.MissedCount()
		if err != nil {
			_ = tg.Send(chatID, "Ошибка: "+err.Error()+"\nСчётчик CALLS: "+strconv.Itoa(n))
			return
		}
		if len(list) == 0 {
			msg := "Пропущенных в памяти модема нет."
			if n > 0 {
				msg += "\nСчётчик CPBS="+strconv.Itoa(n)+" (список пуст — странно)."
			}
			msg += "\nНовые звонки шлются сюда сами после гудков (CLIP)."
			_ = tg.Send(chatID, msg)
			return
		}
		var b strings.Builder
		b.WriteString("Пропущенные (")
		b.WriteString(strconv.Itoa(len(list)))
		b.WriteString("):\n")
		for _, c := range list {
			b.WriteString(strconv.Itoa(c.Index))
			b.WriteString(". ")
			if c.Name != "" {
				b.WriteString(c.Name)
				b.WriteString(" ")
			}
			b.WriteString(c.Number)
			b.WriteByte('\n')
		}
		_ = tg.Send(chatID, b.String())
	case strings.HasPrefix(text, "/sms "):
		num, body, ok := parseSMSCmd(text)
		if !ok {
			_ = tg.Send(chatID, "Формат: /sms +79001112233 текст")
			return
		}
		_ = tg.Send(chatID, "Отправляю SMS…")
		if err := modem.SendSMS(num, body); err != nil {
			_ = tg.Send(chatID, "Ошибка SMS: "+err.Error())
			return
		}
		_ = tg.Send(chatID, "SMS отправлено на "+num)
	case strings.HasPrefix(text, "/ussd "):
		code := strings.TrimSpace(strings.TrimPrefix(text, "/ussd "))
		if code == "" {
			_ = tg.Send(chatID, "Формат: /ussd *100#")
			return
		}
		runUSSD(tg, modem, code)
	default:
		_ = tg.Send(chatID, "Не знаю команду. /help")
	}
}

func runUSSD(tg *Telegram, modem *Modem, code string) {
	_ = tg.Send(chatID, "USSD "+code+"…")
	resp, err := modem.USSD(code)
	if err != nil {
		_ = tg.Send(chatID, "Ошибка USSD: "+err.Error())
		return
	}
	if strings.TrimSpace(resp) == "" || strings.TrimSpace(resp) == "OK" {
		_ = tg.Send(chatID, "USSD: пустой ответ (только OK). Попробуй ещё раз.")
		return
	}
	_ = tg.Send(chatID, "USSD\n"+resp)
}

func parseSMSCmd(text string) (number, body string, ok bool) {
	rest := strings.TrimSpace(strings.TrimPrefix(text, "/sms "))
	parts := strings.SplitN(rest, " ", 2)
	if len(parts) < 2 {
		return "", "", false
	}
	number = strings.TrimSpace(parts[0])
	body = strings.TrimSpace(parts[1])
	if number == "" || body == "" {
		return "", "", false
	}
	return number, body, true
}

func fail(msg string) {
	for {
		println("fatal:", msg)
		time.Sleep(2 * time.Second)
	}
}

func printMem(tag string) {
	var ms runtime.MemStats
	runtime.ReadMemStats(&ms)
	println("mem", tag, "alloc", ms.HeapAlloc, "sys", ms.HeapSys, "idle", ms.HeapIdle)
}
