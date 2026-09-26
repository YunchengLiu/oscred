#include "error.h"
#include <format>
#include <memory>
#include <string>
#include <system_error>
#include <Security/SecBase.h>
#include <MacTypes.h>

namespace osvault::detail {
    namespace {
        class error_category final : public std::error_category {
        public:
            [[nodiscard]] char const* name() const noexcept override {
                return "osvault.keychain";
            }

            [[nodiscard]] std::string message(int const code) const override {
                return std::format("Keychain error {}", code);
            }
        };
    } // namespace

    std::error_code native_error(OSStatus const status) {
        switch (status) {
            case errSecSuccess: return {};
            case errSecParam: return std::make_error_code(std::errc::invalid_argument);
            case errSecAllocate: return std::make_error_code(std::errc::not_enough_memory);
            case errSecIO: return std::make_error_code(std::errc::io_error);
            case errSecUserCanceled: return std::make_error_code(std::errc::operation_canceled);
            case errSecReadOnly: return std::make_error_code(std::errc::read_only_file_system);
            case errSecAuthFailed:
            case errSecInteractionNotAllowed:
            case errSecInteractionRequired: return std::make_error_code(std::errc::permission_denied);
            case errSecItemNotFound:
            case errSecNoSuchKeychain:
            case errSecNoDefaultKeychain: return std::make_error_code(std::errc::no_such_file_or_directory);
            default: break;
        }
        // Error codes can outlive other static objects
        static auto* const category = std::make_unique<error_category>().release();
        return {static_cast<int>(status), *category};
    }
} // namespace osvault::detail
