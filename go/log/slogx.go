package logging

import (
	"context"
	"errors"
	"log/slog"
	"os"
)

const LevelSilly = slog.Level(-8)
const LevelTrace = slog.Level(-6)

// Create a slog.Handler based on the format string.
func SlogHandler(format string, opts *slog.HandlerOptions) slog.Handler {
	switch format {
	case "json":
		return slog.NewJSONHandler(os.Stderr, opts)
	default:
		return slog.NewTextHandler(os.Stderr, opts)
	}
}

// Map verbosity flag to slog.Level (SILLY/TRACE -> -8/-6 ; DEBUG, INFO, WARN, ERROR).
func SlogLevel(v string) (slog.Level, error) {
	switch v {
	case "SILLY":
		return slog.Level(-8), nil
	case "TRACE":
		return slog.Level(-6), nil
	case "DEBUG":
		return slog.LevelDebug, nil
	case "INFO":
		return slog.LevelInfo, nil
	case "WARN":
		return slog.LevelWarn, nil
	case "ERROR":
		return slog.LevelError, nil
	default:
		return 0, errors.New("invalid verbosity: " + v)
	}
}

func Silly(msg string, args ...any) {
	slog.Default().Log(context.Background(), LevelSilly, msg, args...)
}

func SillyContext(ctx context.Context, msg string, args ...any) {
	slog.Default().Log(ctx, LevelSilly, msg, args...)
}

func Trace(msg string, args ...any) {
	slog.Default().Log(context.Background(), LevelTrace, msg, args...)
}

func TraceContext(ctx context.Context, msg string, args ...any) {
	slog.Default().Log(ctx, LevelTrace, msg, args...)
}
