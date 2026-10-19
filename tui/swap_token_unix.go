//go:build darwin || linux

package main

import (
	"errors"
	"io"
	"os"
	"syscall"
)

func readSwapControlTokenFile(path string) (string, error) {
	fd, err := syscall.Open(path, syscall.O_RDONLY|syscall.O_CLOEXEC|syscall.O_NOFOLLOW, 0)
	if err != nil {
		return "", errors.New("cannot open swap daemon token file")
	}
	file := os.NewFile(uintptr(fd), path)
	if file == nil {
		_ = syscall.Close(fd)
		return "", errors.New("cannot access swap daemon token file")
	}
	defer file.Close()

	var metadata syscall.Stat_t
	if err := syscall.Fstat(fd, &metadata); err != nil {
		return "", errors.New("cannot inspect swap daemon token file")
	}
	if metadata.Mode&syscall.S_IFMT != syscall.S_IFREG ||
		int(metadata.Uid) != os.Geteuid() || metadata.Mode&0077 != 0 {
		return "", errors.New("swap daemon token file must be a regular owner-only file")
	}
	contents, err := io.ReadAll(io.LimitReader(file, 258))
	if err != nil || len(contents) == 258 {
		return "", errors.New("cannot read swap daemon token file")
	}
	return validateSwapControlToken(string(contents))
}
