package swtls

import (
	"fmt"
)

func handleHandshake(c *Conn, payload []byte) (action, error) {
	if len(payload) < 4 {
		return 0, fmt.Errorf("swtls: short handshake")
	}
	typ := payload[0]
	body, rest, err := readVec(24, payload[1:])
	if err != nil {
		return 0, err
	}
	if len(rest) != 0 {
		return 0, fmt.Errorf("swtls: trailing handshake bytes")
	}
	switch typ {
	case hsServerHello:
		if err := handleServerHello(c, body); err != nil {
			return 0, err
		}
		if err := computeHandshakeKeys(c); err != nil {
			return 0, err
		}
		return actionResetSequence, nil
	case hsEncryptedExtensions:
		return actionNone, handleEncryptedExtensions(body)
	case hsCertificate, hsCertificateVerify:
		// без проверки сертификата
		return actionNone, nil
	case hsFinished:
		if err := handleServerFinished(c, body); err != nil {
			return 0, err
		}
		return actionSendFinished | actionResetSequence, nil
	case hsNewSessionTicket:
		return actionNone, nil
	default:
		return actionNone, nil
	}
}

func handleServerHello(c *Conn, payload []byte) error {
	if len(payload) < 2+32 {
		return fmt.Errorf("swtls: short ServerHello")
	}
	payload = payload[2:] // legacy_version
	if match(helloRetryRequest, payload) {
		return fmt.Errorf("swtls: HelloRetryRequest not supported")
	}
	copy(c.serverRandom[:], payload[:32])
	payload = payload[32:]

	_, payload, err := readVec(8, payload)
	if err != nil {
		return err
	}
	if !match(suiteAES128GCM, payload) {
		return fmt.Errorf("swtls: unexpected cipher suite")
	}
	payload = payload[2:]
	if len(payload) < 1 || payload[0] != 0 {
		return fmt.Errorf("swtls: bad compression")
	}
	payload = payload[1:]
	exts, payload, err := readVec(16, payload)
	if err != nil {
		return err
	}
	if len(payload) != 0 {
		return fmt.Errorf("swtls: ServerHello trailing data")
	}
	return parseExtensions(c, exts, true)
}

func handleEncryptedExtensions(payload []byte) error {
	exts, _, err := readVec(16, payload)
	if err != nil {
		return err
	}
	return parseExtensions(nil, exts, false)
}

func handleServerFinished(c *Conn, payload []byte) error {
	expected := computeServerFinished(c)
	if len(expected) != len(payload) {
		return fmt.Errorf("swtls: Finished length mismatch")
	}
	for i := range payload {
		if expected[i] != payload[i] {
			return fmt.Errorf("swtls: Finished verify failed")
		}
	}
	computeServerApplicationKeys(c)
	return nil
}

func parseExtensions(c *Conn, exts []byte, needKeyShare bool) error {
	gotKeyShare := false
	for len(exts) > 0 {
		if len(exts) < 4 {
			return fmt.Errorf("swtls: short extension")
		}
		typ := int(exts[0])<<8 | int(exts[1])
		ext, rest, err := readVec(16, exts[2:])
		if err != nil {
			return err
		}
		exts = rest
		switch typ {
		case extKeyShare:
			if c == nil {
				continue
			}
			if err := parseKeyShare(c, ext); err != nil {
				return err
			}
			gotKeyShare = true
		case extSupportedVersions:
			if !match(tlsVersion13, ext) {
				return fmt.Errorf("swtls: not TLS 1.3")
			}
		default:
			// skip unknown (ALPN, etc.)
		}
	}
	if needKeyShare && !gotKeyShare {
		return fmt.Errorf("swtls: missing key_share")
	}
	return nil
}

func parseKeyShare(c *Conn, payload []byte) error {
	if !match(groupX25519, payload) {
		return fmt.Errorf("swtls: non-X25519 key_share")
	}
	payload = payload[2:]
	pubkey, rest, err := readVec(16, payload)
	if err != nil {
		return err
	}
	if len(pubkey) != 32 || len(rest) != 0 {
		return fmt.Errorf("swtls: bad key_share length")
	}
	copy(c.serverPub[:], pubkey)
	return nil
}

func handleChangeCipherSpec(payload []byte) (action, error) {
	if len(payload) != 1 || payload[0] != 1 {
		return 0, fmt.Errorf("swtls: bad CCS")
	}
	// не трогаем sequence — RFC 8446: ignore
	return actionNone, nil
}

func handleAlert(payload []byte) error {
	if len(payload) < 2 {
		return fmt.Errorf("swtls: short alert")
	}
	if payload[1] == 0 { // close_notify
		return closeNotify()
	}
	level := "warning"
	if payload[0] == 2 {
		level = "fatal"
	}
	return fmt.Errorf("swtls: %s alert %d", level, payload[1])
}
