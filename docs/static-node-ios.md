# Drop-in Static Node for iOS

This is the scaffolding for embedding the Fuego node as a static library
instead of launching a separate `fuegod` process. It is intended for iOS
(and other restricted environments) where background daemons are not allowed
and mining is disabled.

## What is included

- `src/StaticNode/fuego_node.h` — C API (`fuego_node_start` / `stop` / status).
- `src/StaticNode/fuego_node.cpp` — Stub implementation so the library links.
- `src/StaticNode/CMakeLists.txt` — `FuegoNodeStatic` target.
- `cmake/ios.toolchain.cmake` — Minimal iOS toolchain skeleton.

## Build (device)

```bash
cmake -B build-ios \
  -DCMAKE_TOOLCHAIN_FILE=cmake/ios.toolchain.cmake \
  -DBUILD_STATIC_NODE=ON \
  -DENABLE_FUEGOTOR=OFF
cmake --build build-ios --target FuegoNodeStatic
```

The resulting `libFuegoNodeStatic.a` can be linked into a Flutter/iOS app
or a plain Xcode project. A thin Obj-C / Dart FFI wrapper is still required
to call the C API from the app.

## What is still needed

1. Wire `fuego_node.cpp` to the real `InProcessNode` + `CryptoNoteCore`
   startup path (currently a no-op stub).
2. Pre-built static Boost and OpenSSL for the iOS SDK.
3. Resource-aware sync controls (battery / thermal / network) so a phone
   node does not thrash.
4. Flutter FFI bindings in `fuego-valise` so the app can start the node
   without spawning `unifyd`.

## Notes

- Mining is not exposed. The library is for validation and wallet service
  only.
- The existing ARM64/NEON path in `src/crypto/slow-hash.c` is already used
  by the core and does not need to be re-implemented here.
