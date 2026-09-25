#include <expected>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <doctest/doctest.h>
#include <osvault/osvault.h>
#include "error.h"

using std::string_literals::operator""s;
using std::string_view_literals::operator""sv;

TEST_CASE("vault default name" * doctest::test_suite("vault")) {
    osvault::vault const storage;
    CHECK(storage.name() == osvault::vault::default_name);
}

TEST_CASE("vault custom name" * doctest::test_suite("vault")) {
    for (auto const& name : {"x"s, " Vault/密钥* "s}) {
        CAPTURE(name);
        osvault::vault const storage{name};
        CHECK(storage.name() == name);
    }
}

TEST_CASE("vault invalid names" * doctest::test_suite("vault")) {
    for (auto const& name : {""s, "\0a"s, "a\0b"s, "a\0"s}) {
        CAPTURE(name);
        CHECK_THROWS_AS(osvault::vault{name}, std::invalid_argument);
    }
}

TEST_CASE("clear invalid names" * doctest::test_suite("vault")) {
    auto const error = std::make_error_code(std::errc::invalid_argument);
    for (auto const name : {""sv, "\0x"sv, "x\0y"sv, "x\0"sv}) {
        CAPTURE(name);
        CHECK(osvault::try_clear(name) == error);
        expect_throw_with_code([&] { osvault::clear(name); }, error);
    }
}

TEST_CASE("vault invalid keys" * doctest::test_suite("vault")) {
    osvault::vault storage;
    auto const     error = std::make_error_code(std::errc::invalid_argument);
    for (auto const key : {""sv, "\0x"sv, "x\0y"sv, "x\0"sv}) {
        CAPTURE(key);
        CHECK(storage.try_read(key) == std::unexpected{error});
        CHECK(storage.try_write(key, {}) == error);
        CHECK(storage.try_erase(key) == std::unexpected{error});
        expect_throw_with_code([&] { return storage.read(key); }, error);
        expect_throw_with_code([&] { return storage.write(key, {}); }, error);
        expect_throw_with_code([&] { return storage.erase(key); }, error);
    }
}

TEST_CASE("vault moved-from state" * doctest::test_suite("vault")) {
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
    expect_throw_with_code([&] { return source.read("key"); }, error);
    expect_throw_with_code([&] { return source.write("key", {}); }, error);
    expect_throw_with_code([&] { return source.erase("key"); }, error);
    expect_throw_with_code([&] { return source.get_keys(); }, error);
    expect_throw_with_code([&] { return source.clear(); }, error);

    source = std::move(destination);
    CHECK(source.name() == "source");
    CHECK(source.max_key_size() > 0);
    CHECK(destination.name().empty());
    CHECK(destination.try_read("key") == std::unexpected{error});
}
