#pragma once

#include <map>
#include <ostream>
#include <string>
#include <vector>
#include <DirectXMath.h>

namespace Json {

enum class ValueType {
    Null,
    Number,
    String,
    Bool,
    Array,
    Object
};

struct Value {
    ValueType type = ValueType::Null;
    double numberValue = 0.0;
    bool boolValue = false;
    std::string stringValue;
    std::vector<Value> arrayValue;
    std::map<std::string, Value> objectValue;
};

class Parser {
public:
    explicit Parser(const std::string& text);
    bool Parse(Value& outValue);

private:
    bool ParseValue(Value& outValue);
    bool ParseObject(Value& outValue);
    bool ParseArray(Value& outValue);
    bool ParseString(std::string& outValue);
    bool ParseNumber(double& outValue);
    void SkipWhitespace();
    bool Match(char expected);
    bool ConsumeLiteral(const char* literal);

    const std::string& m_text;
    size_t m_pos = 0;
};

const Value* FindField(const Value& object, const char* key);

std::string GetString(const Value& object, const char* key, const std::string& fallback = "");
float GetNumber(const Value& object, const char* key, float fallback);
int GetInt(const Value& object, const char* key, int fallback);
bool GetBool(const Value& object, const char* key, bool fallback);
uint32_t GetUInt(const Value& object, const char* key, uint32_t fallback);
DirectX::XMFLOAT3 GetFloat3(const Value& object, const char* key, const DirectX::XMFLOAT3& fallback);
DirectX::XMFLOAT4 GetFloat4(const Value& object, const char* key, const DirectX::XMFLOAT4& fallback);
std::vector<float> GetFloatArray(const Value& object, const char* key, size_t expectedCount, const std::vector<float>& fallback);

void WriteFloat3(std::ostream& out, const DirectX::XMFLOAT3& value);
void WriteFloat4(std::ostream& out, const DirectX::XMFLOAT4& value);

std::string Escape(const std::string& value);
std::wstring Escape(const std::wstring& value);

} // namespace Json
