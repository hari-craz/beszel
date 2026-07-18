package systems

import (
	"errors"
	"fmt"
	"net"
)

// SendMagicPacket constructs and broadcasts a Wake-on-LAN magic packet payload over UDP.
func SendMagicPacket(macStr, bcastIPStr string, port int) error {
	mac, err := net.ParseMAC(macStr)
	if err != nil {
		return fmt.Errorf("invalid MAC address: %w", err)
	}

	if len(mac) != 6 {
		return errors.New("MAC address must be 6 bytes")
	}

	// Magic packet: 6×0xFF header followed by the target MAC repeated 16 times (102 bytes total)
	payload := make([]byte, 6+16*6)
	for i := 0; i < 6; i++ {
		payload[i] = 0xFF
	}
	for i := 0; i < 16; i++ {
		copy(payload[6+i*6:], mac)
	}

	if bcastIPStr == "" {
		bcastIPStr = "255.255.255.255"
	}
	if port <= 0 {
		port = 9
	}

	// Resolve destination UDP address
	destAddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", bcastIPStr, port))
	if err != nil {
		return fmt.Errorf("failed to resolve UDP address: %w", err)
	}

	// Open connection
	conn, err := net.DialUDP("udp", nil, destAddr)
	if err != nil {
		return fmt.Errorf("failed to open UDP connection: %w", err)
	}
	defer conn.Close()

	// Send magic packet payload
	_, err = conn.Write(payload)
	if err != nil {
		return fmt.Errorf("failed to write magic packet: %w", err)
	}

	return nil
}
