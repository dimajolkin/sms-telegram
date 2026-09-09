//go:build tinygo

package swtls

import (
	"encoding/binary"
	"machine"
)

func readRandom(b []byte) error {
	for i := 0; i < len(b); {
		u, err := machine.GetRNG()
		if err != nil {
			return err
		}
		var tmp [4]byte
		binary.LittleEndian.PutUint32(tmp[:], u)
		n := copy(b[i:], tmp[:])
		i += n
	}
	return nil
}
