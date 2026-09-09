//go:build tinygo && !modemonly

package main

import (
	"io"
	"net"
	"runtime"
	"time"
)

func syncNTP() {
	const ntpHost = "pool.ntp.org:123"
	const packetSize = 48
	for try := 0; try < 5; try++ {
		conn, err := net.Dial("udp", ntpHost)
		if err != nil {
			println("ntp dial:", err.Error())
			time.Sleep(2 * time.Second)
			continue
		}
		_ = conn.SetDeadline(time.Now().Add(5 * time.Second))
		var req [packetSize]byte
		req[0] = 0xe3
		if _, err := conn.Write(req[:]); err != nil {
			conn.Close()
			println("ntp write:", err.Error())
			time.Sleep(2 * time.Second)
			continue
		}
		var resp [packetSize]byte
		n, err := conn.Read(resp[:])
		conn.Close()
		if err != nil && err != io.EOF {
			println("ntp read:", err.Error())
			time.Sleep(2 * time.Second)
			continue
		}
		if n < packetSize {
			println("ntp short")
			time.Sleep(2 * time.Second)
			continue
		}
		sec := uint32(resp[40])<<24 | uint32(resp[41])<<16 | uint32(resp[42])<<8 | uint32(resp[43])
		const seventyYears = 2208988800
		t := time.Unix(int64(sec-seventyYears), 0)
		runtime.AdjustTimeOffset(-1 * int64(time.Since(t)))
		println("ntp ok:", t.UTC().Format("2006-01-02 15:04:05"))
		return
	}
	println("ntp failed, TLS may break")
}
