package env

import (
	"os"
	"strings"
)

// Map returns a map of key=value entries from environ (e.g. os.Environ()).
// Entries without '=' are skipped.
func Map(environ []string) map[string]string {
	result := make(map[string]string, len(environ))
	for _, e := range environ {
		i := strings.IndexByte(e, '=')
		if i < 0 {
			continue
		}
		key := e[:i]
		value := e[i+1:]
		result[key] = value
	}
	return result
}

// GetenvDefault returns the value of the environment variable key if set, otherwise fallback.
func GetenvDefault(key, fallback string) string {
	if v, ok := os.LookupEnv(key); ok {
		return v
	}
	return fallback
}
