#include "hex_codec.h"
#include <charconv>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace osvault::windows {

    std::string encode_hex(std::string_view const input) {
        std::string encoded;
        // Check before doubling to avoid overflow in the allocation size
        if (input.size() > encoded.max_size() / 2) {
            throw std::length_error{"Hexadecimal encoding exceeds the string size limit"};
        }
        encoded.reserve(input.size() * 2);
        // Preserve high-bit bytes even when char is signed
        for (auto const ch : input) {
            std::format_to(std::back_inserter(encoded), "{:02X}", static_cast<unsigned char>(ch));
        }
        return encoded;
    }

    std::optional<std::string> decode_hex(std::string_view const input) {
        if (input.size() % 2 != 0) {
            return std::nullopt;
        }
        std::string decoded;
        decoded.reserve(input.size() / 2);
        for (std::size_t index = 0; index < input.size(); index += 2) {
            auto const*  first = input.data() + index; // NOLINT
            auto const*  last  = first + 2;            // NOLINT
            unsigned int byte{};
            // Unsigned parsing rejects signs; full consumption rejects partial matches such as "0G"
            auto const [ptr, error] = std::from_chars(first, last, byte, 16);
            if (error != std::errc{} || ptr != last) {
                return std::nullopt;
            }
            decoded.push_back(static_cast<char>(byte));
        }
        return decoded;
    }

} // namespace osvault::windows
