package env

import (
	"os"
	"reflect"
	"testing"
)

func TestMap(t *testing.T) {
	got := Map([]string{"A=1", "B=2", "NOEQ", "C="})
	want := map[string]string{"A": "1", "B": "2", "C": ""}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("Map() = %v, want %v", got, want)
	}
}

func TestGetenvDefault(t *testing.T) {
	const key = "EAUX_TEST_GETENV_DEFAULT"
	defer os.Unsetenv(key)

	t.Run("unset returns fallback", func(t *testing.T) {
		os.Unsetenv(key)
		if got := GetenvDefault(key, "fallback"); got != "fallback" {
			t.Errorf("GetenvDefault(%q, %q) = %q, want fallback", key, "fallback", got)
		}
	})

	t.Run("set returns value", func(t *testing.T) {
		os.Setenv(key, "custom")
		defer os.Unsetenv(key)
		if got := GetenvDefault(key, "fallback"); got != "custom" {
			t.Errorf("GetenvDefault(%q, %q) = %q, want custom", key, "fallback", got)
		}
	})

	t.Run("set empty returns empty", func(t *testing.T) {
		os.Setenv(key, "")
		defer os.Unsetenv(key)
		if got := GetenvDefault(key, "fallback"); got != "" {
			t.Errorf("GetenvDefault(%q, %q) = %q, want empty", key, "fallback", got)
		}
	})
}
