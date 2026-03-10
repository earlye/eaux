package time

import (
	"time"

	"github.com/senseyeio/duration"
)

type ISO8601Duration = duration.Duration

// ParseISO8601Duration parses an ISO 8601 duration string (e.g. PT5M, P1D)
// into a time.Duration. Calendar units use fixed conversions: days and weeks
// are 24h and 7×24h; months and years are 30 and 365 days. No origin or
// timezone is used, so the result is a single Go time.Duration value suitable for
// timeouts, intervals, or other cases where fixed conversion is enough.
//
// To shift a specific time by an ISO 8601 duration (e.g. "one day from now"
// in local time, respecting DST), use github.com/senseyeio/duration: parse
// with duration.ParseISO8601 and call Shift(origin). Note that Shift uses
// time.AddDate for months and years, so month boundaries have the usual
// rollover behavior (e.g. Aug 31 + P1M → Oct 1).
func ParseISO8601Duration(s string) (time.Duration, error) {
	d, err := duration.ParseISO8601(s)
	if err != nil {
		return 0, err
	}
	return ApproximateDuration(d), nil
}

// ApproximateDuration converts a senseyeio/duration.Duration to a time.Duration.
// Calendar units use fixed conversions: days and weeks are 24h and 7×24h; months
// and years are 30 and 365 days. No origin or timezone is used, so the result is
// a single Go time.Duration value suitable for timeouts, intervals, or other
// cases where fixed conversion is enough.
func ApproximateDuration(d ISO8601Duration) time.Duration {
	return time.Duration(d.TS)*time.Second +
		time.Duration(d.TM)*time.Minute +
		time.Duration(d.TH)*time.Hour +
		time.Duration(d.D)*24*time.Hour +
		time.Duration(d.W)*7*24*time.Hour +
		time.Duration(d.M)*30*24*time.Hour +
		time.Duration(d.Y)*365*24*time.Hour
}
