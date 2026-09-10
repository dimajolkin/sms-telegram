//go:build tinygo && !modemonly

package main

import "strings"

// BotCommand — единый источник для меню Telegram, /help и описаний.
type BotCommand struct {
	Cmd  string // без слэша: "balance"
	Desc string // для setMyCommands (коротко)
	Help string // для /help (можно чуть подробнее)
}

// botCommands — править только здесь.
var botCommands = []BotCommand{
	{Cmd: "balance", Desc: "Баланс (*100#)", Help: "/balance — баланс (*100#)"},
	{Cmd: "missed", Desc: "Пропущенные звонки", Help: "/missed — список пропущенных звонков"},
	{Cmd: "sms", Desc: "SMS: /sms +79… текст", Help: "/sms +79001112233 текст — отправить SMS"},
	{Cmd: "ussd", Desc: "USSD: /ussd *код#", Help: "/ussd *100# — произвольный USSD"},
	{Cmd: "help", Desc: "Справка", Help: "/help — справка"},
}

func helpText() string {
	var b strings.Builder
	b.WriteString("Команды:\n")
	for _, c := range botCommands {
		b.WriteString(c.Help)
		b.WriteByte('\n')
	}
	b.WriteString("\nВходящие SMS и пропущенные звонки приходят сюда автоматически.\n")
	b.WriteString("На экране: Next=inbox, OK=открыть/назад.")
	return b.String()
}

func cmdMatch(text, name string) bool {
	if text == "/"+name {
		return true
	}
	return strings.HasPrefix(text, "/"+name+"@")
}
