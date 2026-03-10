package maps

// HasEntry returns true if the key is present in m.
func HasEntry[K comparable, V any](m map[K]V, key K) bool {
	_, ok := m[key]
	return ok
}
