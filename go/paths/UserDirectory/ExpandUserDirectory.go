package userdirectory

// coverage-ignore-file

// ExpandUserDirectory expands a path possibly prefixed with `~` to the full path.
//
// Equivalent to: `ExpandUserDirectoryEx(path, GetUserDirectory)`
func ExpandUserDirectory(path string) (result string, err error) {
	return ExpandUserDirectoryEx(path, GetUserDirectory)
}
