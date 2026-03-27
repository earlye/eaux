package maps

import (
	"testing"
)

func TestGetFirstEntry(t *testing.T) {
	t.Run("first matching key returns its value", func(t *testing.T) {
		m := map[string]int{"a": 1, "b": 2, "c": 3}
		if got := GetFirstEntry(m, []string{"a", "b"}, 99); got != 1 {
			t.Errorf("GetFirstEntry(m, [a b], 99) = %d; want 1", got)
		}
	})

	t.Run("skips absent keys until first present", func(t *testing.T) {
		m := map[string]int{"b": 2}
		if got := GetFirstEntry(m, []string{"missing", "b"}, 99); got != 2 {
			t.Errorf("GetFirstEntry(m, [missing b], 99) = %d; want 2", got)
		}
	})

	t.Run("no key present returns default", func(t *testing.T) {
		m := map[string]int{"a": 1}
		if got := GetFirstEntry(m, []string{"x", "y"}, 42); got != 42 {
			t.Errorf("GetFirstEntry(m, [x y], 42) = %d; want 42", got)
		}
	})

	t.Run("empty keys returns default", func(t *testing.T) {
		m := map[string]int{"a": 1}
		if got := GetFirstEntry(m, []string{}, 0); got != 0 {
			t.Errorf("GetFirstEntry(m, [], 0) = %d; want 0", got)
		}
	})

	t.Run("empty map returns default", func(t *testing.T) {
		m := map[string]string{}
		if got := GetFirstEntry(m, []string{"a"}, "default"); got != "default" {
			t.Errorf("GetFirstEntry(m, [a], default) = %q; want %q", got, "default")
		}
	})

	t.Run("different key and value types", func(t *testing.T) {
		m := map[int]bool{1: true, 2: false}
		if got := GetFirstEntry(m, []int{99, 2}, true); got != false {
			t.Errorf("GetFirstEntry(m, [99 2], true) = %v; want false", got)
		}
		if got := GetFirstEntry(m, []int{3}, true); got != true {
			t.Errorf("GetFirstEntry(m, [3], true) = %v; want true (default)", got)
		}
	})
}
