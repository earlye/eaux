package logging

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"os"
	"strings"
	"testing"
)

func TestSlogHandler(t *testing.T) {
	tests := []struct {
		name   string
		format string
		opts   *slog.HandlerOptions
	}{
		{"json format", "json", nil},
		{"json with opts", "json", &slog.HandlerOptions{Level: slog.LevelDebug}},
		{"text format (default)", "text", nil},
		{"empty format uses text", "", nil},
		{"unknown format uses text", "unknown", nil},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := SlogHandler(tt.format, tt.opts)
			if got == nil {
				t.Error("SlogHandler returned nil")
			}
		})
	}
}

// TestSlogHandler_levelKey checks that ReplaceAttr maps the "level" attribute to string labels:
// standard slog levels (DEBUG/INFO/WARN/ERROR) plus SILLY and TRACE.
func TestSlogHandler_levelKey(t *testing.T) {
	cases := []struct {
		level slog.Level
		want  string
	}{
		{slog.LevelDebug, "DEBUG"},
		{slog.LevelInfo, "INFO"},
		{slog.LevelWarn, "WARN"},
		{slog.LevelError, "ERROR"},
		{LevelSilly, "SILLY"},
		{LevelTrace, "TRACE"},
	}

	t.Run("json", func(t *testing.T) {
		for _, tc := range cases {
			tc := tc
			t.Run(tc.want, func(t *testing.T) {
				out := captureStderrSlogJSON(t, func(l *slog.Logger) {
					l.Log(context.Background(), tc.level, "x")
				})
				var m map[string]json.RawMessage
				if err := json.Unmarshal(bytes.TrimSpace(out), &m); err != nil {
					t.Fatalf("unmarshal JSON: %v\n%s", err, out)
				}
				raw, ok := m["level"]
				if !ok {
					t.Fatalf("missing level key in JSON:\n%s", out)
				}
				var got string
				if err := json.Unmarshal(raw, &got); err != nil {
					t.Fatalf("level value: %v", err)
				}
				if got != tc.want {
					t.Errorf("level = %q, want %q", got, tc.want)
				}
			})
		}
	})

	t.Run("text", func(t *testing.T) {
		for _, tc := range cases {
			tc := tc
			t.Run(tc.want, func(t *testing.T) {
				out := captureStderrSlogText(t, func(l *slog.Logger) {
					l.Log(context.Background(), tc.level, "x")
				})
				needle := "level=" + tc.want
				if !strings.Contains(string(out), needle) {
					t.Errorf("expected output to contain %q, got:\n%s", needle, out)
				}
			})
		}
	})
}

func captureStderrSlogJSON(t *testing.T, fn func(*slog.Logger)) []byte {
	t.Helper()
	return captureStderrSlog(t, "json", fn)
}

func captureStderrSlogText(t *testing.T, fn func(*slog.Logger)) []byte {
	t.Helper()
	return captureStderrSlog(t, "text", fn)
}

func captureStderrSlog(t *testing.T, format string, fn func(*slog.Logger)) []byte {
	t.Helper()
	opts := &slog.HandlerOptions{Level: LevelSilly}
	return captureStderrSlogWithOpts(t, format, opts, fn)
}

// captureStderrSlogWithOpts builds [SlogHandler] with the given options (after [SlogHandler]'s own
// ReplaceAttr wrapping) and captures bytes written to [os.Stderr].
func captureStderrSlogWithOpts(t *testing.T, format string, opts *slog.HandlerOptions, fn func(*slog.Logger)) []byte {
	t.Helper()
	orig := os.Stderr
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	os.Stderr = w

	logger := slog.New(SlogHandler(format, opts))
	fn(logger)

	if err := w.Close(); err != nil {
		os.Stderr = orig
		r.Close()
		t.Fatal(err)
	}
	out, err := io.ReadAll(r)
	os.Stderr = orig
	if cerr := r.Close(); cerr != nil {
		t.Fatal(cerr)
	}
	if err != nil {
		t.Fatal(err)
	}
	return out
}

// captureStderrWithSlogHandlerAsDefault redirects [os.Stderr] to a pipe, sets [slog.Default] to a
// logger from [SlogHandler] (JSON), runs fn (e.g. [Silly] / [Trace]), restores stderr and the
// default logger, and returns captured stderr bytes.
func captureStderrWithSlogHandlerAsDefault(t *testing.T, opts *slog.HandlerOptions, fn func()) []byte {
	t.Helper()
	origStderr := os.Stderr
	origDefault := slog.Default()
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	os.Stderr = w
	slog.SetDefault(slog.New(SlogHandler("json", opts)))

	fn()

	if err := w.Close(); err != nil {
		os.Stderr = origStderr
		r.Close()
		slog.SetDefault(origDefault)
		t.Fatal(err)
	}
	out, err := io.ReadAll(r)
	os.Stderr = origStderr
	slog.SetDefault(origDefault)
	if cerr := r.Close(); cerr != nil {
		t.Fatal(cerr)
	}
	if err != nil {
		t.Fatal(err)
	}
	return out
}

// TestSlogHandler_originalReplacerCalled ensures opts.ReplaceAttr is still invoked after
// SlogHandler wraps it (the level-key branch runs first, then originalReplacer).
func TestSlogHandler_originalReplacerCalled(t *testing.T) {
	var originalCalls int
	opts := &slog.HandlerOptions{
		Level: LevelSilly,
		ReplaceAttr: func(groups []string, a slog.Attr) slog.Attr {
			originalCalls++
			return a
		},
	}

	_ = captureStderrSlogWithOpts(t, "json", opts, func(l *slog.Logger) {
		l.Info("x")
	})

	if originalCalls == 0 {
		t.Fatal("expected opts.ReplaceAttr to be called at least once")
	}
}

func TestSilly_Trace_helpers(t *testing.T) {
	tests := []struct {
		name      string
		run       func()
		wantLevel slog.Level
	}{
		{
			name: "Silly",
			run: func() {
				Silly("hello", "k", "v")
			},
			wantLevel: LevelSilly,
		},
		{
			name: "SillyContext",
			run: func() {
				SillyContext(context.Background(), "hello", "k", "v")
			},
			wantLevel: LevelSilly,
		},
		{
			name: "Trace",
			run: func() {
				Trace("hello", "k", "v")
			},
			wantLevel: LevelTrace,
		},
		{
			name: "TraceContext",
			run: func() {
				TraceContext(context.Background(), "hello", "k", "v")
			},
			wantLevel: LevelTrace,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var buf strings.Builder
			prev := slog.Default()
			h := slog.NewJSONHandler(&buf, &slog.HandlerOptions{Level: LevelSilly})
			slog.SetDefault(slog.New(h))
			t.Cleanup(func() { slog.SetDefault(prev) })

			tt.run()

			var m map[string]json.RawMessage
			line := bytes.TrimSpace([]byte(buf.String()))
			if len(line) == 0 {
				t.Fatal("no log output")
			}
			if err := json.Unmarshal(line, &m); err != nil {
				t.Fatalf("json: %v\n%s", err, buf.String())
			}
			var level string
			if err := json.Unmarshal(m["level"], &level); err != nil {
				t.Fatalf("level: %v", err)
			}
			if level != tt.wantLevel.String() {
				t.Errorf("level = %q, want %q", level, tt.wantLevel.String())
			}
			var msg string
			if err := json.Unmarshal(m["msg"], &msg); err != nil {
				t.Fatalf("msg: %v", err)
			}
			if msg != "hello" {
				t.Errorf("msg = %q, want %q", msg, "hello")
			}
			var k string
			if err := json.Unmarshal(m["k"], &k); err != nil {
				t.Fatalf("k: %v", err)
			}
			if k != "v" {
				t.Errorf("k = %q, want %q", k, "v")
			}
		})
	}
}

// TestSilly_Trace_helpers_SlogHandler_levelStrings checks that [SlogHandler]'s level ReplaceAttr
// emits "SILLY" and "TRACE" for the custom levels when using [Silly], [SillyContext], [Trace], and [TraceContext].
func TestSilly_Trace_helpers_SlogHandler_levelStrings(t *testing.T) {
	tests := []struct {
		name      string
		run       func()
		wantLabel string
	}{
		{
			name: "Silly",
			run: func() {
				Silly("hello")
			},
			wantLabel: "SILLY",
		},
		{
			name: "SillyContext",
			run: func() {
				SillyContext(context.Background(), "hello")
			},
			wantLabel: "SILLY",
		},
		{
			name: "Trace",
			run: func() {
				Trace("hello")
			},
			wantLabel: "TRACE",
		},
		{
			name: "TraceContext",
			run: func() {
				TraceContext(context.Background(), "hello")
			},
			wantLabel: "TRACE",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Fresh opts each run: [SlogHandler] assigns opts.ReplaceAttr in place.
			opts := &slog.HandlerOptions{Level: LevelSilly}
			out := captureStderrWithSlogHandlerAsDefault(t, opts, tt.run)
			var m map[string]json.RawMessage
			if err := json.Unmarshal(bytes.TrimSpace(out), &m); err != nil {
				t.Fatalf("json: %v\n%s", err, out)
			}
			var level string
			if err := json.Unmarshal(m["level"], &level); err != nil {
				t.Fatalf("level: %v", err)
			}
			if level != tt.wantLabel {
				t.Errorf("level = %q, want %q", level, tt.wantLabel)
			}
			var msg string
			if err := json.Unmarshal(m["msg"], &msg); err != nil {
				t.Fatalf("msg: %v", err)
			}
			if msg != "hello" {
				t.Errorf("msg = %q, want %q", msg, "hello")
			}
		})
	}
}

func TestSlogLevel(t *testing.T) {
	tests := []struct {
		name    string
		v       string
		want    slog.Level
		wantErr bool
	}{
		{"SILLY", "SILLY", LevelSilly, false},
		{"TRACE", "TRACE", LevelTrace, false},
		{"DEBUG", "DEBUG", slog.LevelDebug, false},
		{"INFO", "INFO", slog.LevelInfo, false},
		{"WARN", "WARN", slog.LevelWarn, false},
		{"ERROR", "ERROR", slog.LevelError, false},
		{"invalid empty", "", 0, true},
		{"invalid value", "FOO", 0, true},
		{"lowercase", "info", 0, true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := SlogLevel(tt.v)
			if (err != nil) != tt.wantErr {
				t.Errorf("SlogLevel(%q) error = %v, wantErr %v", tt.v, err, tt.wantErr)
				return
			}
			if !tt.wantErr && got != tt.want {
				t.Errorf("SlogLevel(%q) = %v, want %v", tt.v, got, tt.want)
			}
			if tt.wantErr && err != nil && err.Error() != "invalid verbosity: "+tt.v {
				t.Errorf("SlogLevel(%q) err = %v", tt.v, err)
			}
		})
	}
}

func TestDeferredSwallowFailureWithLog_noError_noLog(t *testing.T) {
	var logBuf strings.Builder
	prev := slog.Default()
	slog.SetDefault(slog.New(slog.NewTextHandler(&logBuf, &slog.HandlerOptions{Level: slog.LevelWarn})))
	t.Cleanup(func() { slog.SetDefault(prev) })

	func() {
		defer DeferredSwallowFailureWithLog(func() error { return nil })
	}()

	if logBuf.Len() != 0 {
		t.Fatalf("expected no log when f returns nil, got:\n%s", logBuf.String())
	}
}

func TestDeferredSwallowFailureWithLog_error_logs(t *testing.T) {
	var logBuf strings.Builder
	prev := slog.Default()
	slog.SetDefault(slog.New(slog.NewTextHandler(&logBuf, &slog.HandlerOptions{Level: slog.LevelWarn})))
	t.Cleanup(func() { slog.SetDefault(prev) })

	errBoom := errors.New("boom")
	func() {
		defer DeferredSwallowFailureWithLog(func() error { return errBoom })
	}()

	s := logBuf.String()
	if !strings.Contains(s, "Swallowing error") || !strings.Contains(s, "boom") {
		t.Fatalf("expected warn log with error, got:\n%s", s)
	}
}

func TestDeferredSwallowFailureWithLog_fRunsWhenDeferRuns(t *testing.T) {
	var called bool
	func() {
		defer DeferredSwallowFailureWithLog(func() error {
			called = true
			return nil
		})
		if called {
			t.Fatal("f must not run until the defer runs (when this function returns)")
		}
	}()
	if !called {
		t.Fatal("f should have run after the deferred function ran")
	}
}

func TestDeferredSwallowFailureWithLog1_noError_noLog(t *testing.T) {
	var logBuf strings.Builder
	prev := slog.Default()
	slog.SetDefault(slog.New(slog.NewTextHandler(&logBuf, &slog.HandlerOptions{Level: slog.LevelWarn})))
	t.Cleanup(func() { slog.SetDefault(prev) })

	func() {
		defer DeferredSwallowFailureWithLog1(func() (int, error) { return 42, nil })
	}()

	if logBuf.Len() != 0 {
		t.Fatalf("expected no log when f returns nil error, got:\n%s", logBuf.String())
	}
}

func TestDeferredSwallowFailureWithLog1_error_logs(t *testing.T) {
	var logBuf strings.Builder
	prev := slog.Default()
	slog.SetDefault(slog.New(slog.NewTextHandler(&logBuf, &slog.HandlerOptions{Level: slog.LevelWarn})))
	t.Cleanup(func() { slog.SetDefault(prev) })

	errBoom := errors.New("boom")
	func() {
		defer DeferredSwallowFailureWithLog1(func() (string, error) { return "", errBoom })
	}()

	s := logBuf.String()
	if !strings.Contains(s, "Swallowing error") || !strings.Contains(s, "boom") {
		t.Fatalf("expected warn log with error, got:\n%s", s)
	}
}

func TestDeferredSwallowFailureWithLog1_fRunsWhenDeferRuns(t *testing.T) {
	var called bool
	func() {
		defer DeferredSwallowFailureWithLog1(func() (int, error) {
			called = true
			return 0, nil
		})
		if called {
			t.Fatal("f must not run until the defer runs (when this function returns)")
		}
	}()
	if !called {
		t.Fatal("f should have run after the deferred function ran")
	}
}
