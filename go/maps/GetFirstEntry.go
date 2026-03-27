package maps

// GetFirstEntry finds the first key in keys where m[key] is present. If found, it returns m[key]; otherwise it returns defaultValue.
func GetFirstEntry[K comparable, V any](m map[K]V, keys []K, defaultValue V) V {
	for _, key := range keys {
		if v, ok := m[key]; ok {
			return v
		}
	}
	return defaultValue
}
