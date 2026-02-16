package env

import "os"

// GetenvDefault returns the value of the environment variable key if set, otherwise fallback.
func GetenvDefault(key, fallback string) string {
	if v, ok := os.LookupEnv(key); ok {
		return v
	}
	return fallback
}
