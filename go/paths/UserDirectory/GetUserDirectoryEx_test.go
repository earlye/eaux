package userdirectory

import (
	"fmt"
	"os/user"
	"testing"
)

func TestGetUserDirectoryEx(t *testing.T) {
	userHomeDir := func() (string, error) {
		return "/users/earlye", nil
	}
	noUserHomeDir := func() (string, error) {
		return "", fmt.Errorf("no user home directory")
	}
	userLookup := func(name string) (*user.User, error) {
		switch name {
		case "bob":
			return &user.User{HomeDir: "/users/bob"}, nil
		case "":
			fallthrough
		case "earlye":
			return &user.User{HomeDir: "/users/earlye"}, nil
		}
		return nil, fmt.Errorf("user not found: %s", name)
	}
	tests := []struct {
		input       string
		expected    string
		expectError bool
		userHomeDir func() (string, error)
	}{
		{input: "", expected: "/users/earlye", expectError: false, userHomeDir: userHomeDir},
		{input: "", expected: "", expectError: true, userHomeDir: noUserHomeDir},

		{input: "bob", expected: "/users/bob", expectError: false, userHomeDir: userHomeDir},
		{input: "earlye", expected: "/users/earlye", expectError: false, userHomeDir: userHomeDir},
		{input: "notfound", expected: "", expectError: true, userHomeDir: userHomeDir},
	}
	fmt.Printf("Running %d tests\n", len(tests))
	for _, test := range tests {
		result, err := GetUserDirectoryEx(test.input, test.userHomeDir, userLookup)
		if test.expectError {
			if err == nil {
				t.Errorf("GetUserDirectoryEx(%q) = %q, %v; want error", test.input, result, err)
			}
		} else {
			if err != nil {
				t.Errorf("GetUserDirectoryEx(%q) = %q, %v; want no error", test.input, result, err)
			}
		}
		if result != test.expected {
			t.Errorf("GetUserDirectory(%q) = %q; want %q", test.input, result, test.expected)
		}
	}
}
