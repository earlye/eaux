package logging

import (
	"log/slog"
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
