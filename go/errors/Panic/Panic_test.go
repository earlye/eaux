package panic

import (
	"fmt"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestPanicNoError(t *testing.T) {
	Panic(nil)
}

func TestPanicError(t *testing.T) {
	assert.Panics(t, func() { Panic(fmt.Errorf("test error")) })
}

func TestPanic1(t *testing.T) {
	check := Panic1(1, nil)
	assert.Equal(t, 1, check)
}

func TestPanic1Error(t *testing.T) {
	assert.Panics(t, func() { Panic1(1, fmt.Errorf("test error")) })
}

func TestPanic1Function(t *testing.T) {
	fn := func() (string, error) { return "test", nil }
	check := Panic1(fn())
	assert.Equal(t, "test", check)
}

func TestPanic1FunctionError(t *testing.T) {
	fn := func() (string, error) { return "", fmt.Errorf("test error") }
	assert.Panics(t, func() { Panic1(fn()) })
}
