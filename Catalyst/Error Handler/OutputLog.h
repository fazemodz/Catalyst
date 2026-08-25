#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace OutputLog {
enum class Level : uint8_t { Info = 0, Warning = 1, Error = 2 };

struct Entry {
    std::string timestamp;
    std::string message;
    Level level = Level::Info;
};

void Add(const std::string& message, Level level = Level::Info);
bool Drain(std::vector<Entry>& out);
}
