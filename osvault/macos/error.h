#pragma once
#include <system_error>
#include <MacTypes.h>

namespace osvault::detail {

    // Retain native statuses where no generic code describes the same failure
    [[nodiscard]] std::error_code native_error(OSStatus status);

} // namespace osvault::detail
