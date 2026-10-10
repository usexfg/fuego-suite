// Copyright (c) 2017-2026 Fuego Developers
//
// These functions were declared here for years but never defined or called, so
// every secret wipe in the tree was a plain memset that the optimiser is free to
// delete once the buffer is dead. Implemented in crypto-util.c.
//
// Prefer these over memset for key material, nonces and other secrets.

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif
// We borrow from https://libsodium.org/
void sodium_memzero(void *pnt, size_t length);
int sodium_compare(const void *a1, const void *a2, size_t length);

#if defined(__cplusplus)
}
#endif