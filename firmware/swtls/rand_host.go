//go:build !tinygo

package swtls

import cryptorand "crypto/rand"

func readRandom(b []byte) error {
	_, err := cryptorand.Read(b)
	return err
}
