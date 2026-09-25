#include "osvault.h"
#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include "backend.h"

namespace osvault {

    namespace {
        [[nodiscard]] bool valid_name(std::string_view const name) noexcept {
            return !name.empty() && !name.contains('\0');
        }

        template<typename T>
        [[nodiscard]] T value_or_throw(std::expected<T, std::error_code> result, char const* const operation) {
            if (!result) {
                throw std::system_error{result.error(), operation};
            }
            return std::move(*result);
        }
    } // namespace

    vault::vault() :
        vault(std::string{default_name}) {}

    vault::vault(std::string name) :
        name_(std::move(name)) {
        if (!valid_name(name_)) {
            throw std::invalid_argument{"Vault name must be nonempty and contain no null bytes"};
        }
    }

    vault::vault(vault&& other) noexcept :
        name_(std::move(other.name_)) {
        other.name_.clear();
    }

    vault& vault::operator=(vault&& other) noexcept {
        if (this != &other) {
            name_ = std::move(other.name_);
            other.name_.clear();
        }
        return *this;
    }

    std::size_t vault::max_key_size() const noexcept {
        return name_.empty() ? 0 : detail::max_key_size(name_);
    }

    // Keep the public query a member alongside the name-dependent key limit
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    std::size_t vault::max_value_size() const noexcept {
        return detail::max_value_size();
    }

    std::vector<std::byte> vault::read(std::string_view const key) const {
        return value_or_throw(try_read(key), "read vault entry");
    }

    void vault::write(std::string_view const key, std::span<std::byte const> const value) {
        if (auto const error = try_write(key, value)) {
            throw std::system_error{error, "write vault entry"};
        }
    }

    bool vault::erase(std::string_view const key) {
        return value_or_throw(try_erase(key), "erase vault entry");
    }

    std::vector<std::string> vault::get_keys() const {
        return value_or_throw(try_get_keys(), "list vault keys");
    }

    void vault::clear() {
        osvault::clear(name_);
    }

    std::expected<std::vector<std::byte>, std::error_code> vault::try_read(std::string_view const key) const {
        if (name_.empty() || !valid_name(key)) {
            return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
        }
        return detail::try_read(name_, key);
    }

    std::error_code vault::try_write(std::string_view const key, std::span<std::byte const> const value) {
        if (name_.empty() || !valid_name(key)) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        return detail::try_write(name_, key, value);
    }

    std::expected<bool, std::error_code> vault::try_erase(std::string_view const key) {
        if (name_.empty() || !valid_name(key)) {
            return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
        }
        return detail::try_erase(name_, key);
    }

    std::expected<std::vector<std::string>, std::error_code> vault::try_get_keys() const {
        if (name_.empty()) {
            return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
        }
        return detail::try_get_keys(name_);
    }

    std::error_code vault::try_clear() {
        return osvault::try_clear(name_);
    }

    std::vector<std::string> enumerate() {
        return value_or_throw(try_enumerate(), "enumerate vaults");
    }

    std::expected<std::vector<std::string>, std::error_code> try_enumerate() {
        auto result = detail::try_enumerate();
        if (result) {
            std::ranges::sort(*result);
            auto const duplicates = std::ranges::unique(*result);
            result->erase(duplicates.begin(), duplicates.end());
        }
        return result;
    }

    void clear(std::string_view const name) {
        if (auto const error = try_clear(name)) {
            throw std::system_error{error, "clear vault"};
        }
    }

    std::error_code try_clear(std::string_view const name) {
        if (!valid_name(name)) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        return detail::try_clear(name);
    }

} // namespace osvault
