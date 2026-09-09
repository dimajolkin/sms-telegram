package swtls

import (
	"crypto/aes"
	"crypto/cipher"
	"fmt"
)

func (c *Conn) decryptRecord(hdr, record []byte) ([]byte, error) {
	if len(record) < gcmTagLen {
		return nil, fmt.Errorf("swtls: short ciphertext")
	}
	block, err := aes.NewCipher(c.serverWriteKey[:])
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	iv := buildIV(c.serverSeq, c.serverWriteIV[:])
	ct := record[:len(record)-gcmTagLen]
	tag := record[len(record)-gcmTagLen:]
	// Go GCM expects ciphertext||tag
	sealed := append(append([]byte{}, ct...), tag...)
	plain, err := gcm.Open(nil, iv, sealed, hdr)
	if err != nil {
		return nil, fmt.Errorf("swtls: decrypt: %w", err)
	}
	return plain, nil
}

func (c *Conn) encryptRecord(plaintext []byte) ([]byte, error) {
	block, err := aes.NewCipher(c.clientWriteKey[:])
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	iv := buildIV(c.clientSeq, c.clientWriteIV[:])
	ll := len(plaintext) + gcmTagLen
	hdr := []byte{recTypeApplicationData, 0x03, 0x03, byte(ll >> 8), byte(ll)}
	sealed := gcm.Seal(nil, iv, plaintext, hdr)
	rec := make([]byte, 0, 5+len(sealed))
	rec = append(rec, hdr...)
	rec = append(rec, sealed...)
	return rec, nil
}

func buildIV(seq uint64, base []byte) []byte {
	iv := make([]byte, len(base))
	copy(iv, base)
	for i := 0; i < 8; i++ {
		iv[len(iv)-1-i] ^= byte(seq >> uint(8*i))
	}
	return iv
}
