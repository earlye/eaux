package fmtx

import (
	"bytes"
	"log/slog"
	"strings"
	"testing"
)

func TestFprintfOrWarn_Success_WritesAndDoesNotLog(t *testing.T) {
	var logBuf strings.Builder
	prev := slog.Default()
	slog.SetDefault(slog.New(slog.NewTextHandler(&logBuf, &slog.HandlerOptions{Level: slog.LevelWarn})))
	t.Cleanup(func() { slog.SetDefault(prev) })

	var out bytes.Buffer
	FprintfOrWarn(&out, "%s%d", "hello", 42)
	if got := out.String(); got != "hello42" {
		t.Fatalf("written output: got %q, want hello42", got)
	}
	if logBuf.Len() != 0 {
		t.Fatalf("expected no log on success, got:\n%s", logBuf.String())
	}
}

func TestFprintfOrWarn_Error_LogsWarning(t *testing.T) {
	var logBuf strings.Builder
	prev := slog.Default()
	slog.SetDefault(slog.New(slog.NewTextHandler(&logBuf, &slog.HandlerOptions{Level: slog.LevelWarn})))
	t.Cleanup(func() { slog.SetDefault(prev) })

	FprintfOrWarn(failWriter{}, "%s", "x")

	s := logBuf.String()
	if !strings.Contains(s, "Fprintf failed") || !strings.Contains(s, "write failed") {
		t.Fatalf("expected warn log with error, got:\n%s", s)
	}
}
