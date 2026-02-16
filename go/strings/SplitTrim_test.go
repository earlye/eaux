package strings

import (
	"reflect"
	"testing"
)

func TestSplitTrim(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		pattern string
		want    []string
	}{
		{"empty input", "", ",", []string{""}},
		{"single element", "a", ",", []string{"a"}},
		{"single with spaces", "  a  ", ",", []string{"a"}},
		{"comma separated", "a,b,c", ",", []string{"a", "b", "c"}},
		{"comma with spaces", " a , b , c ", ",", []string{"a", "b", "c"}},
		{"pipe separator", "x|y|z", "|", []string{"x", "y", "z"}},
		{"empty segments", "a,,b", ",", []string{"a", "", "b"}},
		{"only separator", ",", ",", []string{"", ""}},
		{"leading trailing sep", ",a,b,", ",", []string{"", "a", "b", ""}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := SplitTrim(tt.input, tt.pattern)
			if !reflect.DeepEqual(got, tt.want) {
				t.Errorf("SplitTrim(%q, %q) = %v, want %v", tt.input, tt.pattern, got, tt.want)
			}
		})
	}
}
