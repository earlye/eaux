package strings

import stdstrings "strings"

// SplitTrim splits input by pattern and trims space from each resulting element.
func SplitTrim(input string, pattern string) []string {
	entries := stdstrings.Split(input, pattern)
	for i := range entries {
		entries[i] = stdstrings.TrimSpace(entries[i])
	}
	return entries
}
