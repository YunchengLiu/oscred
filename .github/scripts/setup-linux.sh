#!/usr/bin/env bash
set -euo pipefail

tools_dir=$(realpath -m "${1:?Pass the preset tools directory}")
mkdir -p "$tools_dir/bin"

# GCC 15 provides the C++23 container range constructors used by the library.
sudo add-apt-repository --yes --no-update ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install --no-install-recommends -y \
    build-essential g++-15 pkg-config libsecret-1-dev libglib2.0-dev \
    dbus-daemon dbus-bin gnome-keyring systemd jq

# Keep the preset's g++ name while selecting the CI toolchain.
ln -s /usr/bin/g++-15 "$tools_dir/bin/g++"

# Ubuntu 24.04's procps lacks --env, which scopes fixture cleanup to owned
# processes. Build only pgrep/pkill and keep them local to this CI job.
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
