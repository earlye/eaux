package expanduserdirectory

import (
	"os/user"
)

// GetUserDirectoryEx gets the user directory for a given name, using the provided functions to get the user directory.
// An empty name for the user will return the home directory of the current user.
// If the directory cannot be resolved, an error is returned.
func GetUserDirectoryEx(name string, userHomeDir func() (string, error), userLookup func(string) (*user.User, error)) (result string, err error) {
	if name == "" {
		result, err = userHomeDir()
		return
	}

	u, err := userLookup(name)
	if err != nil {
		return
	}
	result = u.HomeDir
	return
}
