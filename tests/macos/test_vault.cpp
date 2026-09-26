#define DOCTEST_CONFIG_IMPLEMENT

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <doctest/doctest.h>
#include <osvault/osvault.h>
#include "tests/error.h"

// Private Keychain inspection requires the deprecated file-based APIs
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

using std::string_literals::operator""s;
using std::string_view_literals::operator""sv;

namespace {
    bool                       cleanup_failed = false;
    constexpr std::string_view fixture_group  = "osvault-tests/vault/";

    using keychain_ptr = std::unique_ptr<std::remove_pointer_t<SecKeychainRef>, decltype(&CFRelease)>;
    using search_ptr   = std::unique_ptr<std::remove_pointer_t<SecKeychainSearchRef>, decltype(&CFRelease)>;
    using item_ptr     = std::unique_ptr<std::remove_pointer_t<SecKeychainItemRef>, decltype(&CFRelease)>;

    void require_success(OSStatus const status) {
        if (status != errSecSuccess) {
            throw std::runtime_error{std::format("Native Keychain failure: {}", status)};
        }
    }

    [[nodiscard]] keychain_ptr keychain(bool const neighbor = false) {
        auto const* const root = std::getenv("OSVAULT_TEST_ROOT");
        auto const* const home = std::getenv("HOME");
        if (!root || !home || std::filesystem::path{home} != std::filesystem::path{root} / "home" ||
            !std::filesystem::is_regular_file(std::filesystem::path{root} / "session.txt")) {
            throw std::runtime_error{"Run native tests through CTest with the macOS session fixture"};
        }
        keychain_ptr result{nullptr, CFRelease};
        require_success(SecKeychainCopyDefault(std::out_ptr(result)));
        std::array<char, 4096> path{};
        UInt32                 size = path.size();
        require_success(SecKeychainGetPath(result.get(), &size, path.data()));
        if (std::filesystem::path{path.data()} != std::filesystem::path{root} / "primary.keychain") {
            throw std::runtime_error{"Default Keychain is outside the private session"};
        }
        require_success(SecKeychainSetUserInteractionAllowed(false));
        if (neighbor) {
            auto const other = (std::filesystem::path{root} / "neighbor.keychain").string();
            require_success(SecKeychainOpen(other.c_str(), std::out_ptr(result)));
        }
        return result;
    }

    [[nodiscard]] std::vector<item_ptr> enumerate(SecKeychainRef const store) {
        search_ptr search{nullptr, CFRelease};
        require_success(
            SecKeychainSearchCreateFromAttributes(store, kSecGenericPasswordItemClass, nullptr, std::out_ptr(search))
        );
        std::vector<item_ptr> result;
        while (true) {
            item_ptr   item{nullptr, CFRelease};
            auto const status = SecKeychainSearchCopyNext(search.get(), std::out_ptr(item));
            if (status == errSecItemNotFound) {
                break;
            }
            require_success(status);
            result.push_back(std::move(item));
        }
        return result;
    }

    [[nodiscard]] std::string service(SecKeychainItemRef const item) {
        SecKeychainAttribute     attribute{kSecServiceItemAttr, 0, nullptr};
        SecKeychainAttributeList attributes{1, &attribute};
        require_success(SecKeychainItemCopyContent(item, nullptr, &attributes, nullptr, nullptr));
        auto const free_attributes = [](SecKeychainAttributeList* const value) noexcept {
            (void)SecKeychainItemFreeContent(value, nullptr);
        };
        std::unique_ptr<SecKeychainAttributeList, decltype(free_attributes)> const owner{&attributes, free_attributes};
        return {static_cast<char const*>(attribute.data), attribute.length};
    }

    [[nodiscard]] bool remove_and_verify() noexcept {
        try {
            bool success = true;
            // Preserve the cross-process persistence record between test cases
            for (bool const neighbor : {false, true}) {
                auto const store = keychain(neighbor);
                // A failed lock test must not prevent cleanup of the private keychain
                require_success(SecKeychainUnlock(store.get(), 8, "test-key", true));
                for (auto const& item : enumerate(store.get())) {
                    if (service(item.get()) != "osvlt/osvault-tests/persistence/") {
                        auto const status = SecKeychainItemDelete(item.get());
                        if (status != errSecSuccess) {
                            std::fprintf(stderr, "Native test cleanup failed: %d\n", static_cast<int>(status));
                            success = false;
                        }
                    }
                }
                for (auto const& item : enumerate(store.get())) {
                    if (service(item.get()) != "osvlt/osvault-tests/persistence/") {
                        success = false;
                    }
                }
            }
            return success;
        } catch (std::exception const& error) {
            std::fprintf(stderr, "Native test cleanup failed: %s\n", error.what());
            return false;
        }
    }

    class vault_fixture {
    public:
        vault_fixture() {
            for (bool const neighbor : {false, true}) {
                auto const store = keychain(neighbor);
                for (auto const& item : enumerate(store.get())) {
                    if (service(item.get()) != "osvlt/osvault-tests/persistence/") {
                        throw std::runtime_error{"Native test scope is not empty"};
                    }
                }
            }
        }
        ~vault_fixture() noexcept {
            if (!remove_and_verify()) {
                cleanup_failed = true;
            }
        }
        vault_fixture(vault_fixture const&)            = delete;
        vault_fixture& operator=(vault_fixture const&) = delete;

        [[nodiscard]] std::string group(std::string_view const suffix = {}) const {
            return std::format("{}{}", fixture_group, suffix);
        }
    };

    [[nodiscard]] std::vector<std::byte>
    native_read(std::string_view const group, std::string_view const key, bool const neighbor = false) {
        auto const store = keychain(neighbor);
        auto const name  = std::format("osvlt/{}", group);
        UInt32     length{};
        void*      data = nullptr;
        require_success(SecKeychainFindGenericPassword(
            store.get(), static_cast<UInt32>(name.size()), name.data(), static_cast<UInt32>(key.size()), key.data(),
            &length, &data, nullptr
        ));
        auto const free_data = [](void* const pointer) noexcept { (void)SecKeychainItemFreeContent(nullptr, pointer); };
        std::unique_ptr<void, decltype(free_data)> const owner{data, free_data};
        if (length == 0) {
            return {};
        }
        auto const bytes = std::as_bytes(std::span{static_cast<char const*>(data), length});
        return {std::from_range, bytes};
    }

    void
    check_record(osvault::vault const& storage, std::string_view const key, std::span<std::byte const> const value) {
        CHECK(std::ranges::equal(native_read(storage.name(), key), value));
    }

    void check_absent(osvault::vault const& storage, std::string_view const key) {
        auto const store = keychain();
        auto const name  = std::format("osvlt/{}", storage.name());
        CHECK(
            SecKeychainFindGenericPassword(
                store.get(), static_cast<UInt32>(name.size()), name.data(), static_cast<UInt32>(key.size()), key.data(),
                nullptr, nullptr, nullptr
            ) == errSecItemNotFound
        );
    }

    [[nodiscard]] int check_missing() {
        auto const* const root = std::getenv("OSVAULT_TEST_ROOT");
        auto const* const home = std::getenv("HOME");
        if (!root || !home || std::filesystem::path{home} != std::filesystem::path{root} / "missing-home" ||
            !std::filesystem::is_regular_file(std::filesystem::path{root} / "session.txt")) {
            throw std::runtime_error{"Missing private failure environment"};
        }
        require_success(SecKeychainSetUserInteractionAllowed(false));
        keychain_ptr store{nullptr, CFRelease};
        auto const   status = SecKeychainCopyDefault(std::out_ptr(store));
        if (status != errSecNoDefaultKeychain && status != errSecNoSuchKeychain) {
            throw std::runtime_error{"Failure scenario still has a default Keychain"};
        }
        osvault::vault storage{std::string{fixture_group}};
        auto const     missing = std::make_error_code(std::errc::no_such_file_or_directory);
        if (storage.try_read("key") != std::unexpected{missing} || storage.try_write("key", {}) != missing ||
            storage.try_erase("key") != std::unexpected{missing} ||
            storage.try_get_keys() != std::unexpected{missing} || storage.try_clear() != missing ||
            osvault::try_enumerate() != std::unexpected{missing} || osvault::try_clear(storage.name()) != missing) {
            return 1;
        }
        auto const same_error = [missing](auto const operation) {
            try {
                operation();
            } catch (std::system_error const& error) {
                return error.code() == missing;
            }
            return false;
        };
        return same_error([&] { (void)storage.read("key"); }) && same_error([&] { storage.write("key", {}); }) &&
                same_error([&] { (void)storage.erase("key"); }) && same_error([&] { (void)storage.get_keys(); }) &&
                same_error([&] { storage.clear(); }) && same_error([] { (void)osvault::enumerate(); }) &&
                same_error([&] { osvault::clear(storage.name()); })
            ? 0
            : 1;
    }

    void inject(std::string_view const service, std::string_view const key, bool const neighbor = false) {
        auto const store = keychain(neighbor);
        require_success(SecKeychainAddGenericPassword(
            store.get(), static_cast<UInt32>(service.size()), service.data(), static_cast<UInt32>(key.size()),
            key.data(), 0, "", nullptr
        ));
    }
} // namespace

int main(int const argc, char** const argv) {
    try {
        constexpr auto persistence_group = "osvault-tests/persistence/"sv;
        if (argc == 2 && std::string_view{argv[1]} == "--write-test") {
            auto const store = keychain();
            if (!enumerate(store.get()).empty()) {
                throw std::runtime_error{"Native test scope is not empty"};
            }
            osvault::vault{std::string{persistence_group}}.write("reopen", std::as_bytes(std::span{"persisted"sv}));
            return 0;
        }
        if (argc == 2 && std::string_view{argv[1]} == "--read-test") {
            (void)keychain();
            auto const actual = osvault::vault{std::string{persistence_group}}.read("reopen");
            return std::ranges::equal(actual, std::as_bytes(std::span{"persisted"sv})) ? 0 : 1;
        }
        if (argc == 3 && std::string_view{argv[1]} == "--failure-child") {
            (void)keychain();
            osvault::vault{std::string{fixture_group}}.write(argv[2], {});
            if (std::string_view{argv[2]} == "crash") {
                std::raise(SIGKILL);
            } else if (std::string_view{argv[2]} == "hold") {
                std::this_thread::sleep_for(std::chrono::seconds{30});
            }
            return 23;
        }
        if (argc == 3 && std::string_view{argv[1]} == "--verify-failure") {
            (void)native_read(fixture_group, argv[2]);
            return remove_and_verify() ? 0 : 1;
        }
        if (argc == 2 && std::string_view{argv[1]} == "--expect-missing") {
            return check_missing();
        }
        auto const result = doctest::Context(argc, argv).run();
        return result != 0 ? result : (cleanup_failed ? 1 : 0);
    } catch (std::exception const& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

TEST_CASE_FIXTURE(vault_fixture, "vault empty replacement" * doctest::test_suite("native")) {
    osvault::vault    storage{group()};
    std::vector const value{std::byte{0}, std::byte{0xFF}};
    storage.write("key", value);
    storage.write("key", {});
    CHECK(storage.read("key").empty());
    check_record(storage, "key", {});
    storage.write("key", value);
    CHECK(storage.read("key") == value);
    check_record(storage, "key", value);
}

TEST_CASE_FIXTURE(vault_fixture, "vault name lifecycle" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    osvault::vault neighbor{group("0")};
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), group()));
    storage.write("first", {});
    storage.write("second", {});
    neighbor.write("first", {});
    auto const names = osvault::try_enumerate();
    REQUIRE(names);
    CHECK(std::ranges::count(*names, group()) == 1);
    CHECK(std::ranges::count(*names, group("0")) == 1);
    auto const bounded = group("suffix");
    CHECK_FALSE(osvault::try_clear(std::string_view{bounded}.substr(0, group().size())));
    check_absent(storage, "first");
    check_absent(storage, "second");
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), group()));
    check_record(neighbor, "first", {});
    osvault::clear(neighbor.name());
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), neighbor.name()));
    CHECK_FALSE(osvault::try_clear(storage.name()));
}


TEST_CASE_FIXTURE(vault_fixture, "vault keychain isolation" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    inject(std::format("osvlt/{}", storage.name()), "neighbor", true);
    CHECK(storage.get_keys().empty());
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), group()));
    CHECK_FALSE(storage.try_read("neighbor"));
    CHECK_FALSE(storage.erase("neighbor"));
    storage.write("neighbor", std::as_bytes(std::span{"value"sv}));
    storage.clear();
    check_absent(storage, "neighbor");
    CHECK(native_read(storage.name(), "neighbor", true).empty());
}

TEST_CASE_FIXTURE(vault_fixture, "vault malformed records" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    auto const     name    = std::format("osvlt/{}", storage.name());
    auto const     foreign = std::format("other/{}", storage.name());
    inject(name, "");
    inject(name, "bad\0key"sv);
    inject(foreign, "foreign");
    inject("osvlt/", "empty-group");
    auto const store = keychain();
    CHECK(storage.get_keys().empty());
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), group()));
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), ""));
    storage.write("valid", {});
    storage.clear();
    check_absent(storage, "valid");
    CHECK(native_read(storage.name(), "").empty());
    CHECK(native_read(storage.name(), "bad\0key"sv).empty());
    auto const records = enumerate(store.get());
    CHECK(std::ranges::count(records, foreign, [](auto const& item) { return service(item.get()); }) == 1);
    CHECK(std::ranges::count(records, "osvlt/", [](auto const& item) { return service(item.get()); }) == 1);
}

TEST_CASE_FIXTURE(vault_fixture, "vault locked keychain" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    storage.write("key", std::as_bytes(std::span{"secret"sv}));
    auto const store = keychain();
    require_success(SecKeychainLock(store.get()));
    auto const denied = std::make_error_code(std::errc::permission_denied);
    CHECK(storage.try_read("key") == std::unexpected{denied});
    CHECK(storage.try_write("key", {}) == denied);
    CHECK(storage.try_write("new", {}) == denied);
    expect_throw_with_code([&] { return storage.read("key"); }, denied);
    expect_throw_with_code([&] { storage.write("key", {}); }, denied);
    // File-based Keychain permits metadata access and deletion while locked
    CHECK(storage.get_keys() == std::vector{"key"s});
    CHECK(std::ranges::contains(osvault::enumerate(), group()));
    CHECK_FALSE(storage.try_clear());
    CHECK(storage.get_keys().empty());
    require_success(SecKeychainUnlock(store.get(), 8, "test-key", true));
}

TEST_CASE_FIXTURE(vault_fixture, "vault interaction policy restored" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    for (Boolean const allowed : {false, true}) {
        require_success(SecKeychainSetUserInteractionAllowed(allowed));
        CHECK_FALSE(storage.try_read("missing"));
        Boolean actual{};
        require_success(SecKeychainGetUserInteractionAllowed(&actual));
        CHECK(actual == allowed);
        CHECK_FALSE(storage.try_write("key", {}));
        require_success(SecKeychainGetUserInteractionAllowed(&actual));
        CHECK(actual == allowed);
    }
    require_success(SecKeychainSetUserInteractionAllowed(false));
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

TEST_CASE_FIXTURE(vault_fixture, "vault bounded key views" * doctest::test_suite("native")) {
    osvault::vault   storage{group()};
    std::array const input{'k', 'e', 'y', 'x'};
    auto const       key = std::string_view{input.data(), 3};
    storage.write(key, {});
    CHECK(storage.get_keys() == std::vector{"key"s});
    CHECK(storage.read(key).empty());
    check_record(storage, "key", {});
    CHECK(storage.erase(key));
    check_absent(storage, "key");
}

TEST_CASE_FIXTURE(vault_fixture, "vault missing records" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    CHECK(storage.get_keys().empty());
    CHECK_FALSE(storage.try_clear());
    auto const missing = storage.try_read("empty");
    REQUIRE_FALSE(missing);
    CHECK(missing.error() == std::make_error_code(std::errc::no_such_file_or_directory));
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
    auto expected = std::vector{"A"s, "a"s, "密钥/*"s, "é"s, "e\u0301"s, "A/"s, "A*"s};
    std::ranges::sort(expected);
    for (auto const suffix : {"A"sv, "a"sv, "A/密钥*"sv, "é"sv, "e\u0301"sv}) {
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
    for (auto const suffix : {"A"sv, "a"sv, "A/密钥*"sv, "é"sv, "e\u0301"sv}) {
        osvault::vault storage{group(suffix)};
        for (auto const& key : expected) {
            auto const text = std::format("{}{}", suffix, key);
            auto const data = std::as_bytes(std::span{text});
            CHECK(std::ranges::equal(storage.read(key), data));
        }
        storage.clear();
        CHECK(storage.get_keys().empty());
        for (auto const& key : expected) {
            check_absent(storage, key);
        }
    }
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
