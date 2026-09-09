package swtls

import "fmt"

func makeClientHello(c *Conn, hostname string) ([]byte, error) {
	b := make([]byte, 0, 256)
	b = append(b, tlsVersion12...)

	if err := readRandom(c.clientRandom[:]); err != nil {
		return nil, err
	}
	b = append(b, c.clientRandom[:]...)
	b = append(b, 0x00) // empty legacy_session_id

	b = appendLen16(b, 2)
	b = append(b, suiteAES128GCM...)

	b = append(b, 0x01, 0x00) // compression

	exts := make([]byte, 0, 128)

	// supported_versions
	exts = appendU16(exts, extSupportedVersions)
	exts = appendLen16(exts, 3)
	exts = appendLen8(exts, 2)
	exts = append(exts, tlsVersion13...)

	// supported_groups
	exts = appendU16(exts, extSupportedGroups)
	exts = appendLen16(exts, 4)
	exts = appendLen16(exts, 2)
	exts = append(exts, groupX25519...)

	// key_share — NewPrivateKey, не GenerateKey:
	// в Go 1.26 GenerateKey игнорирует Reader (sysrand → /dev/urandom).
	var privBytes [32]byte
	if err := readRandom(privBytes[:]); err != nil {
		return nil, err
	}
	priv, err := newX25519Key(privBytes[:])
	if err != nil {
		return nil, err
	}
	copy(c.clientPriv[:], priv.Bytes())
	copy(c.clientPub[:], priv.PublicKey().Bytes())
	exts = appendU16(exts, extKeyShare)
	exts = appendLen16(exts, len(c.clientPub)+2+2+2)
	exts = appendLen16(exts, len(c.clientPub)+2+2)
	exts = append(exts, groupX25519...)
	exts = appendLen16(exts, len(c.clientPub))
	exts = append(exts, c.clientPub[:]...)

	// server_name
	exts = appendU16(exts, extServerName)
	exts = appendLen16(exts, len(hostname)+5)
	exts = appendLen16(exts, len(hostname)+3)
	exts = append(exts, 0x00) // host_name
	exts = appendLen16(exts, len(hostname))
	exts = append(exts, hostname...)

	// signature_algorithms
	exts = appendU16(exts, extSignatureAlgorithms)
	exts = appendLen16(exts, 8)
	exts = appendLen16(exts, 6)
	exts = append(exts, sigRSAPKCS1SHA256...)
	exts = append(exts, sigECDSAP256SHA256...)
	exts = append(exts, sigRSAPSSSHA256...)

	b = appendLen16(b, len(exts))
	b = append(b, exts...)

	hs := make([]byte, 0, 4+len(b))
	hs = append(hs, hsClientHello)
	hs = appendLen24(hs, len(b))
	hs = append(hs, b...)

	rec := make([]byte, 0, 5+len(hs))
	rec = append(rec, recTypeHandshake)
	rec = append(rec, tlsVersion12...)
	rec = appendLen16(rec, len(hs))
	rec = append(rec, hs...)
	return rec, nil
}

func makeClientFinished(c *Conn) ([]byte, error) {
	finished := computeClientFinished(c)
	hs := make([]byte, 0, 4+len(finished)+1)
	hs = append(hs, hsFinished)
	hs = appendLen24(hs, len(finished))
	hs = append(hs, finished...)
	hs = append(hs, recTypeHandshake)
	return c.encryptRecord(hs)
}

func appendLen8(b []byte, n int) []byte  { return append(b, byte(n)) }
func appendLen16(b []byte, n int) []byte {
	return append(b, byte(n>>8), byte(n))
}
func appendLen24(b []byte, n int) []byte {
	return append(b, byte(n>>16), byte(n>>8), byte(n))
}
func appendU16(b []byte, n int) []byte {
	return append(b, byte(n>>8), byte(n))
}

func readNum(bits int, b []byte) (uint, error) {
	need := bits / 8
	if len(b) < need {
		return 0, fmt.Errorf("swtls: short readNum")
	}
	var x uint
	for i := 0; i < need; i++ {
		x = x<<8 | uint(b[i])
	}
	return x, nil
}

func readVec(lenBits int, payload []byte) (vec, rest []byte, err error) {
	n, err := readNum(lenBits, payload)
	if err != nil {
		return nil, nil, err
	}
	hdr := lenBits / 8
	if uint(len(payload)) < uint(hdr)+n {
		return nil, nil, fmt.Errorf("swtls: short vector")
	}
	return payload[hdr : hdr+int(n)], payload[hdr+int(n):], nil
}

func match(pat, b []byte) bool {
	if len(b) < len(pat) {
		return false
	}
	for i := range pat {
		if pat[i] != b[i] {
			return false
		}
	}
	return true
}
