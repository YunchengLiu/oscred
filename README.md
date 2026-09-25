# osvault

osvault is a C++23 library for binary key-value access to platform-native protected
storage.

- Windows: Credential Manager
- Linux: Secret Service (libsecret)
- macOS: Keychain (planned)

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

    // Sample bytes; value can also be the output of an encryption library
    std::vector const value{std::byte{0x12}, std::byte{0}, std::byte{0xFF}};
    storage.write("token", value); // Creates or replaces this key

    // Read back the original bytes, including embedded zeros
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

### Concurrency

Operations are synchronous. Callers must serialize storage access within the
same user storage context, including across vault objects, threads, and processes.

### Limits

- Names and keys must be nonempty UTF-8 byte strings without null bytes.
- `max_key_size()` and `max_value_size()` report byte limits. `SIZE_MAX` means
  no known fixed limit; native storage may still reject large inputs.
- Operations never display authorization or unlock prompts.

## CMake integration

Link an installed package with CMake:

```cmake
find_package(osvault CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE osvault::osvault)
```

For source integration, replace `find_package` with
`add_subdirectory(path/to/osvault EXCLUDE_FROM_ALL)`.

## Build and test

Requires CMake 3.28+, Ninja, and a C++23 compiler and standard library.
Run native tests through one CTest instance at a time; they reserve the
`osvault-tests/` groups.

### Linux

Linux uses libsecret to access a Secret Service provider over the session D-Bus,
such as [GNOME Keyring](https://wiki.gnome.org/Projects/GnomeKeyring).
On Debian/Ubuntu, install the service and development packages:

```sh
sudo apt update
sudo apt install gnome-keyring dbus-user-session libsecret-1-dev libglib2.0-dev pkg-config
```

Applications need an existing, unlocked default keyring in their user session.

Native tests additionally need these tools:

```sh
sudo apt install dbus-bin systemd jq procps
```

Test cleanup requires `pgrep` and `pkill` with `--env` support.

Build and test with GCC:

```sh
cmake --preset linux-gcc-dbg
cmake --build --preset linux-gcc-dbg
ctest --preset linux-gcc-dbg
```

### Windows

Run in an x64 Visual Studio developer environment with clang-cl installed:

```text
cmake --preset win-clangcl-dbg
cmake --build --preset win-clangcl-dbg
ctest --preset win-clangcl-dbg
```

For MSVC, use `win-msvc-dbg` in these commands.
