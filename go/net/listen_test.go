package net

import (
	"errors"
	"net"
	"testing"
)

func TestIsAddrInUse(t *testing.T) {
	t.Run("nil is false", func(t *testing.T) {
		if IsAddrInUse(nil) {
			t.Error("IsAddrInUse(nil) should be false")
		}
	})
	t.Run("address already in use is true", func(t *testing.T) {
		if !IsAddrInUse(errors.New("listen tcp :0: bind: address already in use")) {
			t.Error("expected true for EADDRINUSE-style error")
		}
	})
	t.Run("other error is false", func(t *testing.T) {
		if IsAddrInUse(errors.New("permission denied")) {
			t.Error("IsAddrInUse(permission denied) should be false")
		}
	})
}

func TestListenOnNextPort(t *testing.T) {
	ln, addr, err := ListenOnNextPort("127.0.0.1", 0)
	if err != nil {
		t.Fatalf("ListenOnNextPort(127.0.0.1, 0): %v", err)
	}
	defer ln.Close()
	if addr == "" {
		t.Error("expected non-empty addr")
	}
	if _, port, err := net.SplitHostPort(addr); err != nil || port == "" {
		t.Errorf("addr %q should have a port: %v", addr, err)
	}
}
