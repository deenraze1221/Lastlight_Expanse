#include "World/Reflection.h"

#include <sstream>

namespace zodar {

std::string_view fieldTypeName(FieldType type) {
    switch (type) {
    case FieldType::Bool:
        return "bool";
    case FieldType::Uint:
        return "uint";
    case FieldType::Float:
        return "number";
    case FieldType::Vec3:
        return "[3]";
    case FieldType::Vec4:
        return "[4]";
    case FieldType::Quat:
        return "quat [x, y, z, w]";
    case FieldType::String:
        return "string";
    }
    return "?";
}

std::string formatField(const FieldInfo& field, const void* component) {
    std::ostringstream out;
    const auto list = [&out](const float* v, int n) {
        out << '[';
        for (int i = 0; i < n; ++i) {
            out << (i ? ", " : "") << v[i];
        }
        out << ']';
    };
    switch (field.type) {
    case FieldType::Bool:
        out << (field.get<bool>(component) ? "true" : "false");
        break;
    case FieldType::Uint:
        out << field.get<uint32_t>(component);
        break;
    case FieldType::Float:
        out << field.get<float>(component);
        break;
    case FieldType::Vec3:
        list(&field.get<glm::vec3>(component).x, 3);
        break;
    case FieldType::Vec4:
        list(&field.get<glm::vec4>(component).x, 4);
        break;
    case FieldType::Quat: {
        const glm::quat& q = field.get<glm::quat>(component);
        const float v[4] = {q.x, q.y, q.z, q.w};
        list(v, 4);
        break;
    }
    case FieldType::String:
        out << '"' << field.get<std::string>(component) << '"';
        break;
    }
    return out.str();
}

} // namespace zodar
