package logging

import (
	"context"
	"errors"
	"fmt"
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
	fmt.Printf("SlogHandler: format=%s, opts=%+v\n", format, opts)
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
