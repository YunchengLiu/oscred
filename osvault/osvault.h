#pragma once
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace osvault {

    /// @brief A vault is an access handle for interacting with the OS's native protected storage
    ///
    /// @details The vault uses a key-value-based representation to store data.
    /// The key is a case-sensitive string, and the value is a byte array.
    /// The vault is identified by a name, which is used to group related key-value entries together.
    /// @note Destroying the vault object does not remove the stored entries.
    class vault {
        std::string name_;

    public:
        static constexpr std::string_view default_name = "_";

        /// @brief Create a vault with the default name
        vault();

        /// @brief Create a vault with a specific name
        /// @param name The name of the vault
        /// @exception std::invalid_argument The name is empty or contains a null byte
        explicit vault(std::string name);
        ~vault() = default;

        vault(vault const&)            = delete;
        vault& operator=(vault const&) = delete;
        vault(vault&& other) noexcept;
        vault& operator=(vault&& other) noexcept;

        [[nodiscard]] std::string_view name() const noexcept {
            return name_;
        }

        /// @brief Get the maximum key length in bytes on the current native platform
        [[nodiscard]] std::size_t max_key_size() const noexcept;

        /// @brief Get the maximum value size in bytes on the current native platform
        [[nodiscard]] std::size_t max_value_size() const noexcept;

        /// @brief Read the value for a key
        ///
        /// @exception std::system_error The operation fails
        [[nodiscard]] std::vector<std::byte> read(std::string_view key) const;

        /// @brief Store a value for a key, replacing any existing value
        ///
        /// @exception std::system_error The operation fails
        void write(std::string_view key, std::span<std::byte const> value);

        /// @brief Remove a key and its value
        ///
        /// @return True if removed, false if already absent
        /// @exception std::system_error The operation fails
        [[nodiscard]] bool erase(std::string_view key);

        /// @brief List keys in this vault
        ///
        /// @exception std::system_error The operation fails
        [[nodiscard]] std::vector<std::string> get_keys() const;

        /// @brief Remove all entries from this vault
        ///
        /// @exception std::system_error The operation fails
        void clear();

        /// @copybrief read
        [[nodiscard]] std::expected<std::vector<std::byte>, std::error_code> try_read(std::string_view key) const;

        /// @copybrief write
        [[nodiscard]] std::error_code try_write(std::string_view key, std::span<std::byte const> value);

        /// @copybrief erase
        /// @return True if removed, false if already absent, or an error
        [[nodiscard]] std::expected<bool, std::error_code> try_erase(std::string_view key);

        /// @copybrief get_keys
        [[nodiscard]] std::expected<std::vector<std::string>, std::error_code> try_get_keys() const;

        /// @copybrief clear
        [[nodiscard]] std::error_code try_clear();
    };
} // namespace osvault
