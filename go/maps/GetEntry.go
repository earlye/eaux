package maps

// GetEntry returns m[key] if the key is present; otherwise it returns defaultValue.
func GetEntry[K comparable, V any](m map[K]V, key K, defaultValue V) V {
	v, ok := m[key]
	if !ok {
		return defaultValue
	}
	return v
}
