package time

import (
	"fmt"
	"time"
)

// FormatISO8601Duration converts a time.Duration to an ISO 8601 duration string
// using only the PT (time) prefix with H, M, and S designators. Durations >= 24h
// result in a large hour value (e.g. PT1000H30M) rather than using date designators.
// Sub-second precision is truncated. Negative durations are prefixed with "-".
func FormatISO8601Duration(d time.Duration) string {
	if d < 0 {
		return "-" + FormatISO8601Duration(-d)
	}

	totalSeconds := int64(d / time.Second)
	h := totalSeconds / 3600
	m := (totalSeconds % 3600) / 60
	s := totalSeconds % 60

	if h == 0 && m == 0 && s == 0 {
		return "PT0S"
	}

	result := "PT"
	if h != 0 {
		result += fmt.Sprintf("%dH", h)
	}
	if m != 0 {
		result += fmt.Sprintf("%dM", m)
	}
	if s != 0 {
		result += fmt.Sprintf("%dS", s)
	}
	return result
}
