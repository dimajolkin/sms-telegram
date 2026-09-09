// Package swtls — минимальный TLS 1.3 клиент (AES-128-GCM + X25519).
// Сертификаты не проверяются (hobby InsecureSkipVerify).
// Логика рукопожатия адаптирована из syncsynchalt/tincan-tls.
package swtls

import (
	"crypto/sha256"
	"errors"
	"fmt"
	"hash"
	"io"
	"net"
	"time"
)

const (
	x25519KeyLen = 32
	sha256OutLen = 32
	aes128KeyLen = 16
	gcmIVLen     = 12
	gcmTagLen    = 16
)

type action int

const (
	actionNone          = action(0)
	actionResetSequence = action(1 << iota)
	actionSendFinished
)

// Conn — TLS 1.3 поверх TCP.
type Conn struct {
	raw          net.Conn
	clientRandom [32]byte
	clientPriv   [x25519KeyLen]byte
	clientPub    [x25519KeyLen]byte
	serverRandom [32]byte
	serverPub    [x25519KeyLen]byte

	transcript hash.Hash
	lastSum    [sha256OutLen]byte

	masterSecret                 [sha256OutLen]byte
	clientHandshakeTrafficSecret [sha256OutLen]byte
	serverHandshakeTrafficSecret [sha256OutLen]byte
	clientAppTrafficSecret       [sha256OutLen]byte
	serverAppTrafficSecret       [sha256OutLen]byte

	clientWriteKey [aes128KeyLen]byte
	serverWriteKey [aes128KeyLen]byte
	clientWriteIV  [gcmIVLen]byte
	serverWriteIV  [gcmIVLen]byte

	serverSeq uint64
	clientSeq uint64

	handshakeDone bool
	readBuf       []byte
}

// Client выполняет TLS 1.3 handshake. hostname — для SNI.
func Client(raw net.Conn, hostname string) (*Conn, error) {
	c := &Conn{raw: raw, transcript: sha256.New()}
	rec, err := makeClientHello(c, hostname)
	if err != nil {
		return nil, err
	}
	if err := c.writeRecordBytes(rec, true); err != nil {
		return nil, err
	}
	for !c.handshakeDone {
		if _, err := c.readRecord(); err != nil {
			return nil, err
		}
	}
	c.transcript = nil
	return c, nil
}

func (c *Conn) Read(b []byte) (int, error) {
	for len(c.readBuf) == 0 {
		if _, err := c.readRecord(); err != nil {
			return 0, err
		}
	}
	n := copy(b, c.readBuf)
	c.readBuf = c.readBuf[n:]
	if len(c.readBuf) == 0 {
		c.readBuf = nil
	}
	return n, nil
}

func (c *Conn) Write(b []byte) (int, error) {
	plain := make([]byte, len(b)+1)
	copy(plain, b)
	plain[len(b)] = recTypeApplicationData
	enc, err := c.encryptRecord(plain)
	if err != nil {
		return 0, err
	}
	if err := c.writeRaw(enc); err != nil {
		return 0, err
	}
	c.clientSeq++
	return len(b), nil
}

func (c *Conn) Close() error                       { return c.raw.Close() }
func (c *Conn) LocalAddr() net.Addr                { return c.raw.LocalAddr() }
func (c *Conn) RemoteAddr() net.Addr               { return c.raw.RemoteAddr() }
func (c *Conn) SetDeadline(t time.Time) error      { return c.raw.SetDeadline(t) }
func (c *Conn) SetReadDeadline(t time.Time) error  { return c.raw.SetReadDeadline(t) }
func (c *Conn) SetWriteDeadline(t time.Time) error { return c.raw.SetWriteDeadline(t) }

func (c *Conn) writeRecordBytes(rec []byte, addTranscript bool) error {
	if err := c.writeRaw(rec); err != nil {
		return err
	}
	if addTranscript && len(rec) > 5 {
		c.addToTranscript(rec[5:])
	}
	return nil
}

func (c *Conn) writeRaw(b []byte) error {
	for len(b) > 0 {
		n, err := c.raw.Write(b)
		b = b[n:]
		if err != nil {
			return err
		}
	}
	return nil
}

func (c *Conn) readRaw(b []byte) error {
	for len(b) > 0 {
		n, err := c.raw.Read(b)
		b = b[n:]
		if err != nil {
			return err
		}
	}
	return nil
}

func (c *Conn) readRecord() (bool, error) {
	hdr := make([]byte, 5)
	if err := c.readRaw(hdr); err != nil {
		return false, err
	}
	typ := hdr[0]
	length := int(hdr[3])<<8 | int(hdr[4])
	if length < 0 || length > 16384+256 {
		return false, fmt.Errorf("swtls: bad record length %d", length)
	}
	payload := make([]byte, length)
	if err := c.readRaw(payload); err != nil {
		return false, err
	}

	acts, err := c.handleRecord(typ, hdr, payload)
	if err != nil {
		return false, err
	}

	if typ != recTypeChangeCipherSpec {
		c.serverSeq++
	}
	if acts&actionResetSequence != 0 {
		c.serverSeq = 0
		c.clientSeq = 0
	}
	if acts&actionSendFinished != 0 {
		fin, err := makeClientFinished(c)
		if err != nil {
			return false, err
		}
		if err := c.writeRaw(fin); err != nil {
			return false, err
		}
		c.clientSeq++
		computeClientApplicationKeys(c)
		c.handshakeDone = true
		return true, nil
	}
	return false, nil
}

func (c *Conn) handleRecord(typ byte, hdr, payload []byte) (action, error) {
	switch typ {
	case recTypeChangeCipherSpec:
		return handleChangeCipherSpec(payload)
	case recTypeAlert:
		return 0, handleAlert(payload)
	case recTypeHandshake:
		c.addToTranscript(payload)
		return handleHandshake(c, payload)
	case recTypeApplicationData:
		plain, err := c.decryptRecord(hdr, payload)
		if err != nil {
			return 0, err
		}
		return c.dispatchInner(hdr, plain)
	default:
		return 0, fmt.Errorf("swtls: unknown record type %d", typ)
	}
}

func (c *Conn) dispatchInner(hdr, inner []byte) (action, error) {
	for len(inner) > 0 && inner[len(inner)-1] == 0 {
		inner = inner[:len(inner)-1]
	}
	if len(inner) == 0 {
		return actionNone, errors.New("swtls: empty inner plaintext")
	}
	overall := inner[len(inner)-1]
	inner = inner[:len(inner)-1]

	switch overall {
	case recTypeApplicationData:
		c.readBuf = append(c.readBuf, inner...)
		return actionNone, nil
	case recTypeHandshake:
		var acts action
		for len(inner) > 0 {
			if len(inner) < 4 {
				return 0, errors.New("swtls: short inner handshake")
			}
			n := int(inner[1])<<16 | int(inner[2])<<8 | int(inner[3])
			if 4+n > len(inner) {
				return 0, errors.New("swtls: truncated inner handshake")
			}
			msg := inner[:4+n]
			inner = inner[4+n:]
			c.addToTranscript(msg)
			a, err := handleHandshake(c, msg)
			if err != nil {
				return 0, err
			}
			acts |= a
		}
		return acts, nil
	case recTypeAlert:
		return 0, handleAlert(inner)
	default:
		return 0, fmt.Errorf("swtls: unknown inner type %d", overall)
	}
}

func (c *Conn) addToTranscript(hs []byte) {
	if c.transcript == nil {
		c.transcript = sha256.New()
	}
	// Снимок hash до append — для server Finished (как бывший lastTranscript).
	sum := c.transcript.Sum(nil)
	copy(c.lastSum[:], sum)
	_, _ = c.transcript.Write(hs)
}

func (c *Conn) transcriptSum() []byte {
	if c.transcript == nil {
		s := sha256.Sum256(nil)
		return s[:]
	}
	return c.transcript.Sum(nil)
}

// closeNotify maps TLS close_notify to io.EOF.
func closeNotify() error { return io.EOF }
