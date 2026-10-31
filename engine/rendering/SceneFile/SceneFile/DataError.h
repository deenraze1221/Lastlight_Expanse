#pragma once

#include "Core/Error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zodar {

// One problem found in a data file. `line`/`column` (1-based) are set for syntax errors and
// unreadable files' first byte; schema errors carry the JSON pointer of the offending value
// (`path`, e.g. "/entities/2/transform/scale") instead. 0 means unknown.
struct DataIssue {
    std::string file;
    uint32_t line = 0;
    uint32_t column = 0;
    std::string path;
    std::string message;

    // "file:line:column: path: message" (missing parts omitted).
    [[nodiscard]] std::string toString() const;
};

// Thrown by the scene / material file functions. Validation collects every issue it can find
// before throwing; what() lists them one per line.
class DataError : public Error {
public:
    explicit DataError(std::vector<DataIssue> issues);
    [[nodiscard]] const std::vector<DataIssue>& issues() const noexcept { return m_issues; }

private:
    std::vector<DataIssue> m_issues;
};

} // namespace zodar
