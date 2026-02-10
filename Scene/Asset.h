#pragma once
#include <string>
#include "../Resources/Mesh.h"
#include "../Resources/Texture.h"

struct Asset {
    int id = -1;
    std::string name;
    std::string sourcePath;
    
    // Core Data
    Mesh* mesh = nullptr;
    Texture* texture = nullptr;   // Default Albedo
    Texture* normalMap = nullptr; // Default Normal
    
    // Settings
    bool useVirtualGeometry = false; // "Nanite" Toggle
    bool debugVisualizer = false;    // Visualizer Toggle
};