package fmtx

import (
	"bytes"
	"errors"
	"log/slog"
	"strings"
	"testing"
)

type failWriter struct{}

func (failWriter) Write(_ []byte) (int, error) {
	return 0, errors.New("write failed")
}

func TestFprintOrWarn_Success_WritesAndDoesNotLog(t *testing.T) {
	var logBuf strings.Builder
	prev := slog.Default()
	slog.SetDefault(slog.New(slog.NewTextHandler(&logBuf, &slog.HandlerOptions{Level: slog.LevelWarn})))
	t.Cleanup(func() { slog.SetDefault(prev) })

	var out bytes.Buffer
	FprintOrWarn(&out, "hello", 42)
	if got := out.String(); got != "hello42" {
		t.Fatalf("written output: got %q, want hello42", got)
	}
	if logBuf.Len() != 0 {
		t.Fatalf("expected no log on success, got:\n%s", logBuf.String())
	}
}

func TestFprintOrWarn_Error_LogsWarning(t *testing.T) {
	var logBuf strings.Builder
	prev := slog.Default()
	slog.SetDefault(slog.New(slog.NewTextHandler(&logBuf, &slog.HandlerOptions{Level: slog.LevelWarn})))
	t.Cleanup(func() { slog.SetDefault(prev) })

	FprintOrWarn(failWriter{}, "x")

	s := logBuf.String()
	if !strings.Contains(s, "Fprint failed") || !strings.Contains(s, "write failed") {
		t.Fatalf("expected warn log with error, got:\n%s", s)
	}
}
