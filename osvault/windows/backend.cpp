#include "backend.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <Windows.h>
#include <wincred.h>
#include "hex_codec.h"

namespace osvault::windows {
    namespace {

        // WinCred's RPC string bound includes the terminator; the marker and separators use seven more characters
        constexpr std::size_t field_bytes = (CRED_MAX_GENERIC_TARGET_NAME_LENGTH - 1 - 7) / 2;

        struct credentials {
            DWORD                                              count{};
            std::unique_ptr<PCREDENTIALW, decltype(&CredFree)> entries{nullptr, &CredFree};
        };

        [[nodiscard]] std::error_code last_error() noexcept {
            return {static_cast<int>(GetLastError()), std::system_category()};
        }

        [[nodiscard]] std::wstring prefix(std::string_view const group) {
            auto const encoded = std::format("osvlt/{}/", encode_hex(group));
            return {encoded.begin(), encoded.end()};
        }

        [[nodiscard]] std::expected<std::wstring, std::error_code>
        target_name(std::string_view const group, std::string_view const key) {
            assert(!key.empty() && !key.contains('\0'));
            // Validate byte sizes before hex expansion or narrowing to Win32 lengths
            if (key.size() > max_key_size(group)) {
                return std::unexpected{std::make_error_code(std::errc::filename_too_long)};
            }
            auto       target  = prefix(group);
            auto const encoded = encode_hex(key);
            target.append(encoded.begin(), encoded.end());
            return target;
        }

        [[nodiscard]] std::expected<credentials, std::error_code> enumerate(std::wstring const& prefix) {
            credentials result;
            // keep the filter alive until the error is captured; deallocation may change GetLastError
            auto const filter = prefix + L"*";
            // credEnumerate returns the array and entries in one CredFree-owned block
            if (CredEnumerateW(filter.c_str(), 0, &result.count, std::out_ptr(result.entries)) == 0) {
                auto const error = last_error();
                if (error.value() != ERROR_NOT_FOUND) {
                    return std::unexpected{error};
                }
                result.count = 0;
                return result;
            }
            return result;
        }

        [[nodiscard]] std::optional<std::string> owned_key(CREDENTIALW const& record, std::wstring_view const prefix) {
            if (record.Type != CRED_TYPE_GENERIC) {
                return std::nullopt;
            }
            std::wstring_view const target{record.TargetName};
            // credEnumerate already guarantees the filter prefix; the suffix remains external data
            assert(target.size() >= prefix.size());
            assert(
                CompareStringOrdinal(
                    // NOLINTNEXTLINE
                    target.data(), static_cast<int>(prefix.size()), prefix.data(), static_cast<int>(prefix.size()), TRUE
                ) == CSTR_EQUAL
            );
            auto const suffix = target.substr(prefix.size());
            if (!std::ranges::all_of(suffix, [](wchar_t const ch) noexcept { return ch <= 0x7F; })) {
                return std::nullopt;
            }
            auto key = decode_hex(std::string{suffix.begin(), suffix.end()});
            // stored records may bypass vault's input validation; only valid decoded keys belong to this format
            if (!key || key->empty() || key->contains('\0')) {
                return std::nullopt;
            }
            return key;
        }

    } // namespace

    std::size_t max_key_size(std::string_view const group) noexcept {
        assert(!group.empty() && !group.contains('\0'));
        return group.size() < field_bytes ? field_bytes - group.size() : 0;
    }

    std::size_t max_value_size() noexcept {
        return CRED_MAX_CREDENTIAL_BLOB_SIZE;
    }

    std::expected<std::vector<std::byte>, std::error_code>
    try_read(std::string_view const group, std::string_view const key) {
        auto const target = target_name(group, key);
        if (!target) {
            return std::unexpected{target.error()};
        }
        std::unique_ptr<CREDENTIALW, decltype(&CredFree)> record{nullptr, &CredFree};
        if (CredReadW(target->c_str(), CRED_TYPE_GENERIC, 0, std::out_ptr(record)) == 0) {
            return std::unexpected{last_error()};
        }
        if (record->CredentialBlobSize == 0) {
            return std::vector<std::byte>{};
        }
        auto const bytes = std::as_bytes(std::span{record->CredentialBlob, record->CredentialBlobSize});
        return std::vector<std::byte>{bytes.begin(), bytes.end()};
    }

    std::error_code
    try_write(std::string_view const group, std::string_view const key, std::span<std::byte const> const value) {
        auto target = target_name(group, key);
        if (!target) {
            return target.error();
        }
        if (value.size() > max_value_size()) {
            return std::make_error_code(std::errc::value_too_large);
        }
        CREDENTIALW record{};
        record.Type               = CRED_TYPE_GENERIC;
        record.TargetName         = target->data();
        record.Persist            = CRED_PERSIST_LOCAL_MACHINE;
        record.CredentialBlobSize = static_cast<DWORD>(value.size());
        // WinCred declares mutable pointers but reads the supplied blob during this call
        // NOLINTNEXTLINE
        record.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<std::byte*>(value.data()));
        if (CredWriteW(&record, 0) == 0) {
            return last_error();
        }
        return {};
    }

    std::expected<bool, std::error_code> try_erase(std::string_view const group, std::string_view const key) {
        auto const target = target_name(group, key);
        if (!target) {
            return std::unexpected{target.error()};
        }
        if (CredDeleteW(target->c_str(), CRED_TYPE_GENERIC, 0) == 0) {
            auto const error = last_error();
            if (error.value() == ERROR_NOT_FOUND) {
                return false;
            }
            return std::unexpected{error};
        }
        return true;
    }

    std::expected<std::vector<std::string>, std::error_code> try_get_keys(std::string_view const group) {
        if (max_key_size(group) == 0) {
            return std::unexpected{std::make_error_code(std::errc::filename_too_long)};
        }
        auto const group_prefix = prefix(group);
        auto const records      = enumerate(group_prefix);
        if (!records) {
            return std::unexpected{records.error()};
        }
        std::vector<std::string> keys;
        for (auto const* record : std::span{records->entries.get(), records->count}) {
            if (auto key = owned_key(*record, group_prefix)) {
                keys.push_back(std::move(*key));
            }
        }
        return keys;
    }

    std::error_code try_clear(std::string_view const group) {
        if (max_key_size(group) == 0) {
            return std::make_error_code(std::errc::filename_too_long);
        }
        auto const group_prefix = prefix(group);
        auto const records      = enumerate(group_prefix);
        if (!records) {
            return records.error();
        }
        for (auto const* record : std::span{records->entries.get(), records->count}) {
            // Finish decoding before the Win32 call so temporary destruction cannot overwrite its error
            if (!owned_key(*record, group_prefix)) {
                continue;
            }
            if (CredDeleteW(record->TargetName, CRED_TYPE_GENERIC, 0) == 0) {
                auto const error = last_error();
                if (error.value() != ERROR_NOT_FOUND) {
                    // if a deletion fails, earlier deletions remain committed; callers can retry the same group
                    return error;
                }
            }
        }
        return {};
    }

} // namespace osvault::windows
