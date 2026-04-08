package logging

import (
	"context"
	"errors"
	"log/slog"
	"os"
)

const LevelSilly = slog.Level(-8)
const LevelTrace = slog.Level(-6)

type ReplaceAttrFunc func(groups []string, a slog.Attr) slog.Attr

// Create a slog.Handler based on the format string.
func SlogHandler(format string, opts *slog.HandlerOptions) slog.Handler {
	if opts == nil {
		opts = &slog.HandlerOptions{
			ReplaceAttr: nil,
		}
	}
	originalReplacer := opts.ReplaceAttr
	opts.ReplaceAttr = func(groups []string, a slog.Attr) slog.Attr {
		if a.Key == "level" {
			level, ok := a.Value.Any().(slog.Level)
			if ok {
				a.Value = slog.StringValue(LevelString(slog.Level(level)))
			}
		}
		if originalReplacer != nil {
			return originalReplacer(groups, a)
		}
		return a
	}
	switch format {
	case "json":
		return slog.NewJSONHandler(os.Stderr, opts)
	default:
		return slog.NewTextHandler(os.Stderr, opts)
	}
}

func LevelString(level slog.Level) string {
	switch level {
	case LevelSilly:
		return "SILLY"
	case LevelTrace:
		return "TRACE"
	default:
		return level.String()
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

// Silly logs silly-level messages.
func Silly(msg string, args ...any) {
	slog.Default().Log(context.Background(), LevelSilly, msg, args...)
}

// SillyContext logs silly-level messages with context
func SillyContext(ctx context.Context, msg string, args ...any) {
	slog.Default().Log(ctx, LevelSilly, msg, args...)
}

// Trace logs trace-level messages
func Trace(msg string, args ...any) {
	slog.Default().Log(context.Background(), LevelTrace, msg, args...)
}

// TraceContext logs trace-level messages with context
func TraceContext(ctx context.Context, msg string, args ...any) {
	slog.Default().Log(ctx, LevelTrace, msg, args...)
}

// DeferredSwallowFailureWithLog logs an error and "swallows" it. This is mainly useful for situations
// where there isn't anything you can do with an error.
// For example:
//
//	 defer DeferredSwallowFailureWithLog(CloseSomeResource())
//		  // What can you actually do if the resource fails to close?
//		  // Nothing really; your main other option is to panic.
func DeferredSwallowFailureWithLog(f func() error) {
	err := f()
	if err != nil {
		slog.Warn("Swallowing error", "error", err)
	}
}

// DeferredSwallowFailureWithLog1 logs an error and "swallows" it along with the return value.
// This is mainly useful for situations where there isn't anything you can do with an error or return value.
// For example:
//
//	 defer DeferredSwallowFailureWithLog1(CloseSomeResource())
//		  // What can you actually do if the resource fails to close?
//		  // Nothing really; your main other option is to panic.
func DeferredSwallowFailureWithLog1[T any](f func() (T, error)) T {
	v, err := f()
	if err != nil {
		slog.Warn("Swallowing error", "error", err)
	}
	return v
}
