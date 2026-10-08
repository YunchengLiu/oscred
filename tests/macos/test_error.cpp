#include <string_view>
#include <system_error>
#include <Security/SecBase.h>
#include <doctest/doctest.h>
#include "osvault/macos/error.h"

TEST_CASE("macos native error mapping" * doctest::test_suite("macos")) {
    CHECK_FALSE(osvault::detail::native_error(errSecSuccess));
    CHECK(osvault::detail::native_error(errSecItemNotFound) == std::errc::no_such_file_or_directory);
    CHECK(osvault::detail::native_error(errSecNoDefaultKeychain) == std::errc::no_such_device);
    CHECK(osvault::detail::native_error(errSecNoSuchKeychain) == std::errc::no_such_device);
    CHECK(osvault::detail::native_error(errSecAuthFailed) == std::errc::permission_denied);
    CHECK(osvault::detail::native_error(errSecInteractionNotAllowed) == std::errc::permission_denied);
    CHECK(osvault::detail::native_error(errSecInteractionRequired) == std::errc::permission_denied);
    CHECK(osvault::detail::native_error(errSecUserCanceled) == std::errc::operation_canceled);
    CHECK(osvault::detail::native_error(errSecIO) == std::errc::io_error);
    CHECK(osvault::detail::native_error(errSecAllocate) == std::errc::not_enough_memory);
    CHECK(osvault::detail::native_error(errSecParam) == std::errc::invalid_argument);
    CHECK(osvault::detail::native_error(errSecReadOnly) == std::errc::read_only_file_system);
}

TEST_CASE("macos unmapped error identity" * doctest::test_suite("macos")) {
    auto const error = osvault::detail::native_error(errSecDuplicateItem);
    CHECK(error.value() == errSecDuplicateItem);
    CHECK(std::string_view{error.category().name()} == "osvault.keychain");
    CHECK(error.category() == osvault::detail::native_error(errSecNotAvailable).category());
}
