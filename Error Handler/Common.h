#pragma once
#include <windows.h>
#include <string>

// Centralized error handling for the entire engine
void ThrowIfFailed(HRESULT hr, const std::string& msg = "DirectX Error");