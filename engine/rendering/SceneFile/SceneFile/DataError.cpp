#include "SceneFile/DataError.h"

namespace zodar {

namespace {

std::string joinIssues(const std::vector<DataIssue>& issues) {
    std::string text;
    for (const DataIssue& issue : issues) {
        if (!text.empty()) {
            text += '\n';
        }
        text += issue.toString();
    }
    return text;
}

} // namespace

std::string DataIssue::toString() const {
    std::string text = file;
    if (line > 0) {
        text += ':' + std::to_string(line);
        if (column > 0) {
            text += ':' + std::to_string(column);
        }
    }
    if (!path.empty()) {
        text += (text.empty() ? "" : ": ") + path;
    }
    text += (text.empty() ? "" : ": ") + message;
    return text;
}

DataError::DataError(std::vector<DataIssue> issues) : Error(joinIssues(issues)), m_issues(std::move(issues)) {}

} // namespace zodar
