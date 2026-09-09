package swtls

import "crypto/ecdh"

func ecdhX25519() ecdh.Curve { return ecdh.X25519() }

func newX25519Key(privBytes []byte) (*ecdh.PrivateKey, error) {
	return ecdh.X25519().NewPrivateKey(privBytes)
}
