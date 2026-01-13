package expanduserdirectory

import (
	"path/filepath"
	"strings"
)

// ExpandUserDirectoryEx expands a path to the full path, using the provided function to get user directories for `~` prefixes.
func ExpandUserDirectoryEx(path string, getUserDirectory func(string) (string, error)) (result string, err error) {
	if strings.HasPrefix(path, "~") {
		parts := strings.Split(path, string(filepath.Separator))
		user := parts[0][1:]
		var userDir string
		userDir, err = getUserDirectory(user)
		if err != nil {
			return
		}
		result = filepath.Join(userDir, strings.Join(parts[1:], "/"))
		return
	}
	result = path
	return
}
