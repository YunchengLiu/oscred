#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

// Private Keychain creation and deletion require the deprecated file-based APIs
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace {
    using keychain_ptr = std::unique_ptr<std::remove_pointer_t<SecKeychainRef>, decltype(&CFRelease)>;

    void require_success(OSStatus const status) {
        if (status != errSecSuccess) {
            throw std::runtime_error{std::format("Keychain operation failed: {}", status)};
        }
    }
} // namespace

int main(int const argc, char** const argv) {
    try {
        auto const* const environment = std::getenv("OSVAULT_TEST_ROOT");
        auto const* const home        = std::getenv("HOME");
        if (argc != 2 || !environment || !home) {
            throw std::runtime_error{"Use the isolated session fixture"};
        }
        std::filesystem::path const root{environment};
        if (!std::filesystem::is_regular_file(root / "session.txt") || std::filesystem::path{home} != root / "home") {
            throw std::runtime_error{"Invalid private session"};
        }
        require_success(SecKeychainSetUserInteractionAllowed(false));
        std::string_view const action{argv[1]};
        if (action != "setup" && action != "cleanup") {
            throw std::runtime_error{"Unknown action"};
        }
        std::array<keychain_ptr, 2> stores{
            keychain_ptr{nullptr, CFRelease},
            keychain_ptr{nullptr, CFRelease}
        };
        std::array const names{"primary.keychain", "neighbor.keychain"};
        bool             success = true;
        for (std::size_t index = 0; index < stores.size(); ++index) {
            auto const path = (root / names[index]).string();
            if (action == "setup") {
                require_success(
                    SecKeychainCreate(path.c_str(), 8, "test-key", false, nullptr, std::out_ptr(stores[index]))
                );
            } else {
                auto status = SecKeychainOpen(path.c_str(), std::out_ptr(stores[index]));
                if (status == errSecSuccess) {
                    status = SecKeychainDelete(stores[index].get());
                }
                if (status != errSecSuccess && status != errSecNoSuchKeychain) {
                    std::fprintf(stderr, "Cannot delete %s: %d\n", path.c_str(), static_cast<int>(status));
                    success = false;
                }
            }
        }
        if (action == "setup") {
            require_success(SecKeychainSetDefault(stores.front().get()));
            std::array<void const*, 2> refs{stores.front().get(), stores.back().get()};
            std::unique_ptr<std::remove_pointer_t<CFArrayRef>, decltype(&CFRelease)> const list{
                CFArrayCreate(nullptr, refs.data(), 2, &kCFTypeArrayCallBacks), CFRelease
            };
            if (!list) {
                throw std::bad_alloc{};
            }
            require_success(SecKeychainSetSearchList(list.get()));
        }
        return success ? 0 : 1;
    } catch (std::exception const& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
