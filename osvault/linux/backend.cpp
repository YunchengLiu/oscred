#include "osvault/backend.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <gio/gio.h>
#include <libsecret/secret.h>
#include <glib-object.h>
#include <glib.h>
#include <glibconfig.h>
#include "error.h"

namespace osvault::detail {
    namespace {
        template<typename T>
        using object_ptr = std::unique_ptr<T, decltype(&g_object_unref)>;

        using error_ptr = std::unique_ptr<GError, decltype(&g_error_free)>;
        using attrs_ptr = std::unique_ptr<GHashTable, decltype(&g_hash_table_unref)>;

        void free_items(GList* const items) noexcept {
            g_list_free_full(items, g_object_unref);
        }

        using items_ptr = std::unique_ptr<GList, decltype(&free_items)>;

        void dismiss(SecretPrompt* const prompt) noexcept {
            // Request dismissal without invoking the interactive Prompt method
            std::unique_ptr<GVariant, decltype(&g_variant_unref)> const response{
                g_dbus_proxy_call_sync(
                    G_DBUS_PROXY(prompt), "Dismiss", nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr
                ),
                g_variant_unref
            };
        }

        void service_class_init(void* const type, void* /*class_data*/) noexcept {
            // Synchronous libsecret operations also use the asynchronous prompt entry points internally
            auto* const service   = static_cast<SecretServiceClass*>(type);
            service->prompt_async = [](SecretService* const self, SecretPrompt* const prompt, GVariantType const*,
                                       GCancellable* const cancel, GAsyncReadyCallback const callback,
                                       gpointer const data) noexcept {
                dismiss(prompt);
                object_ptr<GTask> const task{g_task_new(self, cancel, callback, data), g_object_unref};
                g_task_return_error(
                    task.get(),
                    g_error_new_literal(
                        G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Secret Service interaction is disabled"
                    )
                );
            };
            service->prompt_finish = [](SecretService*, GAsyncResult* const result,
                                        GError** const error) noexcept -> GVariant* {
                return static_cast<GVariant*>(g_task_propagate_pointer(G_TASK(result), error));
            };
            service->prompt_sync = [](SecretService*, SecretPrompt* const prompt, GCancellable*, GVariantType const*,
                                      GError** const error) noexcept -> GVariant* {
                dismiss(prompt);
                g_set_error_literal(
                    error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Secret Service interaction is disabled"
                );
                return nullptr;
            };
        }

        // Collection and item proxies hold weak service references, so retain the service for the entire operation
        struct connection {
            object_ptr<SecretService>    service{nullptr, g_object_unref};
            object_ptr<SecretCollection> collection{nullptr, g_object_unref};
        };

        [[nodiscard]] std::expected<connection, std::error_code> connect() {
            // Only the prompt virtual functions differ; no additional instance or class storage is needed
            static auto const service_type = g_type_register_static_simple(
                SECRET_TYPE_SERVICE, "OsvaultService", sizeof(SecretServiceClass), service_class_init,
                sizeof(SecretService), nullptr, GTypeFlags{}
            );
            error_ptr  error{nullptr, g_error_free};
            connection result;
            result.service.reset(
                secret_service_open_sync(service_type, nullptr, SECRET_SERVICE_NONE, nullptr, std::out_ptr(error))
            );
            if (!result.service) {
                return std::unexpected{native_error(*error)};
            }
            result.collection.reset(secret_collection_for_alias_sync(
                result.service.get(), SECRET_COLLECTION_DEFAULT, SECRET_COLLECTION_NONE, nullptr, std::out_ptr(error)
            ));
            if (!result.collection) {
                return std::unexpected{
                    error ? native_error(*error) : std::make_error_code(std::errc::no_such_file_or_directory)
                };
            }
            // Locked collections may hide their items, so reject them before an empty search can imply success
            if (secret_collection_get_locked(result.collection.get()) != 0) {
                return std::unexpected{std::make_error_code(std::errc::permission_denied)};
            }
            return result;
        }

        constexpr auto schema = []() noexcept {
            SecretSchema result{};
            result.name          = "osvlt";
            result.flags         = SECRET_SCHEMA_NONE;
            result.attributes[0] = {.name = "group", .type = SECRET_SCHEMA_ATTRIBUTE_STRING};
            result.attributes[1] = {.name = "key", .type = SECRET_SCHEMA_ATTRIBUTE_STRING};
            return result;
        }();

        [[nodiscard]] attrs_ptr
        attributes(std::optional<std::string_view> const group, std::optional<std::string_view> const key) noexcept {
            attrs_ptr result{g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free), g_hash_table_unref};
            // Copy bounded views directly into GLib-owned, null-terminated attribute values
            if (group) {
                assert(!group->empty() && !group->contains('\0'));
                g_hash_table_insert(result.get(), g_strdup("group"), g_strndup(group->data(), group->size()));
            }
            if (key) {
                assert(!key->empty() && !key->contains('\0'));
                g_hash_table_insert(result.get(), g_strdup("key"), g_strndup(key->data(), key->size()));
            }
            return result;
        }

        [[nodiscard]] std::expected<items_ptr, std::error_code>
        search(SecretCollection* const collection, GHashTable* const attributes) {
            error_ptr error{nullptr, g_error_free};
            items_ptr result{
                secret_collection_search_sync(
                    collection, &schema, attributes, SECRET_SEARCH_ALL, nullptr, std::out_ptr(error)
                ),
                free_items
            };
            if (error) {
                return std::unexpected{native_error(*error)};
            }
            return result;
        }

        [[nodiscard]] std::string_view owned_key(GHashTable* const fields) noexcept {
            // Records created outside the library may omit a valid key
            // The returned view borrows the caller's attribute table; an empty view is not an owned key
            auto const* const key = static_cast<char const*>(g_hash_table_lookup(fields, "key"));
            return key != nullptr ? std::string_view{key} : std::string_view{};
        }

        [[nodiscard]] std::error_code remove(SecretItem* const item) {
            if (secret_item_get_locked(item) != 0) {
                return std::make_error_code(std::errc::permission_denied);
            }
            error_ptr error{nullptr, g_error_free};
            if (secret_item_delete_sync(item, nullptr, std::out_ptr(error)) == 0) {
                return native_error(*error);
            }
            return {};
        }
    } // namespace

    std::expected<std::vector<std::string>, std::error_code> try_enumerate() {
        auto const context = connect();
        if (!context) {
            return std::unexpected{context.error()};
        }
        auto const fields = attributes(std::nullopt, std::nullopt);
        auto const found  = search(context->collection.get(), fields.get());
        if (!found) {
            return std::unexpected{found.error()};
        }
        std::vector<std::string> names;
        for (auto const* entry = found->get(); entry != nullptr; entry = entry->next) {
            attrs_ptr const   fields{secret_item_get_attributes(SECRET_ITEM(entry->data)), g_hash_table_unref};
            auto const* const group = static_cast<char const*>(g_hash_table_lookup(fields.get(), "group"));
            if (group != nullptr && *group != '\0' && !owned_key(fields.get()).empty()) {
                names.emplace_back(group);
            }
        }
        return names;
    }

    std::size_t max_key_size([[maybe_unused]] std::string_view const group) noexcept {
        assert(!group.empty() && !group.contains('\0'));
        return std::numeric_limits<std::size_t>::max();
    }

    std::size_t max_value_size() noexcept {
        return std::numeric_limits<std::size_t>::max();
    }

    std::expected<std::vector<std::byte>, std::error_code>
    try_read(std::string_view const group, std::string_view const key) {
        auto const context = connect();
        if (!context) {
            return std::unexpected{context.error()};
        }
        auto const fields = attributes(group, key);
        auto const found  = search(context->collection.get(), fields.get());
        if (!found) {
            return std::unexpected{found.error()};
        }
        if (!*found) {
            return std::unexpected{std::make_error_code(std::errc::no_such_file_or_directory)};
        }
        auto* const item = SECRET_ITEM((*found)->data);
        if (secret_item_get_locked(item) != 0) {
            return std::unexpected{std::make_error_code(std::errc::permission_denied)};
        }
        error_ptr error{nullptr, g_error_free};
        if (secret_item_load_secret_sync(item, nullptr, std::out_ptr(error)) == 0) {
            return std::unexpected{native_error(*error)};
        }
        std::unique_ptr<SecretValue, decltype(&secret_value_unref)> const secret{
            secret_item_get_secret(item), secret_value_unref
        };
        gsize             length = 0;
        auto const* const data   = secret_value_get(secret.get(), &length);
        auto const        bytes  = std::as_bytes(std::span{data, length});
        return std::vector<std::byte>{std::from_range, bytes};
    }

    std::error_code
    try_write(std::string_view const group, std::string_view const key, std::span<std::byte const> const value) {
        // Negative lengths mean strlen to libsecret, so reject values that cannot be represented by gssize
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<gssize>::max())) {
            return std::make_error_code(std::errc::value_too_large);
        }
        auto const context = connect();
        if (!context) {
            return context.error();
        }
        auto const fields = attributes(group, key);
        // libsecret consumes the same byte representation through a char pointer
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto const* const data = reinterpret_cast<char const*>(value.data());
        std::unique_ptr<SecretValue, decltype(&secret_value_unref)> const secret{
            secret_value_new(data, static_cast<gssize>(value.size()), "application/octet-stream"), secret_value_unref
        };
        error_ptr                    error{nullptr, g_error_free};
        object_ptr<SecretItem> const item{
            secret_item_create_sync(
                context->collection.get(), &schema, fields.get(), "osvault", secret.get(), SECRET_ITEM_CREATE_REPLACE,
                nullptr, std::out_ptr(error)
            ),
            g_object_unref
        };
        return item ? std::error_code{} : native_error(*error);
    }

    std::expected<bool, std::error_code> try_erase(std::string_view const group, std::string_view const key) {
        auto const context = connect();
        if (!context) {
            return std::unexpected{context.error()};
        }
        auto const fields = attributes(group, key);
        auto const found  = search(context->collection.get(), fields.get());
        if (!found) {
            return std::unexpected{found.error()};
        }
        for (auto const* entry = found->get(); entry != nullptr; entry = entry->next) {
            if (auto const error = remove(SECRET_ITEM(entry->data))) {
                return std::unexpected{error};
            }
        }
        return static_cast<bool>(*found);
    }

    std::expected<std::vector<std::string>, std::error_code> try_get_keys(std::string_view const group) {
        auto const context = connect();
        if (!context) {
            return std::unexpected{context.error()};
        }
        auto const fields = attributes(group, std::nullopt);
        auto const found  = search(context->collection.get(), fields.get());
        if (!found) {
            return std::unexpected{found.error()};
        }
        std::vector<std::string> result;
        for (auto const* entry = found->get(); entry != nullptr; entry = entry->next) {
            attrs_ptr const fields{secret_item_get_attributes(SECRET_ITEM(entry->data)), g_hash_table_unref};
            if (auto const key = owned_key(fields.get()); !key.empty()) {
                result.emplace_back(key);
            }
        }
        std::ranges::sort(result);
        auto const duplicates = std::ranges::unique(result);
        result.erase(duplicates.begin(), duplicates.end());
        return result;
    }

    std::error_code try_clear(std::string_view const group) {
        auto const context = connect();
        if (!context) {
            return context.error();
        }
        auto const fields = attributes(group, std::nullopt);
        auto const found  = search(context->collection.get(), fields.get());
        if (!found) {
            return found.error();
        }
        for (auto const* entry = found->get(); entry != nullptr; entry = entry->next) {
            auto* const     item = SECRET_ITEM(entry->data);
            attrs_ptr const fields{secret_item_get_attributes(item), g_hash_table_unref};
            if (!owned_key(fields.get()).empty()) {
                if (auto const error = remove(item)) {
                    return error;
                }
            }
        }
        return {};
    }
} // namespace osvault::detail
