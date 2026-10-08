#define DOCTEST_CONFIG_IMPLEMENT

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
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
#include <gio/gio.h>
#include <libsecret/secret.h>
#include <osvault/osvault.h>
#include <glib-object.h>
#include <glib.h>
#include "../error.h"

using std::string_literals::operator""s;
using std::string_view_literals::operator""sv;

namespace {
    bool                       cleanup_failed = false;
    constexpr std::string_view fixture_group  = "osvault-tests/vault/";

    template<typename T>
    using object_ptr     = std::unique_ptr<T, decltype(&g_object_unref)>;
    using error_ptr      = std::unique_ptr<GError, decltype(&g_error_free)>;
    using attributes_ptr = std::unique_ptr<GHashTable, decltype(&g_hash_table_unref)>;

    [[nodiscard]] bool
    matches_session_bus(std::string_view const session, std::string_view const bus, std::string_view const kind = {}) {
        constexpr auto prefix = "session-"sv;
        if (!session.starts_with(prefix) || session.size() == prefix.size()) {
            return false;
        }
        // Bind the bus address to the CTest session directory and the selected failure scenario
        return bus ==
            std::format("unix:abstract=osvault-{}{}{}", session.substr(prefix.size()), kind.empty() ? "" : "-", kind);
    }

    void require_isolation(std::string_view const kind = {}) {
        auto const* const root   = std::getenv("OSVAULT_TEST_ROOT");
        auto const* const bus    = std::getenv("OSVAULT_TEST_BUS");
        auto const* const actual = std::getenv("DBUS_SESSION_BUS_ADDRESS");
        if (!root || !bus || !actual || std::string_view{bus} != actual ||
            !matches_session_bus(std::filesystem::path{root}.filename().string(), bus, kind) ||
            !std::filesystem::is_regular_file(std::filesystem::path{root} / "session.json")) {
            throw std::runtime_error{"Run native tests through CTest with the Linux session fixture"};
        }
    }

    [[nodiscard]] std::pair<object_ptr<SecretService>, object_ptr<SecretCollection>>
    collection(char const* const alias = SECRET_COLLECTION_DEFAULT) {
        require_isolation();
        error_ptr                 error{nullptr, g_error_free};
        object_ptr<SecretService> service{
            secret_service_open_sync(SECRET_TYPE_SERVICE, nullptr, SECRET_SERVICE_NONE, nullptr, std::out_ptr(error)),
            g_object_unref
        };
        if (!service) {
            throw std::runtime_error{error->message};
        }
        object_ptr<SecretCollection> result{
            secret_collection_for_alias_sync(
                service.get(), alias, SECRET_COLLECTION_NONE, nullptr, std::out_ptr(error)
            ),
            g_object_unref
        };
        if (!result || secret_collection_get_locked(result.get())) {
            throw std::runtime_error{"Missing or locked isolated collection"};
        }
        // Collection and item proxies borrow the service, including during independent native verification
        return {std::move(service), std::move(result)};
    }

    void free_items(GList* const items) noexcept {
        g_list_free_full(items, g_object_unref);
    }
    using items_ptr = std::unique_ptr<GList, decltype(&free_items)>;

    [[nodiscard]] items_ptr enumerate(SecretCollection* const store) {
        error_ptr error{nullptr, g_error_free};
        if (!secret_collection_load_items_sync(store, nullptr, std::out_ptr(error))) {
            throw std::runtime_error{error->message};
        }
        return {secret_collection_get_items(store), free_items};
    }

    [[nodiscard]] std::string_view field(GHashTable* const fields, char const* const name) noexcept {
        auto const* const value = static_cast<char const*>(g_hash_table_lookup(fields, name));
        return value ? std::string_view{value} : std::string_view{};
    }

    [[nodiscard]] bool in_scope(SecretItem* const item, std::string_view const prefix) noexcept {
        attributes_ptr const fields{secret_item_get_attributes(item), g_hash_table_unref};
        return field(fields.get(), "group").starts_with(prefix);
    }

    void check_empty(std::string_view const prefix, char const* const alias = SECRET_COLLECTION_DEFAULT) {
        auto const [service, store] = collection(alias);
        auto const entries          = enumerate(store.get());
        for (auto const* entry = entries.get(); entry; entry = entry->next) {
            if (in_scope(SECRET_ITEM(entry->data), prefix)) {
                throw std::runtime_error{"Native test scope is not empty"};
            }
        }
    }

    [[nodiscard]] bool remove_and_verify(std::string_view const prefix, char const* const alias) noexcept {
        try {
            auto const [service, store] = collection(alias);
            auto const entries          = enumerate(store.get());
            bool       success          = true;
            for (auto const* entry = entries.get(); entry; entry = entry->next) {
                auto* const item = SECRET_ITEM(entry->data);
                if (!in_scope(item, prefix)) {
                    continue;
                }
                // Delete directly so a cleanup request can never invoke an interactive Prompt
                error_ptr                                                   error{nullptr, g_error_free};
                std::unique_ptr<GVariant, decltype(&g_variant_unref)> const reply{
                    g_dbus_proxy_call_sync(
                        G_DBUS_PROXY(item), "Delete", nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
                        std::out_ptr(error)
                    ),
                    g_variant_unref
                };
                char const* prompt = nullptr;
                if (reply) {
                    g_variant_get(reply.get(), "(&o)", &prompt);
                }
                if (!reply || std::string_view{prompt} != "/") {
                    auto const* const path = g_dbus_proxy_get_object_path(G_DBUS_PROXY(item));
                    if (error) {
                        std::fprintf(
                            stderr, "Cannot delete %s (%s:%d)\n", path, g_quark_to_string(error->domain), error->code
                        );
                    } else {
                        std::fprintf(stderr, "Deleting %s requires interaction\n", path);
                    }
                    success = false;
                }
            }
            check_empty(prefix, alias);
            return success;
        } catch (std::exception const& error) {
            std::fprintf(stderr, "Native cleanup failed: %s\n", error.what());
            return false;
        }
    }

    class vault_fixture {
        char const* const alias_;

    public:
        explicit vault_fixture(char const* const alias = SECRET_COLLECTION_DEFAULT) :
            alias_{alias} {
            check_empty(fixture_group, alias_);
        }
        ~vault_fixture() noexcept {
            if (!remove_and_verify(fixture_group, alias_)) {
                cleanup_failed = true;
            }
        }
        vault_fixture(vault_fixture const&)            = delete;
        vault_fixture& operator=(vault_fixture const&) = delete;

        [[nodiscard]] std::string group(std::string_view const suffix = {}) const {
            return std::format("{}{}", fixture_group, suffix);
        }
    };

    void check_record(
        osvault::vault const& storage, std::string_view const key, std::span<std::byte const> const value,
        char const* const alias = SECRET_COLLECTION_DEFAULT
    ) {
        auto const [service, store] = collection(alias);
        auto const entries          = enumerate(store.get());
        unsigned   matches          = 0;
        for (auto const* entry = entries.get(); entry; entry = entry->next) {
            auto* const          item = SECRET_ITEM(entry->data);
            attributes_ptr const fields{secret_item_get_attributes(item), g_hash_table_unref};
            if (field(fields.get(), "xdg:schema") != "osvlt" || field(fields.get(), "group") != storage.name() ||
                field(fields.get(), "key") != key) {
                continue;
            }
            ++matches;
            error_ptr error{nullptr, g_error_free};
            REQUIRE(secret_item_load_secret_sync(item, nullptr, std::out_ptr(error)));
            std::unique_ptr<SecretValue, decltype(&secret_value_unref)> const secret{
                secret_item_get_secret(item), secret_value_unref
            };
            gsize             length = 0;
            auto const* const data   = secret_value_get(secret.get(), &length);
            CHECK(std::ranges::equal(std::as_bytes(std::span{data, length}), value));
        }
        CHECK(matches == 1);
    }

    void check_absent(osvault::vault const& storage, std::string_view const key) {
        auto const [service, store] = collection();
        auto const entries          = enumerate(store.get());
        for (auto const* entry = entries.get(); entry; entry = entry->next) {
            attributes_ptr const fields{secret_item_get_attributes(SECRET_ITEM(entry->data)), g_hash_table_unref};
            CHECK_FALSE(
                (field(fields.get(), "xdg:schema") == "osvlt" && field(fields.get(), "group") == storage.name() &&
                 field(fields.get(), "key") == key)
            );
        }
    }

    [[nodiscard]] int check_failure(std::string_view const kind) {
        if (kind != "unavailable" && kind != "missing" && kind != "locked") {
            throw std::invalid_argument{"Unknown native failure scenario"};
        }
        require_isolation(kind);
        osvault::vault storage{std::string{fixture_group}};
        auto const     result = storage.try_read("missing");
        if (result) {
            return 1;
        }
        auto const error = result.error();
        if ((kind == "locked" && error != std::errc::permission_denied) ||
            (kind == "missing" && error != std::errc::no_such_device) ||
            (kind == "unavailable" && std::string_view{error.category().name()} != "g-dbus-error-quark")) {
            return 1;
        }
        if (storage.try_write("missing", {}) != error || storage.try_erase("missing") != std::unexpected{error} ||
            storage.try_get_keys() != std::unexpected{error} || storage.try_clear() != error ||
            osvault::try_enumerate() != std::unexpected{error} || osvault::try_clear(storage.name()) != error) {
            return 1;
        }
#ifdef OSVAULT_STATIC
        // Direct registry inspection requires the test and backend to share the same linked GLib instance
        if (kind == "locked") {
            // Exercise the backend's registered prompt overrides with a real authorization request
            error_ptr                       native{nullptr, g_error_free};
            object_ptr<SecretService> const service{
                secret_service_open_sync(
                    g_type_from_name("OsvaultService"), nullptr, SECRET_SERVICE_NONE, nullptr, std::out_ptr(native)
                ),
                g_object_unref
            };
            if (!service) {
                return 1;
            }
            object_ptr<SecretCollection> const store{
                secret_collection_for_alias_sync(
                    service.get(), SECRET_COLLECTION_DEFAULT, SECRET_COLLECTION_NONE, nullptr, std::out_ptr(native)
                ),
                g_object_unref
            };
            if (!store) {
                return 1;
            }
            GList objects{store.get(), nullptr, nullptr};
            secret_service_unlock_sync(service.get(), &objects, nullptr, nullptr, std::out_ptr(native));
            if (!native || native->domain != G_IO_ERROR || native->code != G_IO_ERROR_PERMISSION_DENIED) {
                return 1;
            }
        }
#endif
        auto const same_error = [error](auto const operation) {
            try {
                operation();
            } catch (std::system_error const& exception) {
                return exception.code() == error;
            }
            return false;
        };
        return same_error([&] { (void)storage.read("missing"); }) &&
                same_error([&] { storage.write("missing", {}); }) &&
                same_error([&] { (void)storage.erase("missing"); }) && same_error([&] { (void)storage.get_keys(); }) &&
                same_error([&] { storage.clear(); }) && same_error([] { (void)osvault::enumerate(); }) &&
                same_error([&] { osvault::clear(storage.name()); })
            ? 0
            : 1;
    }
} // namespace

int main(int const argc, char** const argv) {
    try {
        constexpr auto persistence_group = "osvault-tests/persistence/"sv;
        if (argc == 2 && std::string_view{argv[1]} == "--write-test") {
            check_empty(persistence_group);
            osvault::vault{std::string{persistence_group}}.write("reopen", std::as_bytes(std::span{"persisted"sv}));
            return 0;
        }
        if (argc == 2 && std::string_view{argv[1]} == "--read-test") {
            require_isolation();
            auto const actual = osvault::vault{std::string{persistence_group}}.read("reopen");
            return std::ranges::equal(actual, std::as_bytes(std::span{"persisted"sv})) ? 0 : 1;
        }
        if (argc == 3 && std::string_view{argv[1]} == "--expect-error") {
            return check_failure(argv[2]);
        }
        auto const result = doctest::Context(argc, argv).run();
        return result != 0 ? result : (cleanup_failed ? 1 : 0);
    } catch (std::exception const& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

TEST_CASE("linux session bus identity" * doctest::test_suite("linux")) {
    constexpr auto session = "session-0123456789ab"sv;
    CHECK(matches_session_bus(session, "unix:abstract=osvault-0123456789ab"));
    CHECK_FALSE(matches_session_bus(session, "unix:abstract=osvault-fedcba987654"));
    CHECK_FALSE(matches_session_bus(session, "unix:path=/run/user/1000/bus"));
    CHECK_FALSE(matches_session_bus(session, "unix:abstract=osvault-0123456789ab-locked"));
    CHECK(matches_session_bus(session, "unix:abstract=osvault-0123456789ab-locked", "locked"));
    CHECK_FALSE(matches_session_bus(session, "unix:abstract=osvault-0123456789ab-missing", "locked"));
}

TEST_CASE("linux invalid failure scenario" * doctest::test_suite("linux")) {
    CHECK_THROWS_AS(check_failure("unknown"), std::invalid_argument);
    CHECK_THROWS_AS(check_failure(""), std::invalid_argument);
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

TEST_CASE_FIXTURE(vault_fixture, "vault collection isolation" * doctest::test_suite("native")) {
    // The private provider's session collection needs no provisioning or interactive unlock
    vault_fixture const neighbor{SECRET_COLLECTION_SESSION};
    auto const [service, store]             = collection();
    auto const [other_service, other_store] = collection(SECRET_COLLECTION_SESSION);
    REQUIRE(
        std::string_view{g_dbus_proxy_get_object_path(G_DBUS_PROXY(store.get()))} !=
        g_dbus_proxy_get_object_path(G_DBUS_PROXY(other_store.get()))
    );

    osvault::vault    storage{group()};
    std::vector const other_value{std::byte{0x22}, std::byte{0}, std::byte{0xFF}};
    for (auto const* const key : {"shared", "other-only"}) {
        attributes_ptr const fields{g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free), g_hash_table_unref};
        g_hash_table_insert(fields.get(), g_strdup("xdg:schema"), g_strdup("osvlt"));
        g_hash_table_insert(fields.get(), g_strdup("group"), g_strdup(storage.name().data()));
        g_hash_table_insert(fields.get(), g_strdup("key"), g_strdup(key));
        error_ptr                                                         error{nullptr, g_error_free};
        std::unique_ptr<SecretValue, decltype(&secret_value_unref)> const value{
            secret_value_new(
                reinterpret_cast<char const*>(other_value.data()), other_value.size(), "application/octet-stream"
            ),
            secret_value_unref
        };
        object_ptr<SecretItem> const item{
            secret_item_create_sync(
                other_store.get(), nullptr, fields.get(), "osvault test", value.get(), SECRET_ITEM_CREATE_NONE, nullptr,
                std::out_ptr(error)
            ),
            g_object_unref
        };
        REQUIRE(item);
    }
    auto const check_neighbor = [&] {
        for (auto const key : {"shared"sv, "other-only"sv}) {
            check_record(storage, key, other_value, SECRET_COLLECTION_SESSION);
        }
    };

    auto const missing = storage.try_read("other-only");
    REQUIRE_FALSE(missing);
    CHECK(missing.error() == std::make_error_code(std::errc::no_such_file_or_directory));
    CHECK(storage.get_keys().empty());
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), storage.name()));
    CHECK_FALSE(storage.erase("other-only"));
    storage.clear();
    osvault::clear(storage.name());
    check_neighbor();

    std::vector const value{std::byte{0x11}, std::byte{0}, std::byte{0x80}};
    storage.write("shared", value);
    CHECK(storage.read("shared") == value);
    CHECK(storage.get_keys() == std::vector{"shared"s});
    CHECK(std::ranges::contains(osvault::enumerate(), storage.name()));
    check_record(storage, "shared", value);
    check_neighbor();

    CHECK(storage.erase("shared"));
    check_absent(storage, "shared");
    check_neighbor();

    storage.write("shared", value);
    storage.clear();
    CHECK(storage.get_keys().empty());
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), storage.name()));
    check_absent(storage, "shared");
    check_neighbor();
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

TEST_CASE_FIXTURE(vault_fixture, "vault unknown limits" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    CHECK(storage.max_key_size() == std::numeric_limits<std::size_t>::max());
    CHECK(storage.max_value_size() == std::numeric_limits<std::size_t>::max());
    auto const key   = std::string(4096, 'k');
    auto const value = std::vector(8192, std::byte{0xA5});
    storage.write(key, value);
    CHECK(storage.read(key) == value);
    check_record(storage, key, value);
}

TEST_CASE_FIXTURE(vault_fixture, "vault record filtering" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    auto const [service, store] = collection();
    storage.write("valid", {});
    // Native injection also creates a duplicate logical key that enumeration must return only once
    for (auto const* const key : {static_cast<char const*>(nullptr), "", "foreign", "valid"}) {
        attributes_ptr const fields{g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free), g_hash_table_unref};
        g_hash_table_insert(fields.get(), g_strdup("group"), g_strdup(storage.name().data()));
        auto const foreign = key && std::string_view{key} == "foreign";
        g_hash_table_insert(fields.get(), g_strdup("xdg:schema"), g_strdup(foreign ? "foreign" : "osvlt"));
        if (key) {
            g_hash_table_insert(fields.get(), g_strdup("key"), g_strdup(key));
        }
        error_ptr                                                         error{nullptr, g_error_free};
        std::unique_ptr<SecretValue, decltype(&secret_value_unref)> const value{
            secret_value_new("", 0, "application/octet-stream"), secret_value_unref
        };
        object_ptr<SecretItem> const item{
            secret_item_create_sync(
                store.get(), nullptr, fields.get(), "osvault test", value.get(), SECRET_ITEM_CREATE_NONE, nullptr,
                std::out_ptr(error)
            ),
            g_object_unref
        };
        REQUIRE(item);
    }
    CHECK(storage.get_keys() == std::vector{"valid"s});
    CHECK(std::ranges::count(osvault::enumerate(), storage.name()) == 1);
    osvault::clear(storage.name());
    CHECK(storage.get_keys().empty());
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), storage.name()));
    check_absent(storage, "valid");
    auto const [verify_service, verify_store] = collection();
    auto const remaining                      = enumerate(verify_store.get());
    unsigned   count                          = 0;
    for (auto const* entry = remaining.get(); entry; entry = entry->next) {
        if (in_scope(SECRET_ITEM(entry->data), fixture_group)) {
            ++count;
        }
    }
    CHECK(count == 3);
}

TEST_CASE_FIXTURE(vault_fixture, "vault enumeration" * doctest::test_suite("native")) {
    osvault::vault const empty{group("empty")};
    auto                 expected = osvault::enumerate();
    CHECK_FALSE(std::ranges::contains(expected, empty.name()));
    for (auto const suffix : {"A"sv, "a"sv, "A/密钥*"sv}) {
        osvault::vault storage{group(suffix)};
        storage.write("first", {});
        storage.write("second", {});
        expected.emplace_back(storage.name());
    }
    CHECK(std::ranges::is_permutation(osvault::enumerate(), expected));
    auto const names = osvault::try_enumerate();
    REQUIRE(names);
    CHECK(std::ranges::is_permutation(*names, expected));

    osvault::vault storage{group("A")};
    REQUIRE(storage.erase("first"));
    CHECK(std::ranges::contains(osvault::enumerate(), storage.name()));
    REQUIRE(storage.erase("second"));
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), storage.name()));
}

TEST_CASE_FIXTURE(vault_fixture, "clear by name" * doctest::test_suite("native")) {
    osvault::vault storage{group()};
    osvault::vault neighbor{group("child")};
    auto const     input = group("suffix");
    auto const     name  = std::string_view{input}.substr(0, storage.name().size());
    storage.write("first", {});
    storage.write("second", {});
    neighbor.write("first", {});
    osvault::clear(name);
    CHECK(storage.get_keys().empty());
    check_absent(storage, "first");
    check_absent(storage, "second");
    CHECK_FALSE(std::ranges::contains(osvault::enumerate(), name));
    check_record(neighbor, "first", {});

    storage.write("first", {});
    REQUIRE_FALSE(osvault::try_clear(name));
    check_absent(storage, "first");
    CHECK_FALSE(osvault::try_clear(name));
    osvault::clear(name);
    check_record(neighbor, "first", {});
}

TEST_CASE_FIXTURE(vault_fixture, "vault name filtering" * doctest::test_suite("native")) {
    auto const before           = osvault::enumerate();
    auto const [service, store] = collection();
    // Missing and empty groups are confined to CTest's disposable collection
    for (auto const* const name : {static_cast<char const*>(nullptr), ""}) {
        attributes_ptr const fields{g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free), g_hash_table_unref};
        g_hash_table_insert(fields.get(), g_strdup("xdg:schema"), g_strdup("osvlt"));
        g_hash_table_insert(fields.get(), g_strdup("key"), g_strdup("valid"));
        if (name) {
            g_hash_table_insert(fields.get(), g_strdup("group"), g_strdup(name));
        }
        error_ptr                                                         error{nullptr, g_error_free};
        std::unique_ptr<SecretValue, decltype(&secret_value_unref)> const value{
            secret_value_new("", 0, "application/octet-stream"), secret_value_unref
        };
        object_ptr<SecretItem> const item{
            secret_item_create_sync(
                store.get(), nullptr, fields.get(), "osvault test", value.get(), SECRET_ITEM_CREATE_NONE, nullptr,
                std::out_ptr(error)
            ),
            g_object_unref
        };
        REQUIRE(item);
        CHECK(std::ranges::is_permutation(osvault::enumerate(), before));
        REQUIRE(secret_item_delete_sync(item.get(), nullptr, std::out_ptr(error)));
    }
}
