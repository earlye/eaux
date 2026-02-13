package maps

import (
	"testing"
)

func TestGetEntry(t *testing.T) {
	t.Run("key present returns value", func(t *testing.T) {
		m := map[string]int{"a": 1, "b": 2}
		if got := GetEntry(m, "a", 99); got != 1 {
			t.Errorf("GetEntry(m, %q, 99) = %d; want 1", "a", got)
		}
		if got := GetEntry(m, "b", 99); got != 2 {
			t.Errorf("GetEntry(m, %q, 99) = %d; want 2", "b", got)
		}
	})

	t.Run("key absent returns default", func(t *testing.T) {
		m := map[string]int{"a": 1}
		if got := GetEntry(m, "missing", 42); got != 42 {
			t.Errorf("GetEntry(m, %q, 42) = %d; want 42", "missing", got)
		}
	})

	t.Run("empty map returns default", func(t *testing.T) {
		m := map[string]string{}
		if got := GetEntry(m, "any", "default"); got != "default" {
			t.Errorf("GetEntry(m, %q, %q) = %q; want %q", "any", "default", got, "default")
		}
	})

	t.Run("different key and value types", func(t *testing.T) {
		m := map[int]bool{1: true, 2: false}
		if got := GetEntry(m, 1, false); got != true {
			t.Errorf("GetEntry(m, 1, false) = %v; want true", got)
		}
		if got := GetEntry(m, 99, true); got != true {
			t.Errorf("GetEntry(m, 99, true) = %v; want true (default)", got)
		}
	})
}
