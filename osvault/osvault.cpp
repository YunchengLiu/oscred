#include "osvault.h"
#include <stdexcept>
#include <string>
#include <utility>

namespace osvault {

    vault::vault() :
        vault(std::string{default_name}) {}

    vault::vault(std::string name) :
        name_(std::move(name)) {
        if (name_.empty() || name_.contains('\0')) {
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

} // namespace osvault
