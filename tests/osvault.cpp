#include <stdexcept>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <osvault/osvault.h>

using std::string_literals::operator""s;

TEST_CASE("vault binds the default name", "[vault]") {
    osvault::vault const storage;
    CHECK(storage.name() == osvault::vault::default_name);
}

TEST_CASE("vault preserves the supplied name", "[vault]") {
    auto const           name = GENERATE("x"s, " Vault/密钥* "s);
    osvault::vault const storage{name};
    CHECK(storage.name() == name);
}

TEST_CASE("vault rejects invalid names", "[vault]") {
    auto const name = GENERATE(""s, "\0a"s, "a\0b"s, "a\0"s);
    REQUIRE_THROWS_AS(osvault::vault{name}, std::invalid_argument);
}
