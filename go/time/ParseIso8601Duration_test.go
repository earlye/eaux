package time

import (
	"testing"
	"time"
)

func TestParseISO8601Duration(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		want    time.Duration
		wantErr bool
	}{
		// Seconds
		{"seconds only", "PT1S", time.Second, false},
		{"90 seconds", "PT90S", 90 * time.Second, false},
		{"zero seconds", "PT0S", 0, false},

		// Minutes
		{"5 minutes", "PT5M", 5 * time.Minute, false},
		{"90 minutes", "PT90M", 90 * time.Minute, false},

		// Hours
		{"1 hour", "PT1H", time.Hour, false},
		{"1 hour 30 minutes", "PT1H30M", 1*time.Hour + 30*time.Minute, false},
		{"2 hours 15 minutes 45 seconds", "PT2H15M45S", 2*time.Hour + 15*time.Minute + 45*time.Second, false},

		// Days
		{"1 day", "P1D", 24 * time.Hour, false},
		{"3 days", "P3D", 3 * 24 * time.Hour, false},

		// Weeks
		{"1 week", "P1W", 7 * 24 * time.Hour, false},
		{"2 weeks", "P2W", 2 * 7 * 24 * time.Hour, false},

		// Months (approximated as 30 days)
		{"1 month", "P1M", 30 * 24 * time.Hour, false},
		{"2 months", "P2M", 2 * 30 * 24 * time.Hour, false},

		// Years (approximated as 365 days)
		{"1 year", "P1Y", 365 * 24 * time.Hour, false},
		{"2 years", "P2Y", 2 * 365 * 24 * time.Hour, false},

		// Combined date and time
		{"1 day 1 hour", "P1DT1H", 25 * time.Hour, false},
		{"1 week 1 day", "P1W1D", (7 + 1) * 24 * time.Hour, false},
		{"full duration", "P1Y2M3DT4H5M6S", 365*24*time.Hour + 2*30*24*time.Hour + 3*24*time.Hour + 4*time.Hour + 5*time.Minute + 6*time.Second, false},

		// Zero / empty-ish
		{"zero duration", "P0D", 0, false},

		// Invalid
		{"empty string", "", 0, true},
		{"invalid format", "invalid", 0, true},
		{"missing P", "T5M", 0, true},
		{"garbage", "not-a-duration", 0, true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := ParseISO8601Duration(tt.input)
			if (err != nil) != tt.wantErr {
				t.Errorf("ParseISO8601Duration(%q) error = %v, wantErr %v", tt.input, err, tt.wantErr)
				return
			}
			if !tt.wantErr && got != tt.want {
				t.Errorf("ParseISO8601Duration(%q) = %v, want %v", tt.input, got, tt.want)
			}
		})
	}
}
