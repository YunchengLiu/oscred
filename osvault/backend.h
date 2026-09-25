#pragma once
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace osvault::detail {

    [[nodiscard]] std::size_t max_key_size(std::string_view group) noexcept;

    [[nodiscard]] std::size_t max_value_size() noexcept;

    [[nodiscard]] std::expected<std::vector<std::byte>, std::error_code>
    try_read(std::string_view group, std::string_view key);

    [[nodiscard]] std::error_code
    try_write(std::string_view group, std::string_view key, std::span<std::byte const> value);

    [[nodiscard]] std::expected<bool, std::error_code> try_erase(std::string_view group, std::string_view key);

    [[nodiscard]] std::expected<std::vector<std::string>, std::error_code> try_get_keys(std::string_view group);

    [[nodiscard]] std::error_code try_clear(std::string_view group);

} // namespace osvault::detail
