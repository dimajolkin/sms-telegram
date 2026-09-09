package swtls

import (
	"crypto/hmac"
	"crypto/sha256"
)

func computeHandshakeKeys(c *Conn) error {
	shared, err := x25519Shared(c.clientPriv[:], c.serverPub[:])
	if err != nil {
		return err
	}
	earlySecret := hkdfExtract(nil, make([]byte, 32))
	derived := deriveSecret(earlySecret, "derived", nil)
	handshakeSecret := hkdfExtract(derived, shared)
	th := c.transcriptSum()
	copy(c.clientHandshakeTrafficSecret[:], deriveSecretHash(handshakeSecret, "c hs traffic", th))
	copy(c.serverHandshakeTrafficSecret[:], deriveSecretHash(handshakeSecret, "s hs traffic", th))
	derived = deriveSecret(handshakeSecret, "derived", nil)
	master := hkdfExtract(derived, make([]byte, 32))
	copy(c.masterSecret[:], master)

	copy(c.clientWriteKey[:], hkdfExpandLabel(c.clientHandshakeTrafficSecret[:], "key", nil, aes128KeyLen))
	copy(c.serverWriteKey[:], hkdfExpandLabel(c.serverHandshakeTrafficSecret[:], "key", nil, aes128KeyLen))
	copy(c.clientWriteIV[:], hkdfExpandLabel(c.clientHandshakeTrafficSecret[:], "iv", nil, gcmIVLen))
	copy(c.serverWriteIV[:], hkdfExpandLabel(c.serverHandshakeTrafficSecret[:], "iv", nil, gcmIVLen))
	c.serverSeq = 0
	c.clientSeq = 0
	return nil
}

func computeServerApplicationKeys(c *Conn) {
	th := c.transcriptSum()
	copy(c.serverAppTrafficSecret[:], deriveSecretHash(c.masterSecret[:], "s ap traffic", th))
	copy(c.clientAppTrafficSecret[:], deriveSecretHash(c.masterSecret[:], "c ap traffic", th))
	copy(c.serverWriteKey[:], hkdfExpandLabel(c.serverAppTrafficSecret[:], "key", nil, aes128KeyLen))
	copy(c.serverWriteIV[:], hkdfExpandLabel(c.serverAppTrafficSecret[:], "iv", nil, gcmIVLen))
	c.serverSeq = 0
}

func computeClientApplicationKeys(c *Conn) {
	copy(c.clientWriteKey[:], hkdfExpandLabel(c.clientAppTrafficSecret[:], "key", nil, aes128KeyLen))
	copy(c.clientWriteIV[:], hkdfExpandLabel(c.clientAppTrafficSecret[:], "iv", nil, gcmIVLen))
	c.clientSeq = 0
}

func computeServerFinished(c *Conn) []byte {
	key := hkdfExpandLabel(c.serverHandshakeTrafficSecret[:], "finished", nil, sha256OutLen)
	mac := hmac.New(sha256.New, key)
	mac.Write(c.lastSum[:])
	return mac.Sum(nil)
}

func computeClientFinished(c *Conn) []byte {
	key := hkdfExpandLabel(c.clientHandshakeTrafficSecret[:], "finished", nil, sha256OutLen)
	mac := hmac.New(sha256.New, key)
	mac.Write(c.transcriptSum())
	return mac.Sum(nil)
}

func x25519Shared(priv, peerPub []byte) ([]byte, error) {
	curve := ecdhX25519()
	k, err := curve.NewPrivateKey(priv)
	if err != nil {
		return nil, err
	}
	pub, err := curve.NewPublicKey(peerPub)
	if err != nil {
		return nil, err
	}
	return k.ECDH(pub)
}

// deriveSecret: TLS 1.3 Derive-Secret(Secret, Label, Messages) with raw Messages.
func deriveSecret(secret []byte, label string, messages []byte) []byte {
	sum := sha256.Sum256(messages)
	return hkdfExpandLabel(secret, label, sum[:], sha256OutLen)
}

// deriveSecretHash: same, but transcriptHash is already Transcript-Hash(Messages).
func deriveSecretHash(secret []byte, label string, transcriptHash []byte) []byte {
	return hkdfExpandLabel(secret, label, transcriptHash, sha256OutLen)
}

func hkdfExpandLabel(secret []byte, label string, context []byte, length int) []byte {
	hkdflabel := make([]byte, 0, 2+1+6+len(label)+1+len(context))
	hkdflabel = append(hkdflabel, byte(length>>8), byte(length))
	hkdflabel = append(hkdflabel, byte(len(label)+6))
	hkdflabel = append(hkdflabel, "tls13 "...)
	hkdflabel = append(hkdflabel, label...)
	hkdflabel = append(hkdflabel, byte(len(context)))
	hkdflabel = append(hkdflabel, context...)
	return hkdfExpand(secret, hkdflabel, length)
}

func hkdfExtract(salt, ikm []byte) []byte {
	if len(salt) == 0 {
		salt = make([]byte, sha256.Size)
	}
	mac := hmac.New(sha256.New, salt)
	mac.Write(ikm)
	return mac.Sum(nil)
}

func hkdfExpand(prk, info []byte, length int) []byte {
	var (
		t       []byte
		out     = make([]byte, 0, length)
		counter byte = 1
	)
	for len(out) < length {
		mac := hmac.New(sha256.New, prk)
		mac.Write(t)
		mac.Write(info)
		mac.Write([]byte{counter})
		t = mac.Sum(nil)
		need := length - len(out)
		if need > len(t) {
			need = len(t)
		}
		out = append(out, t[:need]...)
		counter++
	}
	return out
}
