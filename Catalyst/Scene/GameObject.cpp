#include "GameObject.h"

#include <atomic>

namespace {
std::atomic<uint64_t> g_nextCatalystStableId{1};

uint64_t AllocateCatalystStableId() {
    return g_nextCatalystStableId.fetch_add(1, std::memory_order_relaxed);
}
}

NativeScriptComponentDesc::NativeScriptComponentDesc()
    : id(AllocateCatalystStableId()) {
}

GameObject::GameObject()
    : id(AllocateCatalystStableId()) {
}
