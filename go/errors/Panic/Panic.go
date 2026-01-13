package panic

func Panic(err error) {
	if err != nil {
		panic(err)
	}
}

// Panic1 panics if the error is not nil, otherwise returns the value.
func Panic1[T any](value T, err error) T {
	Panic(err)
	return value
}
