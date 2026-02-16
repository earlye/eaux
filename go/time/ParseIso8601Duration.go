package time

import (
	"time"

	"github.com/senseyeio/duration"
)

// Parse an ISO 8601 duration (e.g. PT5M, PT1H30M) into time.Duration.
func ParseISO8601Duration(s string) (time.Duration, error) {
	d, err := duration.ParseISO8601(s)
	if err != nil {
		return 0, err
	}
	// Approximate to time.Duration: seconds + minutes + hours + days + weeks; months/years use fixed day counts
	return time.Duration(d.TS)*time.Second +
		time.Duration(d.TM)*time.Minute +
		time.Duration(d.TH)*time.Hour +
		time.Duration(d.D)*24*time.Hour +
		time.Duration(d.W)*7*24*time.Hour +
		time.Duration(d.M)*30*24*time.Hour +
		time.Duration(d.Y)*365*24*time.Hour, nil
}
