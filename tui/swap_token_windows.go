//go:build windows

package main

import "errors"

func readSwapControlTokenFile(string) (string, error) {
	return "", errors.New("swap daemon token files require a verified owner-only filesystem")
}
