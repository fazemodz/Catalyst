#define NOMINMAX
#include "FbxParser.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>
#include <unordered_map>

#include <DirectXMath.h>

#include "Inflate.h"
#include "PolygonTriangulate.h"

using namespace DirectX;

namespace CatalystImport {
namespace {

constexpr char kBinaryMagic[] = "Kaydara FBX Binary  ";
constexpr size_t kMagicLength = 20;    // the trailing NUL and 0x1A 0x00 follow
constexpr size_t kHeaderLength = 27;

// ---------------------------------------------------------------------------
//  Node tree
// ---------------------------------------------------------------------------

// One property of a node. Scalars and arrays are normalised into a common
// representation so a reader does not have to care whether the exporter chose
// float or double, int32 or int64.
struct FbxProperty {
    char type = 0;
    int64_t scalarInt = 0;
    double scalarDouble = 0.0;
    std::string text;
    std::vector<double> doubles;
    std::vector<int64_t> ints;
};

struct FbxNode {
    std::string name;
    std::vector<FbxProperty> properties;
    std::vector<FbxNode> children;

    const FbxNode* Find(const char* childName) const {
        for (const FbxNode& child : children) {
            if (child.name == childName) {
                return &child;
            }
        }
        return nullptr;
    }
};

class FbxReader {
public:
    FbxReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    bool ParseHeader(std::string* outError) {
        if (m_size < kHeaderLength || std::memcmp(m_data, kBinaryMagic, kMagicLength) != 0) {
            if (outError != nullptr) {
                *outError = "Not a binary FBX file.";
            }
            return false;
        }
        std::memcpy(&m_version, m_data + 23, sizeof(uint32_t));
        // From 7.5 onwards the record offsets widened to 64 bits.
        m_wideOffsets = (m_version >= 7500);
        m_cursor = kHeaderLength;
        return true;
    }

    uint32_t Version() const { return m_version; }

    bool ParseNodes(std::vector<FbxNode>& out, std::string* outError) {
        while (m_cursor < m_size) {
            FbxNode node;
            bool isNull = false;
            if (!ReadNode(node, isNull, outError)) {
                return false;
            }
            if (isNull) {
                break;
            }
            out.push_back(std::move(node));
        }
        return true;
    }

private:
    bool Want(size_t bytes) const { return m_cursor + bytes <= m_size; }

    template <typename T>
    bool Read(T& value) {
        if (!Want(sizeof(T))) {
            return false;
        }
        std::memcpy(&value, m_data + m_cursor, sizeof(T));
        m_cursor += sizeof(T);
        return true;
    }

    bool ReadOffset(uint64_t& value) {
        if (m_wideOffsets) {
            return Read(value);
        }
        uint32_t narrow = 0;
        if (!Read(narrow)) {
            return false;
        }
        value = narrow;
        return true;
    }

    bool ReadArrayProperty(FbxProperty& property, std::string* outError) {
        uint32_t length = 0;
        uint32_t encoding = 0;
        uint32_t compressedLength = 0;
        if (!Read(length) || !Read(encoding) || !Read(compressedLength)) {
            return Fail(outError, "An array property header is truncated.");
        }

        size_t elementSize = 0;
        switch (property.type) {
        case 'f': case 'i': elementSize = 4; break;
        case 'd': case 'l': elementSize = 8; break;
        case 'b': case 'c': elementSize = 1; break;
        default: return Fail(outError, "Unknown array property type.");
        }

        const size_t rawSize = static_cast<size_t>(length) * elementSize;
        std::vector<uint8_t> storage;
        const uint8_t* elements = nullptr;

        if (encoding == 0) {
            if (!Want(rawSize)) {
                return Fail(outError, "An uncompressed array runs past the end of the file.");
            }
            elements = m_data + m_cursor;
            m_cursor += rawSize;
        } else if (encoding == 1) {
            if (!Want(compressedLength)) {
                return Fail(outError, "A compressed array runs past the end of the file.");
            }
            std::string inflateError;
            if (!Inflate(m_data + m_cursor, compressedLength, rawSize, storage, &inflateError)) {
                return Fail(outError, ("An array could not be decompressed: " + inflateError).c_str());
            }
            m_cursor += compressedLength;
            elements = storage.data();
        } else {
            return Fail(outError, "An array uses an unknown encoding.");
        }

        // Normalise into doubles or int64s so callers do not branch on width.
        if (property.type == 'f') {
            property.doubles.resize(length);
            for (uint32_t i = 0; i < length; ++i) {
                float value = 0.0f;
                std::memcpy(&value, elements + static_cast<size_t>(i) * 4, 4);
                property.doubles[i] = value;
            }
        } else if (property.type == 'd') {
            property.doubles.resize(length);
            for (uint32_t i = 0; i < length; ++i) {
                double value = 0.0;
                std::memcpy(&value, elements + static_cast<size_t>(i) * 8, 8);
                property.doubles[i] = value;
            }
        } else if (property.type == 'i') {
            property.ints.resize(length);
            for (uint32_t i = 0; i < length; ++i) {
                int32_t value = 0;
                std::memcpy(&value, elements + static_cast<size_t>(i) * 4, 4);
                property.ints[i] = value;
            }
        } else if (property.type == 'l') {
            property.ints.resize(length);
            for (uint32_t i = 0; i < length; ++i) {
                int64_t value = 0;
                std::memcpy(&value, elements + static_cast<size_t>(i) * 8, 8);
                property.ints[i] = value;
            }
        } else {
            property.ints.resize(length);
            for (uint32_t i = 0; i < length; ++i) {
                property.ints[i] = elements[i];
            }
        }
        return true;
    }

    bool ReadProperty(FbxProperty& property, std::string* outError) {
        uint8_t type = 0;
        if (!Read(type)) {
            return Fail(outError, "A property type code is missing.");
        }
        property.type = static_cast<char>(type);

        switch (property.type) {
        case 'Y': { int16_t v = 0; if (!Read(v)) return Fail(outError, "Truncated Y property."); property.scalarInt = v; return true; }
        case 'C': { uint8_t v = 0; if (!Read(v)) return Fail(outError, "Truncated C property."); property.scalarInt = v; return true; }
        case 'I': { int32_t v = 0; if (!Read(v)) return Fail(outError, "Truncated I property."); property.scalarInt = v; return true; }
        case 'L': { int64_t v = 0; if (!Read(v)) return Fail(outError, "Truncated L property."); property.scalarInt = v; return true; }
        case 'F': { float v = 0.0f; if (!Read(v)) return Fail(outError, "Truncated F property."); property.scalarDouble = v; return true; }
        case 'D': { double v = 0.0; if (!Read(v)) return Fail(outError, "Truncated D property."); property.scalarDouble = v; return true; }
        case 'S':
        case 'R': {
            uint32_t length = 0;
            if (!Read(length) || !Want(length)) {
                return Fail(outError, "Truncated string property.");
            }
            property.text.assign(reinterpret_cast<const char*>(m_data + m_cursor), length);
            m_cursor += length;
            return true;
        }
        default:
            return ReadArrayProperty(property, outError);
        }
    }

    bool ReadNode(FbxNode& node, bool& isNull, std::string* outError) {
        uint64_t endOffset = 0;
        uint64_t propertyCount = 0;
        uint64_t propertyListLength = 0;
        uint8_t nameLength = 0;

        if (!ReadOffset(endOffset) || !ReadOffset(propertyCount) ||
            !ReadOffset(propertyListLength) || !Read(nameLength)) {
            return Fail(outError, "A node header is truncated.");
        }

        // A record of all zeroes closes the current nested list.
        if (endOffset == 0) {
            isNull = true;
            return true;
        }
        if (endOffset > m_size) {
            return Fail(outError, "A node claims to end past the end of the file.");
        }

        if (!Want(nameLength)) {
            return Fail(outError, "A node name is truncated.");
        }
        node.name.assign(reinterpret_cast<const char*>(m_data + m_cursor), nameLength);
        m_cursor += nameLength;

        node.properties.resize(static_cast<size_t>(propertyCount));
        for (uint64_t i = 0; i < propertyCount; ++i) {
            if (!ReadProperty(node.properties[static_cast<size_t>(i)], outError)) {
                return false;
            }
        }

        // Anything left before the end offset is a nested list of child nodes.
        while (m_cursor < endOffset) {
            FbxNode child;
            bool childIsNull = false;
            if (!ReadNode(child, childIsNull, outError)) {
                return false;
            }
            if (childIsNull) {
                break;
            }
            node.children.push_back(std::move(child));
        }

        m_cursor = static_cast<size_t>(endOffset);
        return true;
    }

    static bool Fail(std::string* outError, const char* message) {
        if (outError != nullptr) {
            *outError = message;
        }
        return false;
    }

    const uint8_t* m_data;
    size_t m_size;
    size_t m_cursor = 0;
    uint32_t m_version = 0;
    bool m_wideOffsets = false;
};

// ---------------------------------------------------------------------------
//  Property70 lookup
// ---------------------------------------------------------------------------

// A Properties70 entry looks like: P: name, type, subtype, flags, value...
// so the values start at index 4.
const FbxProperty* FindProperty70(const FbxNode& node, const char* wanted, size_t valueIndex) {
    const FbxNode* properties = node.Find("Properties70");
    if (properties == nullptr) {
        return nullptr;
    }
    for (const FbxNode& entry : properties->children) {
        if (entry.name != "P" || entry.properties.empty()) {
            continue;
        }
        if (entry.properties[0].text != wanted) {
            continue;
        }
        const size_t index = 4 + valueIndex;
        if (index < entry.properties.size()) {
            return &entry.properties[index];
        }
    }
    return nullptr;
}

XMFLOAT3 ReadVectorProperty(const FbxNode& node, const char* wanted, XMFLOAT3 fallback) {
    const FbxProperty* x = FindProperty70(node, wanted, 0);
    const FbxProperty* y = FindProperty70(node, wanted, 1);
    const FbxProperty* z = FindProperty70(node, wanted, 2);
    if (x == nullptr || y == nullptr || z == nullptr) {
        return fallback;
    }
    return {static_cast<float>(x->scalarDouble),
            static_cast<float>(y->scalarDouble),
            static_cast<float>(z->scalarDouble)};
}

// ---------------------------------------------------------------------------
//  Transforms
// ---------------------------------------------------------------------------

// FBX names its Euler orders by the sequence the rotations are applied in.
// With row vectors that is also the order the matrices multiply in, so XYZ
// composes as Rx * Ry * Rz.
enum class EulerOrder { XYZ = 0, XZY = 1, YZX = 2, YXZ = 3, ZXY = 4, ZYX = 5, Spherical = 6 };

XMMATRIX EulerToMatrix(const XMFLOAT3& degrees, EulerOrder order) {
    const float toRadians = 3.14159265358979f / 180.0f;
    const XMMATRIX x = XMMatrixRotationX(degrees.x * toRadians);
    const XMMATRIX y = XMMatrixRotationY(degrees.y * toRadians);
    const XMMATRIX z = XMMatrixRotationZ(degrees.z * toRadians);

    switch (order) {
    case EulerOrder::XZY: return x * z * y;
    case EulerOrder::YZX: return y * z * x;
    case EulerOrder::YXZ: return y * x * z;
    case EulerOrder::ZXY: return z * x * y;
    case EulerOrder::ZYX: return z * y * x;
    case EulerOrder::XYZ:
    case EulerOrder::Spherical:   // no spherical support; XYZ is the closest
    default:
        return x * y * z;
    }
}

struct ModelNode {
    XMMATRIX local = XMMatrixIdentity();
    XMMATRIX geometric = XMMatrixIdentity();
    int64_t parent = 0;
    bool resolved = false;
    XMMATRIX world = XMMatrixIdentity();
};

// The complete FBX node transform. In FBX's own column-vector notation:
//
//   M = T * Roff * Rp * Rpre * R * Rpost^-1 * Rp^-1 * Soff * Sp * S * Sp^-1
//
// which reverses for the row-vector convention the engine uses. Pivots and
// offsets are the part that was missing before: Maya and 3ds Max set them
// routinely, and dropping them puts every affected node in the wrong place
// while leaving the mesh itself looking correct.
XMMATRIX BuildLocalMatrix(const FbxNode& model) {
    const XMFLOAT3 translation = ReadVectorProperty(model, "Lcl Translation", {0.0f, 0.0f, 0.0f});
    const XMFLOAT3 rotation = ReadVectorProperty(model, "Lcl Rotation", {0.0f, 0.0f, 0.0f});
    const XMFLOAT3 scaling = ReadVectorProperty(model, "Lcl Scaling", {1.0f, 1.0f, 1.0f});
    const XMFLOAT3 preRotation = ReadVectorProperty(model, "PreRotation", {0.0f, 0.0f, 0.0f});
    const XMFLOAT3 postRotation = ReadVectorProperty(model, "PostRotation", {0.0f, 0.0f, 0.0f});
    const XMFLOAT3 rotationOffset = ReadVectorProperty(model, "RotationOffset", {0.0f, 0.0f, 0.0f});
    const XMFLOAT3 rotationPivot = ReadVectorProperty(model, "RotationPivot", {0.0f, 0.0f, 0.0f});
    const XMFLOAT3 scalingOffset = ReadVectorProperty(model, "ScalingOffset", {0.0f, 0.0f, 0.0f});
    const XMFLOAT3 scalingPivot = ReadVectorProperty(model, "ScalingPivot", {0.0f, 0.0f, 0.0f});

    EulerOrder order = EulerOrder::XYZ;
    if (const FbxProperty* value = FindProperty70(model, "RotationOrder", 0)) {
        const int64_t raw = value->scalarInt;
        if (raw >= 0 && raw <= 6) {
            order = static_cast<EulerOrder>(raw);
        }
    }

    const XMMATRIX T = XMMatrixTranslation(translation.x, translation.y, translation.z);
    const XMMATRIX Roff = XMMatrixTranslation(rotationOffset.x, rotationOffset.y, rotationOffset.z);
    const XMMATRIX Rp = XMMatrixTranslation(rotationPivot.x, rotationPivot.y, rotationPivot.z);
    const XMMATRIX RpInv = XMMatrixTranslation(-rotationPivot.x, -rotationPivot.y, -rotationPivot.z);
    const XMMATRIX Rpre = EulerToMatrix(preRotation, EulerOrder::XYZ);
    const XMMATRIX R = EulerToMatrix(rotation, order);
    const XMMATRIX RpostInv = XMMatrixTranspose(EulerToMatrix(postRotation, EulerOrder::XYZ));
    const XMMATRIX Soff = XMMatrixTranslation(scalingOffset.x, scalingOffset.y, scalingOffset.z);
    const XMMATRIX Sp = XMMatrixTranslation(scalingPivot.x, scalingPivot.y, scalingPivot.z);
    const XMMATRIX SpInv = XMMatrixTranslation(-scalingPivot.x, -scalingPivot.y, -scalingPivot.z);
    const XMMATRIX S = XMMatrixScaling(scaling.x, scaling.y, scaling.z);

    return SpInv * S * Sp * Soff * RpInv * RpostInv * R * Rpre * Rp * Roff * T;
}

// The geometric transform offsets the mesh from its node and is deliberately
// not inherited by children.
XMMATRIX BuildGeometricMatrix(const FbxNode& model) {
    const XMFLOAT3 translation = ReadVectorProperty(model, "GeometricTranslation", {0.0f, 0.0f, 0.0f});
    const XMFLOAT3 rotation = ReadVectorProperty(model, "GeometricRotation", {0.0f, 0.0f, 0.0f});
    const XMFLOAT3 scaling = ReadVectorProperty(model, "GeometricScaling", {1.0f, 1.0f, 1.0f});

    return XMMatrixScaling(scaling.x, scaling.y, scaling.z) *
           EulerToMatrix(rotation, EulerOrder::XYZ) *
           XMMatrixTranslation(translation.x, translation.y, translation.z);
}

// ---------------------------------------------------------------------------
//  Layer elements
// ---------------------------------------------------------------------------

enum class MappingMode { ByControlPoint, ByPolygonVertex, ByPolygon, AllSame, Unknown };
enum class ReferenceMode { Direct, IndexToDirect, Unknown };

MappingMode ReadMappingMode(const FbxNode& layer) {
    const FbxNode* node = layer.Find("MappingInformationType");
    if (node == nullptr || node->properties.empty()) {
        return MappingMode::Unknown;
    }
    const std::string& text = node->properties[0].text;
    if (text == "ByControlPoint" || text == "ByVertice" || text == "ByVertex") return MappingMode::ByControlPoint;
    if (text == "ByPolygonVertex") return MappingMode::ByPolygonVertex;
    if (text == "ByPolygon") return MappingMode::ByPolygon;
    if (text == "AllSame") return MappingMode::AllSame;
    return MappingMode::Unknown;
}

ReferenceMode ReadReferenceMode(const FbxNode& layer) {
    const FbxNode* node = layer.Find("ReferenceInformationType");
    if (node == nullptr || node->properties.empty()) {
        return ReferenceMode::Direct;
    }
    const std::string& text = node->properties[0].text;
    if (text == "Direct") return ReferenceMode::Direct;
    if (text == "IndexToDirect" || text == "Index") return ReferenceMode::IndexToDirect;
    return ReferenceMode::Unknown;
}

const std::vector<double>* FindDoubleArray(const FbxNode& layer, const char* name) {
    const FbxNode* node = layer.Find(name);
    if (node == nullptr || node->properties.empty() || node->properties[0].doubles.empty()) {
        return nullptr;
    }
    return &node->properties[0].doubles;
}

const std::vector<int64_t>* FindIntArray(const FbxNode& layer, const char* name) {
    const FbxNode* node = layer.Find(name);
    if (node == nullptr || node->properties.empty() || node->properties[0].ints.empty()) {
        return nullptr;
    }
    return &node->properties[0].ints;
}

// Resolves which entry of a layer array a given corner should read, folding the
// mapping and reference modes into one index. Returns -1 when the layer cannot
// answer for this corner.
int64_t ResolveLayerIndex(MappingMode mapping,
                          ReferenceMode reference,
                          const std::vector<int64_t>* indices,
                          int64_t controlPoint,
                          int64_t cornerIndex,
                          int64_t polygonIndex) {
    int64_t index = -1;
    switch (mapping) {
    case MappingMode::ByControlPoint:  index = controlPoint; break;
    case MappingMode::ByPolygonVertex: index = cornerIndex; break;
    case MappingMode::ByPolygon:       index = polygonIndex; break;
    case MappingMode::AllSame:         index = 0; break;
    default: return -1;
    }

    if (reference == ReferenceMode::IndexToDirect) {
        if (indices == nullptr || index < 0 || static_cast<size_t>(index) >= indices->size()) {
            return -1;
        }
        index = (*indices)[static_cast<size_t>(index)];
    }
    return index;
}

std::string ToLowerAscii(const std::string& value) {
    std::string lowered = value;
    for (char& c : lowered) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return lowered;
}

// Exporters disagree wildly about what to call each map: FBX's own
// "DiffuseColor", Maya's "Maya|baseColor", Blender's "Base Color", and so on.
// Matching on substrings covers far more of them than an exact table would.
FbxTextureSlot ClassifyTextureSlot(const std::string& propertyName) {
    const std::string name = ToLowerAscii(propertyName);
    auto has = [&name](const char* needle) { return name.find(needle) != std::string::npos; };

    // Normal and bump first: "normalcamera" also contains nothing else, but
    // "bump" can appear alongside colour words in some exporters.
    if (has("normal") || has("bump")) {
        return FbxTextureSlot::Normal;
    }
    if (has("metal")) {
        return FbxTextureSlot::Metallic;
    }
    if (has("rough") || has("shininess") || has("specular") || has("gloss")) {
        return FbxTextureSlot::Roughness;
    }
    if (has("diffuse") || has("basecolor") || has("base color") || has("albedo") || has("color")) {
        return FbxTextureSlot::Albedo;
    }
    return FbxTextureSlot::Unknown;
}

// Reads RelativeFilename and FileName off a Texture node.
void ReadTexturePaths(const FbxNode& texture, std::string& outRelative, std::string& outAbsolute) {
    if (const FbxNode* relative = texture.Find("RelativeFilename")) {
        if (!relative->properties.empty()) {
            outRelative = relative->properties[0].text;
        }
    }
    if (const FbxNode* absolute = texture.Find("FileName")) {
        if (!absolute->properties.empty()) {
            outAbsolute = absolute->properties[0].text;
        }
    }
}

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
        CloseHandle(file);
        return false;
    }

    out.resize(static_cast<size_t>(size.QuadPart));
    size_t written = 0;
    bool ok = true;
    while (written < out.size()) {
        const DWORD block = static_cast<DWORD>((std::min)(out.size() - written, static_cast<size_t>(32u << 20)));
        DWORD read = 0;
        if (!ReadFile(file, out.data() + written, block, &read, nullptr) || read != block) {
            ok = false;
            break;
        }
        written += read;
    }
    CloseHandle(file);
    return ok;
}

}

namespace {

const char* MappingModeName(MappingMode mode) {
    switch (mode) {
    case MappingMode::ByControlPoint:  return "ByControlPoint";
    case MappingMode::ByPolygonVertex: return "ByPolygonVertex";
    case MappingMode::ByPolygon:       return "ByPolygon";
    case MappingMode::AllSame:         return "AllSame";
    default:                           return "UNKNOWN";
    }
}

const char* ReferenceModeName(ReferenceMode mode) {
    switch (mode) {
    case ReferenceMode::Direct:        return "Direct";
    case ReferenceMode::IndexToDirect: return "IndexToDirect";
    default:                           return "UNKNOWN";
    }
}

void DescribeLayer(std::ostringstream& out, const FbxNode& geometry, const char* layerName,
                   const char* valueName, const char* indexName, size_t valuesPerEntry) {
    size_t layerCount = 0;
    for (const FbxNode& child : geometry.children) {
        if (child.name != layerName) {
            continue;
        }
        ++layerCount;
        const std::vector<double>* values = FindDoubleArray(child, valueName);
        const std::vector<int64_t>* indices = FindIntArray(child, indexName);
        out << "      " << layerName << " #" << layerCount
            << "  mapping=" << MappingModeName(ReadMappingMode(child))
            << " reference=" << ReferenceModeName(ReadReferenceMode(child))
            << " values=" << (values ? values->size() / valuesPerEntry : 0)
            << " indices=" << (indices ? indices->size() : 0) << "\n";
    }
    if (layerCount == 0) {
        out << "      " << layerName << ": none\n";
    } else if (layerCount > 1) {
        out << "      NOTE: " << layerCount << " " << layerName
            << " layers present; the reader only uses the first.\n";
    }
}

// Everything in a Model's Properties70 that affects where its geometry lands.
void DescribeModelTransform(std::ostringstream& out, const FbxNode& model) {
    const char* wanted[] = {
        "Lcl Translation", "Lcl Rotation", "Lcl Scaling", "PreRotation", "PostRotation",
        "RotationOffset", "RotationPivot", "ScalingOffset", "ScalingPivot",
        "GeometricTranslation", "GeometricRotation", "GeometricScaling",
    };
    for (const char* name : wanted) {
        const FbxProperty* x = FindProperty70(model, name, 0);
        if (x == nullptr) {
            continue;
        }
        const FbxProperty* y = FindProperty70(model, name, 1);
        const FbxProperty* z = FindProperty70(model, name, 2);
        out << "      " << name << " = ("
            << x->scalarDouble << ", "
            << (y ? y->scalarDouble : 0.0) << ", "
            << (z ? z->scalarDouble : 0.0) << ")";
        // Flag the ones the reader currently ignores.
        const std::string label(name);
        if (label == "RotationOffset" || label == "RotationPivot" ||
            label == "ScalingOffset" || label == "ScalingPivot" || label == "PostRotation") {
            out << "   << NOT APPLIED by the reader";
        }
        out << "\n";
    }
    if (const FbxProperty* order = FindProperty70(model, "RotationOrder", 0)) {
        out << "      RotationOrder = " << order->scalarInt;
        if (order->scalarInt != 0) {
            out << "   << reader assumes XYZ (0)";
        }
        out << "\n";
    }
}

}

std::string DescribeFbxFile(const std::wstring& path) {
    std::ostringstream out;

    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes)) {
        return "Could not read the file.\n";
    }
    if (bytes.size() < kHeaderLength || std::memcmp(bytes.data(), kBinaryMagic, kMagicLength) != 0) {
        return "Not a binary FBX file (ASCII FBX is a different encoding).\n";
    }

    FbxReader reader(bytes.data(), bytes.size());
    std::string error;
    if (!reader.ParseHeader(&error)) {
        return "Header parse failed: " + error + "\n";
    }

    std::vector<FbxNode> roots;
    if (!reader.ParseNodes(roots, &error)) {
        out << "Node parse FAILED: " << error << "\n";
        return out.str();
    }

    out << "FBX version " << reader.Version()
        << " (" << (reader.Version() >= 7500 ? "64-bit" : "32-bit") << " record offsets), "
        << bytes.size() << " bytes\n";

    out << "top-level nodes:";
    for (const FbxNode& node : roots) {
        out << " " << node.name;
    }
    out << "\n";

    const FbxNode* objects = nullptr;
    const FbxNode* connections = nullptr;
    const FbxNode* globals = nullptr;
    for (const FbxNode& node : roots) {
        if (node.name == "Objects") objects = &node;
        else if (node.name == "Connections") connections = &node;
        else if (node.name == "GlobalSettings") globals = &node;
    }

    out << "\nGlobalSettings\n";
    if (globals == nullptr) {
        out << "   (absent)\n";
    } else {
        const char* keys[] = {"UpAxis", "UpAxisSign", "FrontAxis", "FrontAxisSign",
                              "CoordAxis", "CoordAxisSign", "UnitScaleFactor", "OriginalUnitScaleFactor"};
        for (const char* key : keys) {
            if (const FbxProperty* value = FindProperty70(*globals, key, 0)) {
                out << "   " << key << " = "
                    << (value->type == 'D' || value->type == 'F' ? value->scalarDouble
                                                                 : static_cast<double>(value->scalarInt))
                    << "\n";
            }
        }
    }

    if (objects == nullptr) {
        out << "\nNo Objects section.\n";
        return out.str();
    }

    // ---- object census ----------------------------------------------------
    std::map<std::string, size_t> census;
    for (const FbxNode& object : objects->children) {
        ++census[object.name];
    }
    out << "\nObjects (" << objects->children.size() << " total)\n";
    for (const auto& entry : census) {
        out << "   " << entry.first << " x" << entry.second << "\n";
    }

    // ---- geometry ---------------------------------------------------------
    size_t geometryIndex = 0;
    for (const FbxNode& object : objects->children) {
        if (object.name != "Geometry") {
            continue;
        }
        const std::string subtype = object.properties.size() > 2 ? object.properties[2].text : std::string();
        const std::string name = object.properties.size() > 1 ? object.properties[1].text : std::string();
        out << "\nGeometry #" << geometryIndex++ << "  id="
            << (object.properties.empty() ? 0 : object.properties[0].scalarInt)
            << "  subtype='" << subtype << "'\n";
        out << "      name='" << name.substr(0, name.find('\0')) << "'\n";

        if (subtype != "Mesh" && !subtype.empty()) {
            out << "      SKIPPED by the reader: subtype is not Mesh\n";
            continue;
        }

        const FbxNode* verticesNode = object.Find("Vertices");
        const FbxNode* indicesNode = object.Find("PolygonVertexIndex");
        if (verticesNode == nullptr || indicesNode == nullptr ||
            verticesNode->properties.empty() || indicesNode->properties.empty()) {
            out << "      MISSING Vertices or PolygonVertexIndex - this mesh is skipped\n";
            continue;
        }

        const std::vector<double>& points = verticesNode->properties[0].doubles;
        const std::vector<int64_t>& polygons = indicesNode->properties[0].ints;
        out << "      Vertices: " << points.size() / 3 << " control points (array type '"
            << verticesNode->properties[0].type << "')\n";
        out << "      PolygonVertexIndex: " << polygons.size() << " entries (array type '"
            << indicesNode->properties[0].type << "')\n";

        // Polygon size histogram, plus any index that points outside the
        // control point array - the classic cause of exploded geometry.
        std::map<size_t, size_t> sizes;
        size_t current = 0;
        size_t outOfRange = 0;
        size_t polygonCount = 0;
        int64_t minIndex = 0;
        int64_t maxIndex = 0;
        bool first = true;
        for (int64_t raw : polygons) {
            int64_t value = raw < 0 ? ~raw : raw;
            if (first) { minIndex = maxIndex = value; first = false; }
            minIndex = (std::min)(minIndex, value);
            maxIndex = (std::max)(maxIndex, value);
            if (value < 0 || static_cast<size_t>(value) >= points.size() / 3) {
                ++outOfRange;
            }
            ++current;
            if (raw < 0) {
                ++sizes[current];
                current = 0;
                ++polygonCount;
            }
        }
        out << "      polygons: " << polygonCount << ", index range [" << minIndex << ", " << maxIndex << "]\n";
        if (current != 0) {
            out << "      WARNING: " << current << " trailing indices with no polygon terminator\n";
        }
        if (outOfRange != 0) {
            out << "      WARNING: " << outOfRange << " indices point outside the control point array\n";
        }
        out << "      polygon sizes:";
        for (const auto& entry : sizes) {
            out << " " << entry.first << "-gon x" << entry.second;
        }
        out << "\n";

        DescribeLayer(out, object, "LayerElementNormal", "Normals", "NormalsIndex", 3);
        DescribeLayer(out, object, "LayerElementUV", "UV", "UVIndex", 2);
        DescribeLayer(out, object, "LayerElementColor", "Colors", "ColorIndex", 4);
        DescribeLayer(out, object, "LayerElementMaterial", "Materials", "Materials", 1);
    }

    // ---- models -----------------------------------------------------------
    size_t modelIndex = 0;
    for (const FbxNode& object : objects->children) {
        if (object.name != "Model") {
            continue;
        }
        const std::string name = object.properties.size() > 1 ? object.properties[1].text : std::string();
        out << "\nModel #" << modelIndex++ << "  id="
            << (object.properties.empty() ? 0 : object.properties[0].scalarInt)
            << "  name='" << name.substr(0, name.find('\0')) << "'"
            << "  subtype='" << (object.properties.size() > 2 ? object.properties[2].text : std::string()) << "'\n";
        DescribeModelTransform(out, object);
    }

    // ---- connections ------------------------------------------------------
    if (connections != nullptr) {
        std::map<std::string, size_t> kinds;
        for (const FbxNode& connection : connections->children) {
            if (!connection.properties.empty()) {
                ++kinds[connection.properties[0].text];
            }
        }
        out << "\nConnections (" << connections->children.size() << ")\n";
        for (const auto& entry : kinds) {
            out << "   " << entry.first << " x" << entry.second << "\n";
        }
    }

    // ---- what the reader actually produces --------------------------------
    RawObjData raw;
    FbxSceneInfo info;
    std::string parseError;
    out << "\nreader result\n";
    if (!ParseFbxFile(path, raw, &info, &parseError)) {
        out << "   FAILED: " << parseError << "\n";
        return out.str();
    }

    float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (size_t i = 0; i + 2 < raw.Positions.size(); i += 3) {
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = (std::min)(lo[axis], raw.Positions[i + axis]);
            hi[axis] = (std::max)(hi[axis], raw.Positions[i + axis]);
        }
    }
    out << "   meshes merged: " << info.MeshCount << "\n";
    out << "   positions: " << raw.Positions.size() / 3
        << ", normals: " << raw.Normals.size() / 3
        << ", uvs: " << raw.Texcoords.size() / 2
        << ", triangles: " << raw.TriangleCount() << "\n";
    out << "   bounds: (" << lo[0] << ", " << lo[1] << ", " << lo[2] << ") .. ("
        << hi[0] << ", " << hi[1] << ", " << hi[2] << ")\n";
    out << "   size: " << (hi[0] - lo[0]) << " x " << (hi[1] - lo[1]) << " x " << (hi[2] - lo[2]) << "\n";
    return out.str();
}

bool IsBinaryFbxFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    char header[kMagicLength] = {};
    DWORD read = 0;
    const bool ok = ReadFile(file, header, static_cast<DWORD>(kMagicLength), &read, nullptr) &&
                    read == kMagicLength &&
                    std::memcmp(header, kBinaryMagic, kMagicLength) == 0;
    CloseHandle(file);
    return ok;
}

bool ParseFbxFile(const std::wstring& path,
                  RawObjData& out,
                  FbxSceneInfo* outInfo,
                  std::string* outError) {
    out = RawObjData();
    FbxSceneInfo info;

    auto fail = [&](const std::string& message) {
        if (outError != nullptr) {
            *outError = message;
        }
        return false;
    };

    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes)) {
        return fail("Could not read the FBX file.");
    }

    if (bytes.size() < kHeaderLength || std::memcmp(bytes.data(), kBinaryMagic, kMagicLength) != 0) {
        return fail("This is not a binary FBX file. ASCII FBX is a different encoding and is not supported - "
                    "re-export from your tool with the binary option selected.");
    }

    FbxReader reader(bytes.data(), bytes.size());
    if (!reader.ParseHeader(outError)) {
        return false;
    }
    info.Version = reader.Version();

    std::vector<FbxNode> roots;
    if (!reader.ParseNodes(roots, outError)) {
        return false;
    }

    const FbxNode* objects = nullptr;
    const FbxNode* connections = nullptr;
    const FbxNode* globalSettings = nullptr;
    for (const FbxNode& node : roots) {
        if (node.name == "Objects") objects = &node;
        else if (node.name == "Connections") connections = &node;
        else if (node.name == "GlobalSettings") globalSettings = &node;
    }

    if (globalSettings != nullptr) {
        if (const FbxProperty* unit = FindProperty70(*globalSettings, "UnitScaleFactor", 0)) {
            info.UnitScaleFactor = unit->scalarDouble;
        }
        if (const FbxProperty* upAxis = FindProperty70(*globalSettings, "UpAxis", 0)) {
            info.UpAxis = static_cast<int>(upAxis->scalarInt);
        }
        if (const FbxProperty* upSign = FindProperty70(*globalSettings, "UpAxisSign", 0)) {
            info.UpAxisSign = static_cast<int>(upSign->scalarInt);
        }
    }

    if (objects == nullptr) {
        return fail("The FBX file has no Objects section.");
    }

    // ---- gather geometries and models ------------------------------------
    std::unordered_map<int64_t, const FbxNode*> geometries;
    std::unordered_map<int64_t, ModelNode> models;

    for (const FbxNode& object : objects->children) {
        if (object.properties.empty()) {
            continue;
        }
        const int64_t id = object.properties[0].scalarInt;

        if (object.name == "Geometry") {
            // Blend shapes also arrive as Geometry; only meshes carry polygons.
            const bool isMesh = object.properties.size() < 3 || object.properties[2].text == "Mesh";
            if (isMesh) {
                geometries.emplace(id, &object);
            }
        } else if (object.name == "Model") {
            ModelNode model;
            model.local = BuildLocalMatrix(object);
            model.geometric = BuildGeometricMatrix(object);
            models.emplace(id, model);
        }
    }

    // ---- connections ------------------------------------------------------
    std::unordered_map<int64_t, int64_t> geometryToModel;
    if (connections != nullptr) {
        for (const FbxNode& connection : connections->children) {
            if (connection.name != "C" || connection.properties.size() < 3) {
                continue;
            }
            if (connection.properties[0].text != "OO") {
                continue;   // object-to-property links do not affect the hierarchy
            }
            const int64_t child = connection.properties[1].scalarInt;
            const int64_t parent = connection.properties[2].scalarInt;

            if (geometries.find(child) != geometries.end()) {
                geometryToModel[child] = parent;
            } else {
                auto model = models.find(child);
                if (model != models.end()) {
                    model->second.parent = parent;
                }
            }
        }
    }

    // ---- world transforms -------------------------------------------------
    // Walked iteratively with a visit cap, so a file whose connections form a
    // cycle cannot send this into infinite recursion.
    std::function<XMMATRIX(int64_t, int)> worldOf = [&](int64_t id, int depth) -> XMMATRIX {
        auto entry = models.find(id);
        if (entry == models.end() || depth > 256) {
            return XMMatrixIdentity();
        }
        if (entry->second.resolved) {
            return entry->second.world;
        }
        // Marked before recursing so a cycle resolves to identity rather than
        // looping.
        entry->second.resolved = true;
        entry->second.world = entry->second.local * worldOf(entry->second.parent, depth + 1);
        return entry->second.world;
    };

    // ---- flatten every mesh ----------------------------------------------
    for (const auto& entry : geometries) {
        const FbxNode& geometry = *entry.second;

        const FbxNode* verticesNode = geometry.Find("Vertices");
        const FbxNode* indicesNode = geometry.Find("PolygonVertexIndex");
        if (verticesNode == nullptr || indicesNode == nullptr ||
            verticesNode->properties.empty() || indicesNode->properties.empty()) {
            continue;
        }

        const std::vector<double>& controlPoints = verticesNode->properties[0].doubles;
        const std::vector<int64_t>& polygonIndices = indicesNode->properties[0].ints;
        if (controlPoints.size() < 3 || polygonIndices.size() < 3) {
            continue;
        }

        XMMATRIX world = XMMatrixIdentity();
        auto modelLink = geometryToModel.find(entry.first);
        if (modelLink != geometryToModel.end()) {
            auto model = models.find(modelLink->second);
            if (model != models.end()) {
                world = model->second.geometric * worldOf(modelLink->second, 0);
            }
        }

        // Positions are appended for this mesh, and corner indices are offset
        // so several meshes can share one array.
        const size_t positionBase = out.Positions.size() / 3;
        const size_t controlPointCount = controlPoints.size() / 3;
        out.Positions.reserve(out.Positions.size() + controlPoints.size());
        for (size_t i = 0; i < controlPointCount; ++i) {
            XMVECTOR position = XMVectorSet(static_cast<float>(controlPoints[i * 3 + 0]),
                                            static_cast<float>(controlPoints[i * 3 + 1]),
                                            static_cast<float>(controlPoints[i * 3 + 2]),
                                            1.0f);
            position = XMVector3TransformCoord(position, world);
            XMFLOAT3 stored;
            XMStoreFloat3(&stored, position);
            out.Positions.push_back(stored.x);
            out.Positions.push_back(stored.y);
            out.Positions.push_back(stored.z);
        }

        // ---- layer elements -----------------------------------------------
        const FbxNode* normalLayer = geometry.Find("LayerElementNormal");
        const FbxNode* uvLayer = geometry.Find("LayerElementUV");
        const FbxNode* colorLayer = geometry.Find("LayerElementColor");

        const std::vector<double>* normalValues = nullptr;
        const std::vector<int64_t>* normalIndices = nullptr;
        MappingMode normalMapping = MappingMode::Unknown;
        ReferenceMode normalReference = ReferenceMode::Direct;
        if (normalLayer != nullptr) {
            normalValues = FindDoubleArray(*normalLayer, "Normals");
            normalIndices = FindIntArray(*normalLayer, "NormalsIndex");
            normalMapping = ReadMappingMode(*normalLayer);
            normalReference = ReadReferenceMode(*normalLayer);
        }

        const std::vector<double>* uvValues = nullptr;
        const std::vector<int64_t>* uvIndices = nullptr;
        MappingMode uvMapping = MappingMode::Unknown;
        ReferenceMode uvReference = ReferenceMode::Direct;
        if (uvLayer != nullptr) {
            uvValues = FindDoubleArray(*uvLayer, "UV");
            uvIndices = FindIntArray(*uvLayer, "UVIndex");
            uvMapping = ReadMappingMode(*uvLayer);
            uvReference = ReadReferenceMode(*uvLayer);
        }

        // Colours are only read when they sit on control points, because
        // RawObjData carries one colour per position. Anything per-corner is
        // reported as dropped rather than scattered into the wrong slots.
        if (colorLayer != nullptr) {
            info.HadVertexColors = true;
            const std::vector<double>* colorValues = FindDoubleArray(*colorLayer, "Colors");
            const MappingMode colorMapping = ReadMappingMode(*colorLayer);
            const ReferenceMode colorReference = ReadReferenceMode(*colorLayer);
            const std::vector<int64_t>* colorIndices = FindIntArray(*colorLayer, "ColorIndex");

            if (colorValues != nullptr && colorMapping == MappingMode::ByControlPoint) {
                out.Colors.resize(out.Positions.size(), 1.0f);
                for (size_t i = 0; i < controlPointCount; ++i) {
                    const int64_t index = ResolveLayerIndex(colorMapping, colorReference, colorIndices,
                                                            static_cast<int64_t>(i), 0, 0);
                    if (index < 0 || static_cast<size_t>(index) * 4 + 2 >= colorValues->size()) {
                        continue;
                    }
                    const size_t target = (positionBase + i) * 3;
                    out.Colors[target + 0] = static_cast<float>((*colorValues)[index * 4 + 0]);
                    out.Colors[target + 1] = static_cast<float>((*colorValues)[index * 4 + 1]);
                    out.Colors[target + 2] = static_cast<float>((*colorValues)[index * 4 + 2]);
                }
            } else {
                info.DroppedVertexColors = true;
            }
        }

        // ---- polygons ------------------------------------------------------
        // PolygonVertexIndex marks the last corner of each polygon by storing
        // its bitwise complement, which is how an n-gon list is delimited.
        std::vector<ObjCorner> polygon;
        polygon.reserve(16);
        int64_t polygonIndex = 0;

        for (size_t corner = 0; corner < polygonIndices.size(); ++corner) {
            int64_t controlPoint = polygonIndices[corner];
            bool lastOfPolygon = false;
            if (controlPoint < 0) {
                controlPoint = ~controlPoint;
                lastOfPolygon = true;
            }

            if (controlPoint >= 0 && static_cast<size_t>(controlPoint) < controlPointCount) {
                ObjCorner entryCorner;
                entryCorner.position = static_cast<int32_t>(positionBase + controlPoint);
                entryCorner.normal = -1;
                entryCorner.texcoord = -1;

                if (normalValues != nullptr) {
                    const int64_t index = ResolveLayerIndex(normalMapping, normalReference, normalIndices,
                                                            controlPoint, static_cast<int64_t>(corner), polygonIndex);
                    if (index >= 0 && static_cast<size_t>(index) * 3 + 2 < normalValues->size()) {
                        entryCorner.normal = static_cast<int32_t>(out.Normals.size() / 3);
                        out.Normals.push_back(static_cast<float>((*normalValues)[index * 3 + 0]));
                        out.Normals.push_back(static_cast<float>((*normalValues)[index * 3 + 1]));
                        out.Normals.push_back(static_cast<float>((*normalValues)[index * 3 + 2]));
                    }
                }

                if (uvValues != nullptr) {
                    const int64_t index = ResolveLayerIndex(uvMapping, uvReference, uvIndices,
                                                            controlPoint, static_cast<int64_t>(corner), polygonIndex);
                    if (index >= 0 && static_cast<size_t>(index) * 2 + 1 < uvValues->size()) {
                        entryCorner.texcoord = static_cast<int32_t>(out.Texcoords.size() / 2);
                        out.Texcoords.push_back(static_cast<float>((*uvValues)[index * 2 + 0]));
                        out.Texcoords.push_back(static_cast<float>((*uvValues)[index * 2 + 1]));
                    }
                }

                polygon.push_back(entryCorner);
            }

            if (!lastOfPolygon) {
                continue;
            }

            if (polygon.size() >= 3) {
                const size_t triangles = polygon.size() - 2;
                const size_t base = out.Corners.size();
                out.Corners.resize(base + triangles * 3);
                ObjCorner* target = out.Corners.data() + base;

                bool fanned = true;
                if (polygon.size() >= 4) {
                    fanned = !TriangulatePolygon(polygon, out.Positions, out.Positions.size() / 3, target);
                }
                if (fanned) {
                    for (size_t apex = 1; apex + 1 < polygon.size(); ++apex) {
                        *target++ = polygon[0];
                        *target++ = polygon[apex];
                        *target++ = polygon[apex + 1];
                    }
                }
            }

            polygon.clear();
            ++polygonIndex;
        }

        ++info.MeshCount;
    }

    if (out.Corners.empty()) {
        return fail("The FBX file contains no triangles.");
    }

    // A colour array has to cover every position or the mesh builder ignores it.
    if (!out.Colors.empty() && out.Colors.size() != out.Positions.size()) {
        out.Colors.resize(out.Positions.size(), 1.0f);
    }

    // ---- materials and textures -------------------------------------------
    // Texture nodes hang off materials through OP connections whose fourth
    // property names the slot. Materials in turn hang off models, but for a
    // merged import every texture in the file is a candidate, so the material
    // link only matters for naming the slot.
    {
        std::unordered_map<int64_t, const FbxNode*> textureNodes;
        std::unordered_map<int64_t, const FbxNode*> materialNodes;
        for (const FbxNode& object : objects->children) {
            if (object.properties.empty()) {
                continue;
            }
            const int64_t id = object.properties[0].scalarInt;
            if (object.name == "Texture") {
                textureNodes.emplace(id, &object);
            } else if (object.name == "Material") {
                materialNodes.emplace(id, &object);
            }
        }

        std::vector<int64_t> claimed;
        if (connections != nullptr) {
            for (const FbxNode& connection : connections->children) {
                if (connection.name != "C" || connection.properties.size() < 4) {
                    continue;
                }
                if (connection.properties[0].text != "OP") {
                    continue;
                }
                const int64_t child = connection.properties[1].scalarInt;
                const int64_t parent = connection.properties[2].scalarInt;
                const std::string propertyName = connection.properties[3].text;

                auto texture = textureNodes.find(child);
                if (texture == textureNodes.end()) {
                    continue;
                }
                // Only textures bound to a material describe a surface; ones
                // bound elsewhere are for things we do not import.
                if (materialNodes.find(parent) == materialNodes.end()) {
                    continue;
                }

                FbxTextureReference reference;
                reference.Slot = ClassifyTextureSlot(propertyName);
                reference.PropertyName = propertyName;
                ReadTexturePaths(*texture->second, reference.RelativePath, reference.AbsolutePath);
                if (!reference.RelativePath.empty() || !reference.AbsolutePath.empty()) {
                    info.Textures.push_back(reference);
                    claimed.push_back(child);
                }
            }
        }

        // A file can carry textures with no OP connection at all - some
        // exporters only link them through the material's Properties70. Those
        // still tell the artist what the model expects, so they come through
        // unclassified rather than being dropped.
        for (const auto& entry : textureNodes) {
            if (std::find(claimed.begin(), claimed.end(), entry.first) != claimed.end()) {
                continue;
            }
            FbxTextureReference reference;
            reference.Slot = FbxTextureSlot::Unknown;
            ReadTexturePaths(*entry.second, reference.RelativePath, reference.AbsolutePath);
            if (!reference.RelativePath.empty() || !reference.AbsolutePath.empty()) {
                info.Textures.push_back(reference);
            }
        }
    }

    if (outInfo != nullptr) {
        *outInfo = info;
    }
    return true;
}

}
