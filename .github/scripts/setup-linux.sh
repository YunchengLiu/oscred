#!/usr/bin/env bash
set -euo pipefail

preset=${1:?Pass the Linux preset name}
tools_dir=$(realpath -m "build/$preset/tools")
mkdir -p "$tools_dir/bin"

packages=(libsecret-1-dev libglib2.0-dev gnome-keyring)
case "$preset" in
    linux-gcc-*)
        # Ubuntu 24.04 needs GCC 15 for the C++23 container range constructors.
        if ! command -v g++-15 >/dev/null; then
            sudo add-apt-repository --yes --no-update ppa:ubuntu-toolchain-r/test
            packages+=(g++-15)
        fi
        # Keep the preset's g++ name while selecting the CI toolchain.
        ln -s /usr/bin/g++-15 "$tools_dir/bin/g++"
        ;;
    linux-clang-*)
        # The runner provides Clang and LLD; add the matching LLVM libraries and archive tools.
        packages+=(libc++-21-dev libunwind-21-dev libclang-rt-21-dev llvm-21)
        printf '%s\n' /usr/lib/llvm-21/bin >> "$GITHUB_PATH"
        ;;
esac

sudo apt-get update
sudo apt-get install --no-install-recommends -y "${packages[@]}"

# Ubuntu 24.04 and 26.04 package procps 4.0.4, without --env for scoped fixture cleanup.
# Build only pgrep/pkill and keep them local to this CI job.
archive="$tools_dir/procps-ng-4.0.6.tar.xz"
curl --fail --location --retry 3 \
    https://sourceforge.net/projects/procps-ng/files/Production/procps-ng-4.0.6.tar.xz/download \
    --output "$archive"
printf '%s  %s\n' \
    67bea6fbc3a42a535a0230c9e891e5ddfb4d9d39422d46565a2990d1ace15216 \
    "$archive" | sha256sum --check
tar -xf "$archive" -C "$tools_dir"
pushd "$tools_dir/procps-ng-4.0.6"
./configure --disable-shared --enable-static --without-ncurses --disable-nls --disable-numa
make -j2 src/pgrep src/pkill
install -m 755 src/pgrep src/pkill "$tools_dir/bin/"
popd

printf '%s\n' "$tools_dir/bin" >> "$GITHUB_PATH"
