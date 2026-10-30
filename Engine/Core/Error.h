#pragma once

#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace raven {

// Unrecoverable engine error (failed initialization, lost device, missing files).
// Recoverable conditions are reported through return values instead.
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

// Logs the message with its source location and throws raven::Error.
[[noreturn]] void fail(std::string_view message,
                       std::source_location location = std::source_location::current());

} // namespace raven

// Checks an invariant in every build configuration and fails with a message.
#define RAVEN_ENSURE(condition, message)                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            ::raven::fail(std::string("Check failed: " #condition " - ") +  \
                          std::string(message));                            \
        }                                                                   \
    } while (false)
