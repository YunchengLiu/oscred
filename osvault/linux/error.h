#pragma once
#include <system_error>
#include <glib.h>

namespace osvault::detail {

    // Translate operation failures to generic codes where exact, retaining unmapped native identities
    [[nodiscard]] std::error_code native_error(GError const& error);

} // namespace osvault::detail
