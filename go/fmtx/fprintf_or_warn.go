package fmtx

import (
	"fmt"
	"io"
	"log/slog"
)

// FprintfOrWarn writes to w using [fmt.Fprintf]. If the write fails, it logs a warning with [slog.Warn]
// and swallows n and err from the underlying call.
func FprintfOrWarn(w io.Writer, format string, a ...any) {
	_, err := fmt.Fprintf(w, format, a...)
	if err != nil {
		slog.Warn("Fprintf failed", "error", err)
	}
}
