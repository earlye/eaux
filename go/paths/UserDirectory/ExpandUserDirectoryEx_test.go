package userdirectory

import (
	"fmt"
	"testing"
)

func TestExpandUserDirectory(t *testing.T) {

	getUserDirectory := func(name string) (string, error) {
		switch name {
		case "bob":
			return "/users/bob", nil
		case "":
			fallthrough
		case "earlye":
			return "/users/earlye", nil
		}
		return "", fmt.Errorf("user not found: %s", name)
	}
	_ = getUserDirectory // suppress "unused" error
	noGetUserDirectory := func(name string) (string, error) {
		return "", fmt.Errorf("user not found: %s", name)
	}
	_ = noGetUserDirectory // suppress "unused" error
	tests := []struct {
		input            string
		expected         string
		expectError      bool
		getUserDirectory func(string) (string, error)
	}{
		{input: "~/Downloads", expected: "/users/earlye/Downloads", expectError: false, getUserDirectory: getUserDirectory},
		{input: "~bob/Downloads", expected: "/users/bob/Downloads", expectError: false, getUserDirectory: getUserDirectory},
		{input: "/foo/Downloads", expected: "/foo/Downloads", expectError: false, getUserDirectory: getUserDirectory},
		{input: "~/Downloads", expected: "", expectError: true, getUserDirectory: noGetUserDirectory},
	}
	fmt.Printf("Running %d tests\n", len(tests))
	for _, test := range tests {
		result, err := ExpandUserDirectoryEx(test.input, test.getUserDirectory)
		if test.expectError {
			if err == nil {
				t.Errorf("ExpandUserDirectoryEx(%q) = %q, %v; want error", test.input, result, err)
			}
		} else {
			if err != nil {
				t.Errorf("ExpandUserDirectoryEx(%q) = %q, %v; want no error", test.input, result, err)
			}
			if result != test.expected {
				t.Errorf("ExpandUserDirectory(%q) = %q; want %q", test.input, result, test.expected)
			}
		}
	}
}
