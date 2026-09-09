package swtls

var (
	tlsVersion12 = []byte{0x03, 0x03}
	tlsVersion13 = []byte{0x03, 0x04}

	suiteAES128GCM = []byte{0x13, 0x01}

	sigRSAPKCS1SHA256 = []byte{0x04, 0x01}
	sigECDSAP256SHA256 = []byte{0x04, 0x03}
	sigRSAPSSSHA256   = []byte{0x08, 0x04}

	groupX25519 = []byte{0x00, 0x1d}

	helloRetryRequest = []byte{
		0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,
		0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C,
	}
)

const (
	extServerName          = 0
	extSupportedGroups     = 10
	extSignatureAlgorithms = 13
	extSupportedVersions   = 43
	extKeyShare            = 51

	recTypeChangeCipherSpec = byte(20)
	recTypeAlert            = byte(21)
	recTypeHandshake        = byte(22)
	recTypeApplicationData  = byte(23)

	hsClientHello         = byte(1)
	hsServerHello         = byte(2)
	hsNewSessionTicket    = byte(4)
	hsEncryptedExtensions = byte(8)
	hsCertificate         = byte(11)
	hsCertificateVerify   = byte(15)
	hsFinished            = byte(20)
)
