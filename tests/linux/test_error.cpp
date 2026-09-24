#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <catch2/catch_test_macros.hpp>
#include <gio/gio.h>
#include <libsecret/secret.h>
#include <osvault/linux/error.h>
#include <glib.h>

namespace {
    // Construct before main so this check runs after function-local static destruction
    struct exit_error_check {
        std::error_code error;

        ~exit_error_check() {
            if (error &&
                (std::string_view{error.category().name()} != "g-dbus-error-quark" ||
                 !error.message().contains("g-dbus-error-quark"))) {
                std::fputs("Native error identity was lost during static destruction\n", stderr);
                std::_Exit(EXIT_FAILURE);
            }
        }
    };

    exit_error_check retained;
} // namespace

TEST_CASE("linux native error lifetime", "[linux]") {
    GError const native{.domain = G_DBUS_ERROR, .code = G_DBUS_ERROR_SERVICE_UNKNOWN, .message = nullptr};
    retained.error = osvault::linux_backend::native_error(native);
    REQUIRE(retained.error);
    CHECK(retained.error.value() == G_DBUS_ERROR_SERVICE_UNKNOWN);
}

TEST_CASE("linux standard operation errors", "[linux]") {
    struct mapping {
        GQuark    domain;
        int       code;
        std::errc expected;
    };
    std::array const cases{
        mapping{G_IO_ERROR,   G_IO_ERROR_FAILED,            std::errc::io_error                 },
        mapping{G_DBUS_ERROR, G_DBUS_ERROR_FAILED,          std::errc::io_error                 },
        mapping{SECRET_ERROR, SECRET_ERROR_PROTOCOL,        std::errc::protocol_error           },
        mapping{G_IO_ERROR,   G_IO_ERROR_PERMISSION_DENIED, std::errc::permission_denied        },
        mapping{G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,   std::errc::permission_denied        },
        mapping{SECRET_ERROR, SECRET_ERROR_IS_LOCKED,       std::errc::permission_denied        },
        mapping{G_IO_ERROR,   G_IO_ERROR_CANCELLED,         std::errc::operation_canceled       },
        mapping{G_IO_ERROR,   G_IO_ERROR_NO_SPACE,          std::errc::no_space_on_device       },
        mapping{G_IO_ERROR,   G_IO_ERROR_NOT_SUPPORTED,     std::errc::not_supported            },
        mapping{G_DBUS_ERROR, G_DBUS_ERROR_TIMED_OUT,       std::errc::timed_out                },
        mapping{G_IO_ERROR,   G_IO_ERROR_MESSAGE_TOO_LARGE, std::errc::message_size             },
        mapping{SECRET_ERROR, SECRET_ERROR_NO_SUCH_OBJECT,  std::errc::no_such_file_or_directory},
    };
    for (auto const& [domain, code, expected] : cases) {
        CAPTURE(g_quark_to_string(domain), code);
        GError const native{.domain = domain, .code = code, .message = nullptr};
        auto const   error = osvault::linux_backend::native_error(native);
        REQUIRE(error);
        CHECK(error == std::make_error_code(expected));
    }
}

TEST_CASE("linux unmapped native errors", "[linux]") {
    std::error_code error;
    {
        std::unique_ptr<GError, decltype(&g_error_free)> const native{
            g_error_new_literal(G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN, "Service unavailable"), g_error_free
        };
        error = osvault::linux_backend::native_error(*native);
    }
    REQUIRE(error);
    CHECK(error.value() == G_DBUS_ERROR_SERVICE_UNKNOWN);
    CHECK(std::string_view{error.category().name()} == "g-dbus-error-quark");
    CHECK(error.message().contains("g-dbus-error-quark"));

    GError const other{.domain = G_IO_ERROR, .code = G_IO_ERROR_DBUS_ERROR, .message = nullptr};
    auto const   distinct = osvault::linux_backend::native_error(other);
    CHECK(distinct.category() != error.category());

    GError const again{.domain = G_DBUS_ERROR, .code = G_DBUS_ERROR_SERVICE_UNKNOWN, .message = nullptr};
    CHECK(osvault::linux_backend::native_error(again) == error);
}

TEST_CASE("linux unknown domain zero is a failure", "[linux]") {
    GError const native{.domain = g_quark_from_static_string("osvault-test-error"), .code = 0, .message = nullptr};
    CHECK(osvault::linux_backend::native_error(native) == std::errc::io_error);
}
