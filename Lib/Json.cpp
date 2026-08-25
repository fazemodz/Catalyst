#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Json {

Parser::Parser(const std::string& text) : m_text(text) {}

bool Parser::Parse(Value& outValue) {
    // Windows editors commonly save JSON with a UTF-8 BOM. Without this the
    // parse fails on the very first byte and the caller silently falls back to
    // defaults, which is near-impossible to diagnose from the outside.
    if (m_pos == 0 && m_text.size() >= 3 &&
        static_cast<unsigned char>(m_text[0]) == 0xEF &&
        static_cast<unsigned char>(m_text[1]) == 0xBB &&
        static_cast<unsigned char>(m_text[2]) == 0xBF) {
        m_pos = 3;
    }

    SkipWhitespace();
    if (!ParseValue(outValue)) {
        return false;
    }
    SkipWhitespace();
    return m_pos == m_text.size();
}

bool Parser::ParseValue(Value& outValue) {
    SkipWhitespace();
    if (m_pos >= m_text.size()) {
        return false;
    }

    const char current = m_text[m_pos];
    if (current == '{') {
        return ParseObject(outValue);
    }
    if (current == '[') {
        return ParseArray(outValue);
    }
    if (current == '"') {
        outValue.type = ValueType::String;
        return ParseString(outValue.stringValue);
    }
    if (current == '-' || (current >= '0' && current <= '9')) {
        outValue.type = ValueType::Number;
        return ParseNumber(outValue.numberValue);
    }
    if (ConsumeLiteral("true")) {
        outValue.type = ValueType::Bool;
        outValue.boolValue = true;
        return true;
    }
    if (ConsumeLiteral("false")) {
        outValue.type = ValueType::Bool;
        outValue.boolValue = false;
        return true;
    }
    if (ConsumeLiteral("null")) {
        outValue.type = ValueType::Null;
        return true;
    }
    return false;
}

bool Parser::ParseObject(Value& outValue) {
    if (!Match('{')) {
        return false;
    }

    outValue.type = ValueType::Object;
    outValue.objectValue.clear();
    SkipWhitespace();
    if (Match('}')) {
        return true;
    }

    while (m_pos < m_text.size()) {
        std::string key;
        if (!ParseString(key)) {
            return false;
        }
        SkipWhitespace();
        if (!Match(':')) {
            return false;
        }

        Value child;
        if (!ParseValue(child)) {
            return false;
        }
        outValue.objectValue[key] = child;

        SkipWhitespace();
        if (Match('}')) {
            return true;
        }
        if (!Match(',')) {
            return false;
        }
        SkipWhitespace();
    }

    return false;
}

bool Parser::ParseArray(Value& outValue) {
    if (!Match('[')) {
        return false;
    }

    outValue.type = ValueType::Array;
    outValue.arrayValue.clear();
    SkipWhitespace();
    if (Match(']')) {
        return true;
    }

    while (m_pos < m_text.size()) {
        Value child;
        if (!ParseValue(child)) {
            return false;
        }
        outValue.arrayValue.push_back(child);

        SkipWhitespace();
        if (Match(']')) {
            return true;
        }
        if (!Match(',')) {
            return false;
        }
        SkipWhitespace();
    }

    return false;
}

bool Parser::ParseString(std::string& outValue) {
    if (!Match('"')) {
        return false;
    }

    outValue.clear();
    while (m_pos < m_text.size()) {
        const char current = m_text[m_pos++];
        if (current == '"') {
            return true;
        }
        if (current != '\\') {
            outValue += current;
            continue;
        }
        if (m_pos >= m_text.size()) {
            return false;
        }

        const char escaped = m_text[m_pos++];
        switch (escaped) {
        case '"':
        case '\\':
        case '/':
            outValue += escaped;
            break;
        case 'b':
            outValue += '\b';
            break;
        case 'f':
            outValue += '\f';
            break;
        case 'n':
            outValue += '\n';
            break;
        case 'r':
            outValue += '\r';
            break;
        case 't':
            outValue += '\t';
            break;
        default:
            return false;
        }
    }

    return false;
}

bool Parser::ParseNumber(double& outValue) {
    const size_t start = m_pos;
    if (m_text[m_pos] == '-') {
        ++m_pos;
    }
    while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
        ++m_pos;
    }
    if (m_pos < m_text.size() && m_text[m_pos] == '.') {
        ++m_pos;
        while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
            ++m_pos;
        }
    }
    if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
        ++m_pos;
        if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) {
            ++m_pos;
        }
        while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])) != 0) {
            ++m_pos;
        }
    }

    try {
        outValue = std::stod(m_text.substr(start, m_pos - start));
        return true;
    } catch (...) {
        return false;
    }
}

void Parser::SkipWhitespace() {
    while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos])) != 0) {
        ++m_pos;
    }
}

bool Parser::Match(char expected) {
    if (m_pos < m_text.size() && m_text[m_pos] == expected) {
        ++m_pos;
        return true;
    }
    return false;
}

bool Parser::ConsumeLiteral(const char* literal) {
    const size_t literalLength = std::char_traits<char>::length(literal);
    if (m_pos + literalLength > m_text.size()) {
        return false;
    }
    if (m_text.compare(m_pos, literalLength, literal) != 0) {
        return false;
    }
    m_pos += literalLength;
    return true;
}

const Value* FindField(const Value& object, const char* key) {
    if (object.type != ValueType::Object) {
        return nullptr;
    }
    auto found = object.objectValue.find(key);
    return found != object.objectValue.end() ? &found->second : nullptr;
}

std::string GetString(const Value& object, const char* key, const std::string& fallback) {
    const Value* field = FindField(object, key);
    return (field != nullptr && field->type == ValueType::String) ? field->stringValue : fallback;
}

float GetNumber(const Value& object, const char* key, float fallback) {
    const Value* field = FindField(object, key);
    return (field != nullptr && field->type == ValueType::Number) ? static_cast<float>(field->numberValue) : fallback;
}

int GetInt(const Value& object, const char* key, int fallback) {
    const Value* field = FindField(object, key);
    return (field != nullptr && field->type == ValueType::Number) ? static_cast<int>(field->numberValue) : fallback;
}

bool GetBool(const Value& object, const char* key, bool fallback) {
    const Value* field = FindField(object, key);
    return (field != nullptr && field->type == ValueType::Bool) ? field->boolValue : fallback;
}

uint32_t GetUInt(const Value& object, const char* key, uint32_t fallback) {
    const Value* field = FindField(object, key);
    return (field != nullptr && field->type == ValueType::Number) ? static_cast<uint32_t>(field->numberValue) : fallback;
}

DirectX::XMFLOAT3 GetFloat3(const Value& object, const char* key, const DirectX::XMFLOAT3& fallback) {
    const Value* field = FindField(object, key);
    if (field == nullptr || field->type != ValueType::Array || field->arrayValue.size() != 3) {
        return fallback;
    }
    for (const Value& component : field->arrayValue) {
        if (component.type != ValueType::Number) {
            return fallback;
        }
    }
    return {
        static_cast<float>(field->arrayValue[0].numberValue),
        static_cast<float>(field->arrayValue[1].numberValue),
        static_cast<float>(field->arrayValue[2].numberValue)
    };
}

DirectX::XMFLOAT4 GetFloat4(const Value& object, const char* key, const DirectX::XMFLOAT4& fallback) {
    const Value* field = FindField(object, key);
    if (field == nullptr || field->type != ValueType::Array || field->arrayValue.size() != 4) {
        return fallback;
    }
    for (const Value& component : field->arrayValue) {
        if (component.type != ValueType::Number) {
            return fallback;
        }
    }
    return {
        static_cast<float>(field->arrayValue[0].numberValue),
        static_cast<float>(field->arrayValue[1].numberValue),
        static_cast<float>(field->arrayValue[2].numberValue),
        static_cast<float>(field->arrayValue[3].numberValue)
    };
}

std::vector<float> GetFloatArray(const Value& object, const char* key, size_t expectedCount, const std::vector<float>& fallback) {
    const Value* field = FindField(object, key);
    if (field == nullptr || field->type != ValueType::Array || field->arrayValue.size() != expectedCount) {
        return fallback;
    }
    std::vector<float> values;
    values.reserve(expectedCount);
    for (const Value& component : field->arrayValue) {
        if (component.type != ValueType::Number) {
            return fallback;
        }
        values.push_back(static_cast<float>(component.numberValue));
    }
    return values;
}

void WriteFloat3(std::ostream& out, const DirectX::XMFLOAT3& value) {
    out << "[" << value.x << ", " << value.y << ", " << value.z << "]";
}

void WriteFloat4(std::ostream& out, const DirectX::XMFLOAT4& value) {
    out << "[" << value.x << ", " << value.y << ", " << value.z << ", " << value.w << "]";
}

std::string Escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"':  escaped += "\\\""; break;
        case '\n': escaped += "\\n";  break;
        case '\r': escaped += "\\r";  break;
        case '\t': escaped += "\\t";  break;
        default:   escaped += ch;     break;
        }
    }
    return escaped;
}

std::wstring Escape(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size());
    for (wchar_t ch : value) {
        switch (ch) {
        case L'\\': escaped += L"\\\\"; break;
        case L'"':  escaped += L"\\\""; break;
        case L'\n': escaped += L"\\n";  break;
        case L'\r': escaped += L"\\r";  break;
        case L'\t': escaped += L"\\t";  break;
        default:    escaped += ch;      break;
        }
    }
    return escaped;
}

} // namespace Json
