package swtls

import (
	"io"
	"net"
	"testing"
	"time"
)

func TestClientTelegram(t *testing.T) {
	raw, err := net.DialTimeout("tcp", "api.telegram.org:443", 10*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	defer raw.Close()
	_ = raw.SetDeadline(time.Now().Add(30 * time.Second))

	c, err := Client(raw, "api.telegram.org")
	if err != nil {
		t.Fatal(err)
	}
	req := "GET / HTTP/1.1\r\nHost: api.telegram.org\r\nConnection: close\r\nUser-Agent: swtls-test\r\n\r\n"
	if _, err := c.Write([]byte(req)); err != nil {
		t.Fatal(err)
	}
	buf := make([]byte, 128)
	n, err := c.Read(buf)
	if n == 0 && err != nil {
		t.Fatal(err)
	}
	t.Logf("got %d bytes: %q err=%v", n, string(buf[:n]), err)
	if n < 4 || string(buf[:4]) != "HTTP" {
		// drain a bit more in case of NST first
		rest, _ := io.ReadAll(io.LimitReader(c, 512))
		all := append(buf[:n], rest...)
		t.Logf("full: %q", all)
		if len(all) < 4 || string(all[:4]) != "HTTP" {
			t.Fatalf("expected HTTP response, got %q", all)
		}
	}
}
