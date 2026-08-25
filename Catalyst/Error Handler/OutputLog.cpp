#include "OutputLog.h"

#include <mutex>
#include <windows.h>

namespace OutputLog {
namespace {
std::mutex g_mutex;
std::vector<Entry> g_pending;

std::string CurrentTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    auto pad2 = [](int n) { return (n < 10 ? "0" : "") + std::to_string(n); };
    return pad2(st.wHour) + ":" + pad2(st.wMinute) + ":" + pad2(st.wSecond);
}
}

void Add(const std::string& message, Level level) {
    OutputDebugStringA(("[OutputLog] " + message + "\n").c_str());
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_pending.size() >= 2000) {
        return;
    }
    g_pending.push_back({CurrentTimestamp(), message, level});
}

bool Drain(std::vector<Entry>& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_pending.empty()) {
        return false;
    }
    out.insert(out.end(),
               std::make_move_iterator(g_pending.begin()),
               std::make_move_iterator(g_pending.end()));
    g_pending.clear();
    return true;
}
}
