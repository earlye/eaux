// coverage-ignore-file
package expanduserdirectory

// ExpandUserDirectory expands a path possibly prefixed with `~` to the full path.
func ExpandUserDirectory(path string) (result string, err error) {
	return ExpandUserDirectoryEx(path, GetUserDirectory)
}
