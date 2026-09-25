#include "error.h"
#include <format>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <gio/gio.h>
#include <libsecret/secret.h>
#include <glib.h>

namespace osvault::detail {
    namespace {
        class error_category final : public std::error_category {
            GQuark domain_;

        public:
            explicit error_category(GQuark const domain) noexcept :
                domain_(domain) {}

            [[nodiscard]] char const* name() const noexcept override {
                return g_quark_to_string(domain_);
            }

            [[nodiscard]] std::string message(int const code) const override {
                return std::format("{} error {}", name(), code);
            }
        };
    } // namespace

    std::error_code native_error(GError const& error) {
        if (error.domain == G_IO_ERROR) {
            switch (error.code) {
                case G_IO_ERROR_FAILED: return std::make_error_code(std::errc::io_error);
                case G_IO_ERROR_NOT_FOUND: return std::make_error_code(std::errc::no_such_file_or_directory);
                case G_IO_ERROR_NO_SPACE: return std::make_error_code(std::errc::no_space_on_device);
                case G_IO_ERROR_PERMISSION_DENIED: return std::make_error_code(std::errc::permission_denied);
                case G_IO_ERROR_NOT_SUPPORTED: return std::make_error_code(std::errc::not_supported);
                case G_IO_ERROR_CANCELLED: return std::make_error_code(std::errc::operation_canceled);
                case G_IO_ERROR_TIMED_OUT: return std::make_error_code(std::errc::timed_out);
                case G_IO_ERROR_MESSAGE_TOO_LARGE: return std::make_error_code(std::errc::message_size);
                default: break;
            }
        } else if (error.domain == G_DBUS_ERROR) {
            switch (error.code) {
                case G_DBUS_ERROR_FAILED:
                case G_DBUS_ERROR_IO_ERROR: return std::make_error_code(std::errc::io_error);
                case G_DBUS_ERROR_NO_MEMORY: return std::make_error_code(std::errc::not_enough_memory);
                case G_DBUS_ERROR_NOT_SUPPORTED: return std::make_error_code(std::errc::not_supported);
                case G_DBUS_ERROR_ACCESS_DENIED: return std::make_error_code(std::errc::permission_denied);
                case G_DBUS_ERROR_TIMEOUT:
                case G_DBUS_ERROR_TIMED_OUT: return std::make_error_code(std::errc::timed_out);
                default: break;
            }
        } else if (error.domain == SECRET_ERROR) {
            switch (error.code) {
                case SECRET_ERROR_PROTOCOL: return std::make_error_code(std::errc::protocol_error);
                case SECRET_ERROR_IS_LOCKED: return std::make_error_code(std::errc::permission_denied);
                case SECRET_ERROR_NO_SUCH_OBJECT: return std::make_error_code(std::errc::no_such_file_or_directory);
                default: break;
            }
        }
        // GError codes can be zero, but std::error_code reserves zero for success
        if (error.code == 0) {
            return std::make_error_code(std::errc::io_error);
        }
        // Keep one category per domain alive through static destruction
        static auto* const categories = std::make_unique<std::map<GQuark, error_category>>().release();
        auto const&        category   = categories->try_emplace(error.domain, error.domain).first->second;
        return {error.code, category};
    }
} // namespace osvault::detail
