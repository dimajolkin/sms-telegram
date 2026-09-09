package swtls

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"time"
)

// Transport — http.RoundTripper с software TLS 1.3.
// Соединение можно переиспользовать, но firmware закрывает его после каждого
// запроса (CloseIdle): иначе Conn держит heap, и UI/модем ловят OOM.
type Transport struct {
	Timeout time.Duration

	host   string
	conn   net.Conn
	reader *bufio.Reader
}

func (t *Transport) RoundTrip(req *http.Request) (*http.Response, error) {
	if req.URL == nil {
		return nil, fmt.Errorf("swtls: nil URL")
	}
	host := req.URL.Hostname()
	if host == "" {
		return nil, fmt.Errorf("swtls: empty host")
	}
	port := req.URL.Port()
	if port == "" {
		if req.URL.Scheme == "http" {
			port = "80"
		} else {
			port = "443"
		}
	}
	addr := net.JoinHostPort(host, port)
	useTLS := req.URL.Scheme == "https" || port == "443"

	timeout := t.Timeout
	if timeout == 0 {
		timeout = 45 * time.Second
	}
	deadline := time.Now().Add(timeout)

	conn, br, err := t.connFor(host, addr, useTLS, timeout, deadline)
	if err != nil {
		return nil, err
	}

	if req.Header == nil {
		req.Header = make(http.Header)
	}
	if req.Host == "" {
		req.Host = host
	}
	if req.Header.Get("User-Agent") == "" {
		req.Header.Set("User-Agent", "sms-telegram/swtls")
	}
	req.Header.Set("Connection", "close")
	req.Proto = "HTTP/1.1"
	req.ProtoMajor = 1
	req.ProtoMinor = 1

	if err := req.Write(conn); err != nil {
		t.drop()
		return nil, err
	}
	resp, err := http.ReadResponse(br, req)
	if err != nil {
		t.drop()
		return nil, err
	}
	resp.Body = &connBody{ReadCloser: resp.Body, tr: t}
	return resp, nil
}

func (t *Transport) connFor(host, addr string, useTLS bool, timeout time.Duration, deadline time.Time) (net.Conn, *bufio.Reader, error) {
	if t.conn != nil && t.host == host && t.reader != nil {
		_ = t.conn.SetDeadline(deadline)
		return t.conn, t.reader, nil
	}
	t.drop()

	raw, err := net.DialTimeout("tcp", addr, timeout)
	if err != nil {
		return nil, nil, err
	}
	_ = raw.SetDeadline(deadline)

	var conn net.Conn = raw
	if useTLS {
		tlsConn, err := Client(raw, host)
		if err != nil {
			raw.Close()
			return nil, nil, err
		}
		conn = tlsConn
	}
	t.host = host
	t.conn = conn
	t.reader = bufio.NewReaderSize(conn, 512)
	return t.conn, t.reader, nil
}

func (t *Transport) drop() {
	if t.conn != nil {
		_ = t.conn.Close()
	}
	t.conn = nil
	t.reader = nil
	t.host = ""
}

// CloseIdle закрывает keep-alive (вызов перед GC, если нужно освободить RAM).
func (t *Transport) CloseIdle() {
	if t != nil {
		t.drop()
	}
}

type connBody struct {
	io.ReadCloser
	tr *Transport
}

func (b *connBody) Close() error {
	// Дочитываем тело, чтобы keep-alive остался валидным.
	_, _ = io.Copy(io.Discard, io.LimitReader(b.ReadCloser, 8*1024))
	err := b.ReadCloser.Close()
	return err
}

// Dial открывает TLS-соединение к host:port (host без порта для SNI).
func Dial(network, addr string) (net.Conn, error) {
	host := addr
	if h, _, err := net.SplitHostPort(addr); err == nil {
		host = h
	} else if i := strings.LastIndex(addr, ":"); i > 0 {
		host = addr[:i]
	}
	raw, err := net.Dial(network, addr)
	if err != nil {
		return nil, err
	}
	c, err := Client(raw, host)
	if err != nil {
		raw.Close()
		return nil, err
	}
	return c, nil
}
