#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace osvault::detail {

    /// @brief Encode arbitrary bytes as uppercase hex
    ///
    /// @exception std::length_error The doubled size exceeds std::string::max_size()
    [[nodiscard]] std::string encode_hex(std::string_view input);

    /// @brief Decode hex of either case to raw bytes
    ///
    /// @return Decoded bytes, or nullopt for odd length or invalid digits
    [[nodiscard]] std::optional<std::string> decode_hex(std::string_view input);

} // namespace osvault::detail
