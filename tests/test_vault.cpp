#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_predicate.hpp>
#include <osvault/osvault.h>

using std::string_literals::operator""s;
using std::string_view_literals::operator""sv;

namespace {
    [[nodiscard]] auto error_is(std::error_code const code) {
        return Catch::Matchers::Predicate<std::system_error>(
            [code](std::system_error const& error) noexcept { return error.code() == code; }, "the same operation error"
        );
    }
} // namespace

TEST_CASE("vault default name", "[vault]") {
    osvault::vault const storage;
    CHECK(storage.name() == osvault::vault::default_name);
}

TEST_CASE("vault custom name", "[vault]") {
    auto const           name = GENERATE("x"s, " Vault/密钥* "s);
    osvault::vault const storage{name};
    CHECK(storage.name() == name);
}

TEST_CASE("vault invalid names", "[vault]") {
    auto const name = GENERATE(""s, "\0a"s, "a\0b"s, "a\0"s);
    REQUIRE_THROWS_AS(osvault::vault{name}, std::invalid_argument);
}

TEST_CASE("vault invalid keys", "[vault]") {
    osvault::vault storage;
    auto const     key   = GENERATE(""sv, "\0x"sv, "x\0y"sv, "x\0"sv);
    auto const     error = std::make_error_code(std::errc::invalid_argument);
    CHECK(storage.try_read(key) == std::unexpected{error});
    CHECK(storage.try_write(key, {}) == error);
    CHECK(storage.try_erase(key) == std::unexpected{error});
    CHECK_THROWS_MATCHES(storage.read(key), std::system_error, error_is(error));
    CHECK_THROWS_MATCHES(storage.write(key, {}), std::system_error, error_is(error));
    CHECK_THROWS_MATCHES(storage.erase(key), std::system_error, error_is(error));
}

TEST_CASE("vault moved-from state", "[vault]") {
    osvault::vault source{"source"};
    auto const     value_limit = source.max_value_size();
    osvault::vault destination{std::move(source)};
    CHECK(destination.name() == "source");
    CHECK(source.name().empty());
    CHECK(source.max_key_size() == 0);
    CHECK(source.max_value_size() == value_limit);
    auto const error = std::make_error_code(std::errc::invalid_argument);
    CHECK(source.try_read("key") == std::unexpected{error});
    CHECK(source.try_write("key", {}) == error);
    CHECK(source.try_erase("key") == std::unexpected{error});
    CHECK(source.try_get_keys() == std::unexpected{error});
    CHECK(source.try_clear() == error);
    CHECK_THROWS_MATCHES(source.read("key"), std::system_error, error_is(error));
    CHECK_THROWS_MATCHES(source.write("key", {}), std::system_error, error_is(error));
    CHECK_THROWS_MATCHES(source.erase("key"), std::system_error, error_is(error));
    CHECK_THROWS_MATCHES(source.get_keys(), std::system_error, error_is(error));
    CHECK_THROWS_MATCHES(source.clear(), std::system_error, error_is(error));

    source = std::move(destination);
    CHECK(source.name() == "source");
    CHECK(source.max_key_size() > 0);
    CHECK(destination.name().empty());
    CHECK(destination.try_read("key") == std::unexpected{error});
}
