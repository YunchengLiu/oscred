# osvault

osvault is a lightweight C++23 library for native protected storage through Windows
Credential Manager, macOS Keychain, and Linux Secret Service. It provides a direct
binary key-value interface for small data.

- Read, replace, and erase binary values, including embedded zeros and empty data.
- Named vaults, persistent entries, key and vault enumeration, and per-vault cleanup.
- Use throwing operations or inspect failures with `std::error_code` and `std::expected`.
- Non-interactive access without authorization or unlock prompts.

## Usage

### Basic

```cpp
#include <cstddef>
#include <print>
#include <vector>
#include <osvault/osvault.h>

int main() {
    // Entries are grouped by this name and persist across runs
    osvault::vault storage{"osvault-example"};

    // Values preserve embedded zero bytes
    std::vector const value{std::byte{0x12}, std::byte{0}, std::byte{0xFF}};
    storage.write("token", value); // Creates or replaces this key

    // A missing key throws std::system_error
    auto const restored = storage.read("token");
    std::println("Read {} bytes", restored.size());

    bool const removed = storage.erase("token"); // False if already absent
    std::println("Removed: {}", removed);
}
```

### Error handling

`read` and `write` throw `std::system_error` on operation failure. Use their
`try_` counterparts to inspect the error instead. This snippet reuses `storage`
and `value` inside `main`:

```cpp
if (auto const error = storage.try_write("token", value)) {
    std::println("Write failed: {}", error.message());
    return 1;
}

if (auto const result = storage.try_read("token"); result) {
    std::println("Read {} bytes", result->size());
} else {
    std::println("Read failed: {}", result.error().message());
    return 1;
}
```

### Vault management

Enumerate names with stored entries, or clear a vault by name. An empty vault
does not appear in the results. Both functions also have `try_` counterparts.
Use `storage.get_keys()` to list keys within a vault.

```cpp
for (auto const& name : osvault::enumerate()) {
    std::println("Vault: {}", name);
}
osvault::clear("osvault-example"); // Removes all entries in this vault
```

### Concurrency

Operations are synchronous. Callers must serialize storage access within the
same user storage context, including across vault objects, threads, and processes.
On macOS, also coordinate direct Keychain calls that share its process-wide
interaction setting.

### Limits

- Names and keys are case-sensitive, nonempty UTF-8 byte strings without null bytes.
- Names and keys are metadata, not secrets; vault names do not provide application isolation.
- `max_key_size()` and `max_value_size()` report byte limits. `SIZE_MAX` means
  no known fixed limit; native storage may still reject large inputs.
- Operations that require user interaction report an error.

See the [public header](osvault/osvault.h) for the complete API contract.

## Build

Requires CMake 3.28+, Ninja, and a compiler and standard library with C++23 support.

### Platform setup

- **Windows:** install Visual Studio C++ build tools and the Windows SDK, then
  use an x64 Visual Studio developer environment. The clang-cl presets also need
  `clang-cl`, `llvm-rc`, and `llvm-mt` on `PATH`.
- **macOS:** use a recent Xcode or Command Line Tools installation with Apple
  Clang. At runtime, applications need access to an existing default file-based
  Keychain.
- **Linux:** install pkg-config, libsecret 0.18+, and GIO 2.48+ development files.
  On Debian/Ubuntu:

  ```sh
  sudo apt update
  sudo apt install pkg-config libsecret-1-dev libglib2.0-dev
  ```

  GCC presets need GCC 15 or newer as `g++` on `PATH`. LLVM presets use `clang++`
  with libc++/libc++abi, LLD, compiler-rt, and LLVM libunwind. Install matching
  LLVM components.

  At runtime, applications need a session D-Bus and an existing, unlocked default
  keyring from a Secret Service provider such as GNOME Keyring (`gnome-keyring`
  on Debian/Ubuntu).

### Configure and build

From the repository root, list the available [presets](CMakePresets.json), then
replace `<preset-name>` with your choice:

```sh
cmake --list-presets
cmake --preset "<preset-name>"
cmake --build --preset "<preset-name>"
```

Libraries are static by default; configure with `-DBUILD_SHARED_LIBS=ON` for a
shared build, or `-DOSVAULT_BUILD_TESTING=OFF` for a library-only build.

## Test

Standalone builds enable tests by default. CMake fetches doctest using Git.

- **Windows:** PowerShell (`pwsh` or `powershell`).
- **Linux:** GNOME Keyring, D-Bus tools, `busctl`, and `jq`. On Debian/Ubuntu:

  ```sh
  sudo apt install gnome-keyring dbus-daemon dbus-bin systemd jq
  ```

  Cleanup also requires `pgrep` and `pkill` with `--env` support. If the
  distribution's procps lacks this option, build a newer version and put those
  tools on `PATH`. See the procps build steps in the
  [CI setup script](.github/scripts/setup-linux.sh) for an example.

After building, use CTest to run tests and manage storage setup and cleanup:

```sh
ctest --preset "<preset-name>"
```

Linux and macOS tests use private storage; Windows tests use the current user's
Credential Manager. Run one CTest instance at a time; tests reserve the
`osvault-tests/` groups.

For interrupted runs, stop remaining test processes before using the cleanup
scripts for [Windows](tests/windows/cleanup.ps1), [Linux](tests/linux/cleanup.sh),
or [macOS](tests/macos/cleanup.sh).

## CMake integration

After building, install from the repository root:

```sh
cmake --install "build/<preset-name>/cache"
```

The installation goes into `build/<preset-name>/install/`. Pass its absolute path
as `-DCMAKE_PREFIX_PATH="<install-prefix>"` when configuring your application.
After defining the application's target (`my_app` here), link the installed package:

```cmake
find_package(osvault CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE osvault::osvault)
```

Use matching build configurations and C++ standard libraries for osvault and the
application; Linux LLVM presets use libc++.

For source integration, replace `find_package` with
`add_subdirectory(path/to/osvault EXCLUDE_FROM_ALL)` and link the same target.
