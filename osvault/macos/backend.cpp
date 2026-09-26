#include "osvault/backend.h"
#include <cassert>
#include <cstddef>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFString.h>
#include <Security/SecBase.h>
#include <Security/SecItem.h>
#include <Security/SecKeychain.h>
#include <MacTypes.h>
#include "error.h"

namespace osvault::detail {
    namespace {
        constexpr std::string_view marker = "osvlt/";
        // File-based SecItem bridges attribute and data lengths through UInt32
        constexpr std::size_t field_bytes = std::numeric_limits<UInt32>::max();

        using keychain_ptr = std::unique_ptr<std::remove_pointer_t<SecKeychainRef>, decltype(&CFRelease)>;
        using attrs_ptr    = std::unique_ptr<std::remove_pointer_t<CFMutableDictionaryRef>, decltype(&CFRelease)>;
        using result_ptr   = std::unique_ptr<void const, decltype(&CFRelease)>;

        [[nodiscard]] attrs_ptr attributes() {
            attrs_ptr result{
                CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks),
                CFRelease
            };
            if (!result) {
                throw std::bad_alloc{};
            }
            return result;
        }

        [[nodiscard]] std::error_code
        set_text(CFMutableDictionaryRef const fields, CFStringRef const key, std::string_view const text) {
            std::unique_ptr<std::remove_pointer_t<CFStringRef>, decltype(&CFRelease)> const value{
                CFStringCreateWithBytes(
                    nullptr,
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                    reinterpret_cast<UInt8 const*>(text.data()), static_cast<CFIndex>(text.size()),
                    kCFStringEncodingUTF8, 0
                ),
                CFRelease
            };
            if (!value) {
                return std::make_error_code(std::errc::invalid_argument);
            }
            CFDictionarySetValue(fields, key, value.get());
            return {};
        }

        // These management APIs are required for file-based Keychain hosts
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        template<typename Operation>
            requires std::is_nothrow_invocable_r_v<OSStatus, Operation&>
        [[nodiscard]] OSStatus noninteractive(Operation operation) noexcept {
            Boolean previous{};
            auto    status = SecKeychainGetUserInteractionAllowed(&previous);
            if (status != errSecSuccess) {
                return status;
            }
            status = SecKeychainSetUserInteractionAllowed(false);
            if (status != errSecSuccess) {
                return status;
            }
            auto const result = operation();
            status            = SecKeychainSetUserInteractionAllowed(previous);
            return status == errSecSuccess ? result : status;
        }

        struct query {
            keychain_ptr keychain{nullptr, CFRelease};
            attrs_ptr    fields{nullptr, CFRelease};
        };

        [[nodiscard]] std::expected<query, std::error_code>
        make_query(std::optional<std::string_view> const group, std::optional<std::string_view> const key) {
            assert(!group || (!group->empty() && !group->contains('\0')));
            assert(!key || (!key->empty() && !key->contains('\0')));
            if ((group && group->size() > field_bytes - marker.size()) || (key && key->size() > field_bytes)) {
                return std::unexpected{std::make_error_code(std::errc::filename_too_long)};
            }
            query result;
            result.fields = attributes();
            auto const status =
                noninteractive([&] noexcept { return SecKeychainCopyDefault(std::out_ptr(result.keychain)); });
            if (status != errSecSuccess) {
                return std::unexpected{native_error(status)};
            }
            void const* keychain = result.keychain.get();
            std::unique_ptr<std::remove_pointer_t<CFArrayRef>, decltype(&CFRelease)> const scope{
                CFArrayCreate(nullptr, &keychain, 1, &kCFTypeArrayCallBacks), CFRelease
            };
            if (!scope) {
                throw std::bad_alloc{};
            }
            CFDictionarySetValue(result.fields.get(), kSecClass, kSecClassGenericPassword);
            // Keep lookups in the default Keychain used for insertion
            CFDictionarySetValue(result.fields.get(), kSecMatchSearchList, scope.get());
            if (group) {
                auto const service = std::format("{}{}", marker, *group);
                if (auto const error = set_text(result.fields.get(), kSecAttrService, service)) {
                    return std::unexpected{error};
                }
            }
            if (key) {
                if (auto const error = set_text(result.fields.get(), kSecAttrAccount, *key)) {
                    return std::unexpected{error};
                }
            }
            return result;
        }
#pragma clang diagnostic pop

        [[nodiscard]] std::optional<std::string> text_field(CFDictionaryRef const fields, CFStringRef const key) {
            auto const* const value = CFDictionaryGetValue(fields, key);
            if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) {
                return std::nullopt;
            }
            auto const* const text  = static_cast<CFStringRef>(value);
            auto const        range = CFRangeMake(0, CFStringGetLength(text));
            CFIndex           length{};
            if (CFStringGetBytes(text, range, kCFStringEncodingUTF8, 0, 0, nullptr, 0, &length) != range.length) {
                return std::nullopt;
            }
            std::string result(static_cast<std::size_t>(length), '\0');
            CFStringGetBytes(
                text, range, kCFStringEncodingUTF8, 0, 0,
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<UInt8*>(result.data()), length, nullptr
            );
            if (result.empty() || result.contains('\0')) {
                return std::nullopt;
            }
            return result;
        }

        [[nodiscard]] std::expected<result_ptr, std::error_code> search(query const& query) {
            CFDictionarySetValue(query.fields.get(), kSecReturnAttributes, kCFBooleanTrue);
            CFDictionarySetValue(query.fields.get(), kSecReturnRef, kCFBooleanTrue);
            CFDictionarySetValue(query.fields.get(), kSecMatchLimit, kSecMatchLimitAll);
            result_ptr result{nullptr, CFRelease};
            auto const status =
                noninteractive([&] noexcept { return SecItemCopyMatching(query.fields.get(), std::out_ptr(result)); });
            if (status != errSecSuccess && status != errSecItemNotFound) {
                return std::unexpected{native_error(status)};
            }
            return result;
        }
    } // namespace

    std::expected<std::vector<std::string>, std::error_code> try_enumerate() {
        auto const query = make_query(std::nullopt, std::nullopt);
        if (!query) {
            return std::unexpected{query.error()};
        }
        auto const found = search(*query);
        if (!found) {
            return std::unexpected{found.error()};
        }
        std::vector<std::string> names;
        if (!*found) {
            return names;
        }
        auto const* const entries = static_cast<CFArrayRef>(found->get());
        for (CFIndex index = 0; index < CFArrayGetCount(entries); ++index) {
            auto const* const fields  = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(entries, index));
            auto const        service = text_field(fields, kSecAttrService);
            if (service && service->starts_with(marker) && service->size() > marker.size() &&
                text_field(fields, kSecAttrAccount)) {
                names.push_back(service->substr(marker.size()));
            }
        }
        return names;
    }

    std::size_t max_key_size(std::string_view const group) noexcept {
        assert(!group.empty() && !group.contains('\0'));
        return group.size() <= field_bytes - marker.size() ? field_bytes : 0;
    }

    std::size_t max_value_size() noexcept {
        return field_bytes;
    }

    std::expected<std::vector<std::byte>, std::error_code>
    try_read(std::string_view const group, std::string_view const key) {
        auto const query = make_query(group, key);
        if (!query) {
            return std::unexpected{query.error()};
        }
        CFDictionarySetValue(query->fields.get(), kSecReturnData, kCFBooleanTrue);
        result_ptr result{nullptr, CFRelease};
        auto const status =
            noninteractive([&] noexcept { return SecItemCopyMatching(query->fields.get(), std::out_ptr(result)); });
        if (status != errSecSuccess) {
            return std::unexpected{native_error(status)};
        }
        auto const* const data = static_cast<CFDataRef>(result.get());
        if (CFDataGetLength(data) == 0) {
            return std::vector<std::byte>{};
        }
        auto const bytes =
            std::as_bytes(std::span{CFDataGetBytePtr(data), static_cast<std::size_t>(CFDataGetLength(data))});
        return std::vector<std::byte>{std::from_range, bytes};
    }

    std::error_code
    try_write(std::string_view const group, std::string_view const key, std::span<std::byte const> const value) {
        if (value.size() > max_value_size()) {
            return std::make_error_code(std::errc::value_too_large);
        }
        auto const query = make_query(group, key);
        if (!query) {
            return query.error();
        }
        // File-based updates treat a null data pointer as unchanged, even at length zero
        UInt8 const empty{};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto const* const bytes = value.empty() ? &empty : reinterpret_cast<UInt8 const*>(value.data());
        std::unique_ptr<std::remove_pointer_t<CFDataRef>, decltype(&CFRelease)> const data{
            CFDataCreateWithBytesNoCopy(nullptr, bytes, static_cast<CFIndex>(value.size()), kCFAllocatorNull), CFRelease
        };
        if (!data) {
            throw std::bad_alloc{};
        }
        auto const changes = attributes();
        CFDictionarySetValue(changes.get(), kSecValueData, data.get());
        auto status = noninteractive([&] noexcept { return SecItemUpdate(query->fields.get(), changes.get()); });
        if (status == errSecItemNotFound) {
            CFDictionaryRemoveValue(query->fields.get(), kSecMatchSearchList);
            CFDictionarySetValue(query->fields.get(), kSecUseKeychain, query->keychain.get());
            CFDictionarySetValue(query->fields.get(), kSecValueData, data.get());
            status = noninteractive([&] noexcept { return SecItemAdd(query->fields.get(), nullptr); });
        }
        return native_error(status);
    }

    std::expected<bool, std::error_code> try_erase(std::string_view const group, std::string_view const key) {
        auto const query = make_query(group, key);
        if (!query) {
            return std::unexpected{query.error()};
        }
        auto const status = noninteractive([&] noexcept { return SecItemDelete(query->fields.get()); });
        if (status == errSecItemNotFound) {
            return false;
        }
        if (status != errSecSuccess) {
            return std::unexpected{native_error(status)};
        }
        return true;
    }

    std::expected<std::vector<std::string>, std::error_code> try_get_keys(std::string_view const group) {
        auto const query = make_query(group, std::nullopt);
        if (!query) {
            return std::unexpected{query.error()};
        }
        auto const found = search(*query);
        if (!found) {
            return std::unexpected{found.error()};
        }
        std::vector<std::string> keys;
        if (!*found) {
            return keys;
        }
        auto const* const entries = static_cast<CFArrayRef>(found->get());
        for (CFIndex index = 0; index < CFArrayGetCount(entries); ++index) {
            auto const* const fields = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(entries, index));
            if (auto key = text_field(fields, kSecAttrAccount)) {
                keys.push_back(std::move(*key));
            }
        }
        return keys;
    }

    std::error_code try_clear(std::string_view const group) {
        auto const query = make_query(group, std::nullopt);
        if (!query) {
            return query.error();
        }
        auto const found = search(*query);
        if (!found) {
            return found.error();
        }
        if (!*found) {
            return {};
        }
        auto const* const entries = static_cast<CFArrayRef>(found->get());
        auto const        removal = attributes();
        for (CFIndex index = 0; index < CFArrayGetCount(entries); ++index) {
            auto const* const fields = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(entries, index));
            if (!text_field(fields, kSecAttrAccount)) {
                continue;
            }
            auto const* const item = CFDictionaryGetValue(fields, kSecValueRef);
            assert(item);
            CFDictionarySetValue(removal.get(), kSecValueRef, item);
            auto const status = noninteractive([&] noexcept { return SecItemDelete(removal.get()); });
            if (status != errSecSuccess && status != errSecItemNotFound) {
                return native_error(status);
            }
        }
        return {};
    }
} // namespace osvault::detail
