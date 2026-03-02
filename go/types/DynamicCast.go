package types

// DynamicCast checks if the value is of type T, and if so, returns a pointer to it.
// If the value is not of type T, it returns nil.
//
// Equivalent to: `result, _ = v.(*T)`
//
// Example use:
//
// ```go
// import (
//
//	"github.com/earlye/eaux/go/types"
//	"github.com/go-errors/errors"
//	"log/slog"
//
// )
//
//	func foo( ) error {
//	    err := bar()
//	    if gerr := types.DynamicCast[errors.Error](err); gerr != nil {
//	        slog.Error(gerr.Message(), "stack", gerr.Stack())
//	    }
//	}
//
// ```
func DynamicCast[T interface{}](v interface{}) (result *T) {
	result, _ = v.(*T)
	return
}
