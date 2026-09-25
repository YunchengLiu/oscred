#define DOCTEST_CONFIG_IMPLEMENT

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cwctype>
#include <exception>
#include <expected>
#include <format>
#include <memory>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include <osvault/osvault.h>
#include <osvault/windows/hex_codec.h>
#include <Windows.h>
#include <wincred.h>
#include "../error.h"

using std::string_literals::operator""s;
using std::string_view_literals::operator""sv;

namespace {
    bool                       cleanup_failed = false;
    constexpr std::string_view fixture_group  = "osvault-tests/vault/";

    struct entries {
        DWORD                                              count{};
        std::unique_ptr<PCREDENTIALW, decltype(&CredFree)> data{nullptr, &CredFree};
    };

    [[nodiscard]] std::wstring filter_for(std::string_view const group) {
        auto const text = std::format("osvlt/{}*", osvault::detail::encode_hex(group));
        return {text.begin(), text.end()};
    }

    [[nodiscard]] std::expected<entries, DWORD> enumerate(std::wstring const& filter) noexcept {
        entries result;
        if (!CredEnumerateW(filter.c_str(), 0, &result.count, std::out_ptr(result.data))) {
            auto const error = GetLastError();
            if (error != ERROR_NOT_FOUND) {
                return std::unexpected{error};
            }
            result.count = 0;
        }
        return result;
    }

    [[nodiscard]] bool remove_and_verify(std::wstring const& filter) noexcept {
        auto const found = enumerate(filter);
        if (!found) {
            std::fwprintf(stderr, L"Cannot enumerate test scope %ls (Win32 %lu)\n", filter.c_str(), found.error());
            return false;
        }
        bool success = true;
        for (auto const* entry : std::span{found->data.get(), found->count}) {
            if (entry->Type == CRED_TYPE_GENERIC && !CredDeleteW(entry->TargetName, CRED_TYPE_GENERIC, 0)) {
                auto const error = GetLastError();
                if (error != ERROR_NOT_FOUND) {
                    std::fwprintf(stderr, L"Cannot remove test credential %ls (Win32 %lu)\n", entry->TargetName, error);
                    success = false;
                }
            }
        }
        auto const remaining = enumerate(filter);
        if (!remaining) {
            std::fwprintf(stderr, L"Cannot verify test scope %ls (Win32 %lu)\n", filter.c_str(), remaining.error());
            return false;
        }
        for (auto const* entry : std::span{remaining->data.get(), remaining->count}) {
            if (entry->Type == CRED_TYPE_GENERIC) {
                std::fwprintf(stderr, L"Test credential remains: %ls\n", entry->TargetName);
                success = false;
            }
        }
        return success;
    }

    void check_empty(std::wstring const& filter) {
        auto const found = enumerate(filter);
        if (!found) {
            throw std::system_error{static_cast<int>(found.error()), std::system_category(), "Check test scope"};
        }
        if (std::ranges::any_of(std::span{found->data.get(), found->count}, [](auto const* entry) noexcept {
                return entry->Type == CRED_TYPE_GENERIC;
            })) {
            throw std::runtime_error{"Test group is not empty; run the cleanup script before retrying"};
        }
    }

    // Prepare the filter before writing so cleanup does not allocate it during unwinding
    class vault_fixture {
        std::wstring filter_;

    public:
        vault_fixture() :
            filter_(filter_for(fixture_group)) {
            check_empty(filter_);
        }

        ~vault_fixture() noexcept {
            if (!remove_and_verify(filter_)) {
                cleanup_failed = true;
            }
        }

        vault_fixture(vault_fixture const&)            = delete;
        vault_fixture& operator=(vault_fixture const&) = delete;

        [[nodiscard]] std::string group(std::string_view const suffix = {}) const {
            return std::format("{}{}", fixture_group, suffix);
        }
    };

    [[nodiscard]] std::wstring target_for(osvault::vault const& storage, std::string_view const key) {
        auto const text =
            std::format("osvlt/{}/{}", osvault::detail::encode_hex(storage.name()), osvault::detail::encode_hex(key));
        return {text.begin(), text.end()};
    }

    void
    check_record(osvault::vault const& storage, std::string_view const key, std::span<std::byte const> const value) {
        auto const                                        target = target_for(storage, key);
        std::unique_ptr<CREDENTIALW, decltype(&CredFree)> record{nullptr, &CredFree};
        REQUIRE(CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, std::out_ptr(record)) != 0);
        CHECK(record->Type == CRED_TYPE_GENERIC);
        CHECK(record->Persist == CRED_PERSIST_LOCAL_MACHINE);
        CHECK(std::wstring_view{record->TargetName} == target);
        CHECK(std::ranges::equal(std::as_bytes(std::span{record->CredentialBlob, record->CredentialBlobSize}), value));
    }

    void check_absent(osvault::vault const& storage, std::string_view const key) {
        auto const                                        target = target_for(storage, key);
        std::unique_ptr<CREDENTIALW, decltype(&CredFree)> record{nullptr, &CredFree};
        auto const found = CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, std::out_ptr(record));
        auto const error = GetLastError();
        CHECK(found == 0);
        if (found == 0) {
            CHECK(error == ERROR_NOT_FOUND);
        }
    }
} // namespace

int main(int argc, char* argv[]) {
    try {
        constexpr auto persistence_group = "osvault-tests/persistence/"sv;
        if (argc == 2 && std::string_view{argv[1]} == "--write-test") {
            check_empty(filter_for(persistence_group));
            osvault::vault storage{std::string{persistence_group}};
            storage.write("reopen", std::as_bytes(std::span{"persisted"sv}));
            return 0;
        }
        if (argc == 2 && std::string_view{argv[1]} == "--read-test") {
            auto const actual   = osvault::vault{std::string{persistence_group}}.read("reopen");
            auto const expected = std::as_bytes(std::span{"persisted"sv});
            return std::ranges::equal(actual, expected) ? 0 : 1;
        }
        auto const result = doctest::Context(argc, argv).run();
        // A nonthrowing fixture destructor cannot report a cleanup failure through test assertions
        return result != 0 ? result : (cleanup_failed ? 1 : 0);
    } catch (std::exception const& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

TEST_CASE_FIXTURE(vault_fixture, "vault binary round trip" * doctest::test_suite("native")) {
    osvault::vault    storage{group()};
    std::vector const value{std::byte{0}, std::byte{0x7F}, std::byte{0x80}, std::byte{0xFF}};
    storage.write("bytes", value);
    check_record(storage, "bytes", value);
    CHECK(storage.read("bytes") == value);
    CHECK(osvault::vault{group()}.try_read("bytes") == value);
}

TEST_CASE_FIXTURE(vault_fixture, "vault replacement" * doctest::test_suite("native")) {
    osvault::vault    storage{group()};
    std::vector const value{std::byte{0}, std::byte{0xFF}};
    storage.write("bytes", value);
    REQUIRE_FALSE(storage.try_write("bytes", std::span{value}.first(1)));
    CHECK(storage.read("bytes") == std::vector{std::byte{0}});
    check_record(storage, "bytes", std::span{value}.first(1));
}

TEST_CASE_FIXTURE(vault_fixture, "vault missing records" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    CHECK(storage.get_keys().empty());
    CHECK_FALSE(storage.try_clear());
    auto const missing = storage.try_read("empty");
    REQUIRE_FALSE(missing);
    CHECK(missing.error() == std::error_code{ERROR_NOT_FOUND, std::system_category()});
    expect_throw_with_code([&] { return storage.read("empty"); }, missing.error());
    CHECK_FALSE(storage.erase("empty"));
    CHECK(storage.try_erase("empty") == false);
}

TEST_CASE_FIXTURE(vault_fixture, "vault empty value" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    storage.write("empty", {});
    check_record(storage, "empty", {});
    CHECK(storage.read("empty").empty());
    CHECK(storage.erase("empty"));
    check_absent(storage, "empty");
    CHECK_FALSE(storage.erase("empty"));
    REQUIRE_FALSE(storage.try_write("empty", {}));
    CHECK(storage.try_erase("empty") == true);
    check_absent(storage, "empty");
    CHECK(storage.try_erase("empty") == false);
}

TEST_CASE_FIXTURE(vault_fixture, "vault key identity" * doctest::test_suite("native")) {
    auto expected = std::vector{"A"s, "a"s, "密钥/*"s, "A/"s, "A*"s};
    std::ranges::sort(expected);
    for (auto const suffix : {"A"sv, "a"sv, "A/密钥*"sv}) {
        osvault::vault storage{group(suffix)};
        for (auto const& key : expected) {
            auto const text = std::format("{}{}", suffix, key);
            auto const data = std::as_bytes(std::span{text});
            storage.write(key, data);
            check_record(storage, key, data);
        }
        auto keys = storage.get_keys();
        std::ranges::sort(keys);
        CHECK(keys == expected);
    }
    for (auto const suffix : {"A"sv, "a"sv, "A/密钥*"sv}) {
        osvault::vault const storage{group(suffix)};
        for (auto const& key : expected) {
            auto const text = std::format("{}{}", suffix, key);
            auto const data = std::as_bytes(std::span{text});
            CHECK(std::ranges::equal(storage.read(key), data));
        }
    }
}

TEST_CASE_FIXTURE(vault_fixture, "vault key limit" * doctest::test_suite("native")) {
    osvault::vault    storage{group()};
    auto const        key = std::string(storage.max_key_size(), 'x');
    std::vector const value{std::byte{0xA5}};
    storage.write(key, value);
    CHECK(storage.read(key) == value);
    check_record(storage, key, value);
    CHECK(storage.get_keys() == std::vector{key});

    auto const long_key   = key + 'x';
    auto const name_error = std::make_error_code(std::errc::filename_too_long);
    CHECK(storage.try_write(long_key, {}) == name_error);
    CHECK(storage.try_read(long_key) == std::unexpected{name_error});
    CHECK(storage.try_erase(long_key) == std::unexpected{name_error});
    expect_throw_with_code([&] { return storage.write(long_key, {}); }, name_error);
    expect_throw_with_code([&] { return storage.read(long_key); }, name_error);
    expect_throw_with_code([&] { return storage.erase(long_key); }, name_error);
    CHECK(storage.read(key) == value);
    check_record(storage, key, value);
}

TEST_CASE_FIXTURE(vault_fixture, "vault value limit" * doctest::test_suite("native")) {
    osvault::vault    storage{group()};
    constexpr auto    key = "value"sv;
    std::vector const value(storage.max_value_size(), std::byte{0xA5});
    storage.write(key, value);
    CHECK(storage.read(key) == value);
    check_record(storage, key, value);
    auto const large       = std::vector(storage.max_value_size() + 1, std::byte{});
    auto const value_error = std::make_error_code(std::errc::value_too_large);
    CHECK(storage.try_write(key, large) == value_error);
    expect_throw_with_code([&] { return storage.write(key, large); }, value_error);
    CHECK(storage.read(key) == value);
    check_record(storage, key, value);
}

TEST_CASE_FIXTURE(vault_fixture, "vault move persistence" * doctest::test_suite("native")) {
    {
        osvault::vault source{group("source")};
        osvault::vault destination{group("destination")};
        source.write("key", {});
        destination.write("key", {});
        osvault::vault moved{std::move(source)};
        destination = std::move(moved);
        CHECK(moved.name().empty());
        CHECK(destination.name() == group("source"));
        CHECK(destination.read("key").empty());
    }
    CHECK(osvault::vault{group("source")}.read("key").empty());
    CHECK(osvault::vault{group("destination")}.read("key").empty());
}

TEST_CASE_FIXTURE(vault_fixture, "vault clear isolation" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    osvault::vault neighbor{group("0")};
    storage.write("first", {});
    storage.write("second", {});
    neighbor.write("first", {});
    REQUIRE_FALSE(storage.try_clear());
    CHECK(storage.get_keys().empty());
    check_absent(storage, "first");
    check_absent(storage, "second");
    CHECK(neighbor.read("first").empty());
    check_record(neighbor, "first", {});
    storage.write("first", {});
    storage.clear();
    storage.clear();
    CHECK(storage.try_get_keys() == std::vector<std::string>{});
    check_absent(storage, "first");
    CHECK(neighbor.read("first").empty());
    check_record(neighbor, "first", {});
}

TEST_CASE_FIXTURE(vault_fixture, "vault record filtering" * doctest::test_suite("native")) {
    osvault::vault     storage{group()};
    auto const         text = std::format("osvlt/{}/", osvault::detail::encode_hex(storage.name()));
    std::wstring const prefix{text.begin(), text.end()};
    storage.write("valid", {});
    std::vector<std::wstring> foreign;
    for (auto const suffix : {L"", L"0", L"GG", L"00", L"410042", L"41/42", L"\u0141\u0131"}) {
        foreign.push_back(prefix + suffix);
    }
    auto const encoded   = osvault::detail::encode_hex("密钥");
    auto       lowercase = prefix + std::wstring{encoded.begin(), encoded.end()};
    std::ranges::transform(lowercase, lowercase.begin(), std::towlower);
    // Only this case needs native writes to create records the public API cannot produce
    auto injected = foreign;
    injected.push_back(lowercase);
    for (auto& target : injected) {
        CREDENTIALW credential{};
        credential.Type       = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.Persist    = CRED_PERSIST_LOCAL_MACHINE;
        REQUIRE(CredWriteW(&credential, 0));
    }
    CHECK(storage.read("密钥").empty());
    auto keys = storage.get_keys();
    std::ranges::sort(keys);
    CHECK(keys == std::vector{"valid"s, "密钥"s});

    REQUIRE_FALSE(storage.try_clear());
    CHECK(storage.get_keys().empty());
    for (auto const& untouched : foreign) {
        std::unique_ptr<CREDENTIALW, decltype(&CredFree)> record{nullptr, &CredFree};
        auto const found = CredReadW(untouched.c_str(), CRED_TYPE_GENERIC, 0, std::out_ptr(record));
        CHECK(found);
    }
    auto const missing = std::error_code{ERROR_NOT_FOUND, std::system_category()};
    CHECK(storage.try_read("valid") == std::unexpected{missing});
    CHECK(storage.try_read("密钥") == std::unexpected{missing});
    check_absent(storage, "valid");
    check_absent(storage, "密钥");
}

TEST_CASE_FIXTURE(vault_fixture, "vault group limit" * doctest::test_suite("native")) {
    osvault::vault const normal{group()};
    CHECK(normal.max_value_size() == CRED_MAX_CREDENTIAL_BLOB_SIZE);
    CHECK(7 + 2 * normal.name().size() + 2 * normal.max_key_size() == CRED_MAX_GENERIC_TARGET_NAME_LENGTH - 2);
    auto name = group();
    name.resize((CRED_MAX_GENERIC_TARGET_NAME_LENGTH - 1 - 7) / 2 - 1, 'g');
    osvault::vault storage{name};
    REQUIRE(storage.max_key_size() == 1);
    storage.write("x", {});
    check_record(storage, "x", {});
    CHECK(storage.read("x").empty());
    CHECK(storage.get_keys() == std::vector{"x"s});
    storage.clear();
    check_absent(storage, "x");
    osvault::vault oversized{name + 'g'};
    CHECK(oversized.max_key_size() == 0);
    auto const error = std::make_error_code(std::errc::filename_too_long);
    CHECK(oversized.try_write("x", {}) == error);
    CHECK(oversized.try_read("x") == std::unexpected{error});
    CHECK(oversized.try_erase("x") == std::unexpected{error});
    CHECK(oversized.try_get_keys() == std::unexpected{error});
    CHECK(oversized.try_clear() == error);
    expect_throw_with_code([&] { return oversized.get_keys(); }, error);
    expect_throw_with_code([&] { return oversized.clear(); }, error);
}

TEST_CASE("vault fixture unwinding" * doctest::test_suite("native")) {
    std::string name;
    auto const  interrupted = [&name] {
        vault_fixture const owned;
        name = owned.group();
        osvault::vault{name}.write("probe", {});
        throw std::logic_error{"Intentional cleanup probe"};
    };
    REQUIRE_THROWS_AS(interrupted(), std::logic_error);
    check_absent(osvault::vault{name}, "probe");
}
