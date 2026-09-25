#pragma once

#include <concepts>
#include <exception>
#include <ostream>
#include <source_location>
#include <system_error>
#include <doctest/doctest.h>

// Preserve structured error checks without depending on localized exception messages
template<typename Operation>
    requires std::invocable<Operation&>
void expect_throw_with_code(
    Operation operation, std::error_code const expected,
    std::source_location const location = std::source_location::current()
) {
    INFO("Expectation at ", location.file_name(), ':', location.line());
    try {
        static_cast<void>(operation());
    } catch (std::system_error const& error) {
        CHECK(error.code() == expected);
        return;
    } catch (std::exception const& error) {
        FAIL_CHECK("Expected std::system_error, caught: ", error.what());
        return;
    } catch (...) {
        FAIL_CHECK("Expected std::system_error, caught an unknown exception");
        return;
    }
    FAIL_CHECK("Expected std::system_error, but no exception was thrown");
}
