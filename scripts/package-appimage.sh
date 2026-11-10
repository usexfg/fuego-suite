#!/usr/bin/env bash
# Assemble the Fuego AppDir, bundle non-system shared libraries, and build the AppImage.
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: scripts/package-appimage.sh <build-dir> <appdir> <output.AppImage> <appimagetool>
EOF
}

if [[ $# -ne 4 ]]; then
  usage
  exit 2
fi

build_dir=$1
appdir=$2
output=$3
appimagetool=$4

if [[ -z "$build_dir" || -z "$appdir" || "$appdir" == "/" || -z "$output" || -z "$appimagetool" ]]; then
  usage
  exit 2
fi

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
icon="$repo_root/docs/images/PolishedFuegoGonCharcoal.png"

if [[ ! -d "$build_dir/src" ]]; then
  echo "error: build directory has no src output: $build_dir" >&2
  exit 1
fi
if [[ ! -x "$appimagetool" ]]; then
  echo "error: appimagetool is not executable: $appimagetool" >&2
  exit 1
fi
if [[ ! -f "$icon" ]]; then
  echo "error: AppImage icon not found: $icon" >&2
  exit 1
fi

# Recreate the generated payload directories so stale binaries or libraries
# cannot leak into a release image.
rm -rf "$appdir/usr/bin" "$appdir/usr/lib" "$appdir/usr/share"
mkdir -p "$appdir/usr/bin"
mkdir -p "$appdir/usr/lib"
mkdir -p "$appdir/usr/share/applications"
mkdir -p "$appdir/usr/share/icons/hicolor/256x256/apps"

copied_bins=0
for exe in "$build_dir"/src/*; do
  if [[ -f "$exe" && -x "$exe" ]]; then
    install -m 755 "$exe" "$appdir/usr/bin/$(basename "$exe")"
    strip "$appdir/usr/bin/$(basename "$exe")"
    copied_bins=$((copied_bins + 1))
  fi
done

if [[ "$copied_bins" -eq 0 ]]; then
  echo "error: no executables found in $build_dir/src" >&2
  exit 1
fi
if [[ ! -x "$appdir/usr/bin/fuegod" ]]; then
  echo "error: fuegod was not produced by the build" >&2
  exit 1
fi

is_host_runtime_lib() {
  local name=$1
  case "$name" in
    linux-vdso.so.1|ld-linux*.so*|ld-musl*.so*|libc.so*|libm.so*|libmvec.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|libnsl.so*|libnss_*.so*|libanl.so*|libBrokenLocale.so*|libcidn.so*)
      return 0
      ;;
  esac
  return 1
}

tmp_libs=$(mktemp)
trap 'rm -f "$tmp_libs"' EXIT

while IFS= read -r -d '' exe; do
  ldd_output=$(ldd "$exe" 2>&1) || {
    echo "error: ldd failed for $exe" >&2
    echo "$ldd_output" >&2
    exit 1
  }
  if grep -q "not found" <<<"$ldd_output"; then
    echo "error: unresolved shared library dependency in $exe" >&2
    echo "$ldd_output" >&2
    exit 1
  fi
  sed -nE 's/^[^=]*=>[[:space:]]+([^[:space:]]+).*/\1/p; s|^[[:space:]]*(/[^[:space:]]+)[[:space:]].*|\1|p' <<<"$ldd_output" >> "$tmp_libs"
done < <(find "$appdir/usr/bin" -maxdepth 1 -type f -perm -111 -print0)

sort -u "$tmp_libs" -o "$tmp_libs"

copied_libs=0
while IFS= read -r lib; do
  [[ -n "$lib" ]] || continue
  if [[ ! -e "$lib" ]]; then
    echo "error: dependency path does not exist: $lib" >&2
    exit 1
  fi

  lib_name=$(basename "$lib")
  soname=$(readelf -d "$lib" 2>/dev/null | sed -nE 's/.*Library soname: \[([^]]+)\].*/\1/p' | head -n 1)
  if [[ -n "$soname" ]]; then
    lib_name=$(basename "$soname")
  fi
  if is_host_runtime_lib "$lib_name"; then
    continue
  fi

  dest="$appdir/usr/lib/$lib_name"
  if [[ ! -e "$dest" ]]; then
    cp -L "$lib" "$dest"
    chmod 644 "$dest"
  fi
  copied_libs=$((copied_libs + 1))
done < "$tmp_libs"

echo "Bundled $copied_libs shared libraries into $appdir/usr/lib"

cat > "$appdir/fuego.desktop" <<'DESKTOP_EOF'
[Desktop Entry]
Type=Application
Name=Fuego
Exec=fuegod
Icon=fuego
Categories=Network;
DESKTOP_EOF

cat > "$appdir/AppRun" <<'APPRUN_EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${HERE}/usr/bin/fuegod" "$@"
APPRUN_EOF
chmod +x "$appdir/AppRun"

cp "$icon" "$appdir/fuego.png"

# Fail before publishing if the packaged daemon still has an unresolvable
# dependency when the bundled library directory is preferred.
if LD_LIBRARY_PATH="$appdir/usr/lib" ldd "$appdir/usr/bin/fuegod" 2>&1 | grep -q "not found"; then
  echo "error: fuegod has unresolved dependencies after bundling" >&2
  LD_LIBRARY_PATH="$appdir/usr/lib" ldd "$appdir/usr/bin/fuegod" >&2 || true
  exit 1
fi

ARCH="${ARCH:-x86_64}" "$appimagetool" "$appdir" "$output"
