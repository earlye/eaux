package net

import (
	"fmt"
	"net"
	"strconv"
	"strings"
)

// ListenOnNextPort tries host:port, then host:port+1, ... until a listener is bound.
// Skips any port where 0.0.0.0:port is already in use (e.g. by Docker), so the chosen
// port is free on both the host and the wildcard. Returns the listener and the actual
// address (e.g. "127.0.0.1:8082").
func ListenOnNextPort(host string, startPort int) (net.Listener, string, error) {
	const maxPort = 65535
	for port := startPort; port <= maxPort; port++ {
		portStr := strconv.Itoa(port)
		if host != "0.0.0.0" {
			probe, err := net.Listen("tcp", net.JoinHostPort("0.0.0.0", portStr))
			if err != nil {
				if IsAddrInUse(err) {
					continue
				}
				return nil, "", fmt.Errorf("probe 0.0.0.0:%s: %w", portStr, err)
			}
			probe.Close()
		}
		addr := net.JoinHostPort(host, portStr)
		ln, err := net.Listen("tcp", addr)
		if err == nil {
			return ln, addr, nil // success path
		}
		if IsAddrInUse(err) {
			continue
		}
		return nil, "", fmt.Errorf("listen %s: %w", addr, err)
	}
	return nil, "", fmt.Errorf("no free port in range %d-%d", startPort, maxPort)
}

// IsAddrInUse reports whether err indicates "address already in use"
// (POSIX EADDRINUSE / Windows WSAEADDRINUSE).
func IsAddrInUse(err error) bool {
	if err == nil {
		return false
	}
	const errAddrInUse = "address already in use"
	return strings.Contains(err.Error(), errAddrInUse)
}
