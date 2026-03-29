#pragma once
#include <windows.h>
#include <string>


void ThrowIfFailed(HRESULT hr, const std::string& msg = "DirectX Error");