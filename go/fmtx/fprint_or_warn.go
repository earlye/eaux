package fmtx

import (
	"fmt"
	"io"
	"log/slog"
)

// FprintOrWarn writes to w using [fmt.Fprint]. If the write fails, it logs a warning with [slog.Warn]
// and swallows n and err from the underlying call.
func FprintOrWarn(w io.Writer, a ...any) {
	_, err := fmt.Fprint(w, a...)
	if err != nil {
		slog.Warn("Fprint failed", "error", err)
	}
}
