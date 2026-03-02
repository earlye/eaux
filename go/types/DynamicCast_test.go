package types

import (
	"fmt"
	"testing"
)

func TestDynamicCast(t *testing.T) {
	one := 1
	hello := "hello"
	tests := []struct {
		name    string
		v       interface{}
		wantNil bool
		wantVal interface{} // when wantNil is false, expected dereferenced value
	}{
		{
			name:    "nil value returns nil",
			v:       nil,
			wantNil: true,
		},
		{
			name:    "wrong type returns nil",
			v:       "not an int",
			wantNil: true,
		},
		{
			name:    "value type (not pointer) returns nil",
			v:       42,
			wantNil: true,
		},
		{
			name:    "pointer to int",
			v:       &one,
			wantNil: false,
			wantVal: 1,
		},
		{
			name:    "pointer to string yields nil when asserting *int",
			v:       &hello,
			wantNil: true,
		},
		{
			name:    "nil pointer to int",
			v:       (*int)(nil),
			wantNil: true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := DynamicCast[int](tt.v)
			if tt.wantNil {
				if got != nil {
					t.Errorf("DynamicCast[int](%v) = %v; want nil", tt.v, got)
				}
				return
			}
			if got == nil {
				t.Errorf("DynamicCast[int](%v) = nil; want non-nil", tt.v)
				return
			}
			if *got != tt.wantVal {
				t.Errorf("DynamicCast[int](%v) = %v; want %v", tt.v, *got, tt.wantVal)
			}
		})
	}
}

func TestDynamicCast_string(t *testing.T) {
	hello := "hello"
	got := DynamicCast[string](&hello)
	if got == nil {
		t.Fatal("DynamicCast[string](&hello) = nil; want non-nil")
	}
	if *got != "hello" {
		t.Errorf("DynamicCast[string](&hello) = %q; want %q", *got, "hello")
	}

	// Wrong type
	got = DynamicCast[string](nil)
	if got != nil {
		t.Errorf("DynamicCast[string](nil) = %v; want nil", got)
	}
}

func TestDynamicCast_equivalent_to_type_assertion(t *testing.T) {
	one := 1
	v := interface{}(&one)

	// DynamicCast should behave like: result, _ = v.(*int)
	result := DynamicCast[int](v)
	if result == nil {
		t.Fatal("expected non-nil")
	}
	if *result != 1 {
		t.Errorf("got %d; want 1", *result)
	}

	// Direct assertion for comparison
	direct, ok := v.(*int)
	if !ok || direct == nil {
		t.Fatal("direct assertion failed")
	}
	if *result != *direct {
		t.Errorf("DynamicCast result %d != direct assertion %d", *result, *direct)
	}
}

// testError is an error-derived type used to verify that DynamicCast can downcast
// from error (interface{}) to a concrete *T. This demonstrates the same pattern
// used with github.com/go-errors/errors: if gerr := types.DynamicCast[errors.Error](err); gerr != nil { ... }
type testError struct {
	msg string
}

func (e *testError) Error() string { return e.msg }

// TestDynamicCast_errorType verifies that we can downcast an error to a concrete
// error-derived type and use its methods. This is the primary use case for
// DynamicCast (e.g. downcasting to *go-errors/errors.Error) without depending on go-errors.
func TestDynamicCast_errorType(t *testing.T) {
	err := &testError{msg: "something failed"}

	// Pass as error (interface{}); downcast to *testError.
	got := DynamicCast[testError](err)
	if got == nil {
		t.Fatal("DynamicCast[testError](error value) = nil; want non-nil")
	}
	if got.Error() != "something failed" {
		t.Errorf("got.Error() = %q; want %q", got.Error(), "something failed")
	}

	// Plain stdlib error must not cast to *testError.
	plainErr := fmt.Errorf("plain error")
	got = DynamicCast[testError](plainErr)
	if got != nil {
		t.Errorf("DynamicCast[testError](stdlib error) = %v; want nil", got)
	}

	// nil error must yield nil.
	got = DynamicCast[testError](error(nil))
	if got != nil {
		t.Errorf("DynamicCast[testError](nil) = %v; want nil", got)
	}
}

// Example: show that failed cast returns nil (no panic).
func ExampleDynamicCast_fail() {
	var v interface{} = "not an int"
	result := DynamicCast[int](v)
	fmt.Println(result == nil)
	// Output: true
}

// Example: successful cast returns pointer to value.
func ExampleDynamicCast_ok() {
	x := 42
	var v interface{} = &x
	result := DynamicCast[int](v)
	if result != nil {
		fmt.Println(*result)
	}
	// Output: 42
}
