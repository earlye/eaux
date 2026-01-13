// coverage-ignore-file
package userdirectory

import (
	"os"
	"os/user"
)

// GetUserDirectory gets the user directory for a given name.
// An empty name for the user will return the home directory of the current user.
// If the directory cannot be resolved, an error is returned.
func GetUserDirectory(name string) (result string, err error) {
	return GetUserDirectoryEx(name, os.UserHomeDir, user.Lookup)
}
