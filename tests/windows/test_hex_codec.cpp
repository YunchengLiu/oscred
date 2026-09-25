#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <doctest/doctest.h>
#include <osvault/windows/hex_codec.h>

using std::string_view_literals::operator""sv;

TEST_CASE("hex encoding" * doctest::test_suite("hex")) {
    for (auto const& [input, expected] : {
             std::pair{""sv,               ""sv                },
             std::pair{"A"sv,              "41"sv              },
             std::pair{"a"sv,              "61"sv              },
             std::pair{"foobar"sv,         "666F6F626172"sv    },
             std::pair{"密钥/*"sv,         "E5AF86E992A52F2A"sv},
             std::pair{"\0\x7F\x80\xFF"sv, "007F80FF"sv        }
    }) {
        CAPTURE(input);
        CHECK(osvault::windows::encode_hex(input) == expected);
    }
}

TEST_CASE("hex byte encoding" * doctest::test_suite("hex")) {
    std::string input;
    std::string expected;
    // An independent oracle catches mistakes in the production formatter's width and signedness
    constexpr auto digits = "0123456789ABCDEF"sv;
    for (int value = 0; value < 256; ++value) {
        input.push_back(static_cast<char>(value));
        expected.push_back(digits[static_cast<std::size_t>(value) / 16]);
        expected.push_back(digits[static_cast<std::size_t>(value) % 16]);
    }
    CHECK(osvault::windows::encode_hex(input) == expected);
}

TEST_CASE("hex decoding" * doctest::test_suite("hex")) {
    for (auto const& [input, expected] : {
             std::pair{""sv,                 ""sv              },
             std::pair{"41"sv,               "A"sv             },
             std::pair{"61"sv,               "a"sv             },
             std::pair{"666f6F626172"sv,     "foobar"sv        },
             std::pair{"E5AF86E992A52F2A"sv, "密钥/*"sv        },
             std::pair{"007F80FF"sv,         "\0\x7F\x80\xFF"sv}
    }) {
        CAPTURE(input);
        auto const decoded = osvault::windows::decode_hex(input);
        REQUIRE(decoded);
        CHECK(*decoded == expected);
    }
}

TEST_CASE("hex byte decoding" * doctest::test_suite("hex")) {
    for (auto const uppercase : {false, true}) {
        CAPTURE(uppercase);
        std::string encoded;
        std::string expected;
        for (int value = 0; value < 256; ++value) {
            encoded += uppercase ? std::format("{:02X}", value) : std::format("{:02x}", value);
            expected.push_back(static_cast<char>(value));
        }
        auto const decoded = osvault::windows::decode_hex(encoded);
        REQUIRE(decoded);
        CHECK(*decoded == expected);
    }
}

TEST_CASE("hex invalid input" * doctest::test_suite("hex")) {
    for (auto const input :
         {"0"sv, "G0"sv, "0G"sv, "00GG"sv, "0x41"sv, "41 42 "sv, "0\0"sv, "+1"sv, "-1"sv, " 1"sv, "1 "sv,
          "\xFF"
          "0"sv}) {
        CAPTURE(input);
        CHECK_FALSE(osvault::windows::decode_hex(input));
    }
}
