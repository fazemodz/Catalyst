#include "Common.h"
#include <stdexcept>

void ThrowIfFailed(HRESULT hr, const std::string& msg) {
    if (FAILED(hr)) {
        throw std::runtime_error(msg);
    }
}