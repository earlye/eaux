# github.com/earlye/eaux/go/types

This package provides some helpers for type management.

Example use:

```
import (
    "github.com/earlye/eaux/go/types"
    "github.com/go-errors/errors"
    "log/slog"
)

func foo( ) error {
    err := bar()
    if gerr := types.DynamicCast[errors.Error](err); gerr != nil {
        slog.Error(gerr.Message(), "stack", gerr.Stack())
    }
}
```