package time

import (
	"testing"
	"time"
)

func TestFormatISO8601Duration(t *testing.T) {
	tests := []struct {
		name  string
		input time.Duration
		want  string
	}{
		// Zero
		{"zero", 0, "PT0S"},

		// Seconds only
		{"1 second", time.Second, "PT1S"},
		{"45 seconds", 45 * time.Second, "PT45S"},
		{"90 seconds", 90 * time.Second, "PT1M30S"},

		// Minutes only
		{"5 minutes", 5 * time.Minute, "PT5M"},
		{"90 minutes", 90 * time.Minute, "PT1H30M"},

		// Hours only
		{"1 hour", time.Hour, "PT1H"},
		{"24 hours", 24 * time.Hour, "PT24H"},

		// Combined
		{"1 hour 30 minutes", time.Hour + 30*time.Minute, "PT1H30M"},
		{"2 hours 15 minutes 45 seconds", 2*time.Hour + 15*time.Minute + 45*time.Second, "PT2H15M45S"},
		{"1 hour 0 minutes 1 second", time.Hour + time.Second, "PT1H1S"},

		// Large hours (>= 24h expressed as hours, not days)
		{"1000 hours 30 minutes", 1000*time.Hour + 30*time.Minute, "PT1000H30M"},
		{"48 hours", 48 * time.Hour, "PT48H"},

		// Sub-second precision truncated
		{"1 second 500ms truncated", time.Second + 500*time.Millisecond, "PT1S"},
		{"999ms truncated to zero", 999 * time.Millisecond, "PT0S"},

		// Negative
		{"negative 5 minutes", -5 * time.Minute, "-PT5M"},
		{"negative 1 hour 30 minutes", -(time.Hour + 30*time.Minute), "-PT1H30M"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := FormatISO8601Duration(tt.input)
			if got != tt.want {
				t.Errorf("FormatISO8601Duration(%v) = %q, want %q", tt.input, got, tt.want)
			}
		})
	}
}
