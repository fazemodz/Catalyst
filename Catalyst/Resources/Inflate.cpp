#include "Inflate.h"

#include <array>
#include <cstring>

namespace CatalystImport {
namespace {

// RFC 1951 section 3.2.5. Base lengths and the extra bits that follow each.
constexpr uint16_t kLengthBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
constexpr uint8_t kLengthExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
constexpr uint16_t kDistanceBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
constexpr uint8_t kDistanceExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

// The order the code-length code lengths arrive in for a dynamic block.
constexpr uint8_t kCodeLengthOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

// A canonical Huffman decoder held as counts-per-length plus the symbols in
// code order, which is all the information a canonical code needs.
struct HuffmanTable {
    std::array<uint16_t, 16> counts{};
    std::vector<uint16_t> symbols;

    bool Build(const uint8_t* lengths, size_t count) {
        counts.fill(0);
        for (size_t i = 0; i < count; ++i) {
            ++counts[lengths[i]];
        }
        // Length zero means "symbol unused", so it never takes part.
        counts[0] = 0;

        // Reject a code that is over-subscribed. An incomplete code is allowed:
        // a block can legitimately define a single distance symbol.
        int left = 1;
        for (int length = 1; length < 16; ++length) {
            left <<= 1;
            left -= counts[length];
            if (left < 0) {
                return false;
            }
        }

        std::array<uint16_t, 16> offsets{};
        offsets[1] = 0;
        for (int length = 1; length < 15; ++length) {
            offsets[length + 1] = static_cast<uint16_t>(offsets[length] + counts[length]);
        }

        symbols.assign(count, 0);
        for (size_t i = 0; i < count; ++i) {
            if (lengths[i] != 0) {
                symbols[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
            }
        }
        return true;
    }
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    // Returns -1 once the stream is exhausted, which every caller checks; a
    // truncated stream must fail rather than decode rubbish.
    int ReadBit() {
        if (m_bitCount == 0) {
            if (m_position >= m_size) {
                m_overrun = true;
                return -1;
            }
            m_bitBuffer = m_data[m_position++];
            m_bitCount = 8;
        }
        const int bit = m_bitBuffer & 1;
        m_bitBuffer >>= 1;
        --m_bitCount;
        return bit;
    }

    int ReadBits(int count) {
        int value = 0;
        for (int i = 0; i < count; ++i) {
            const int bit = ReadBit();
            if (bit < 0) {
                return -1;
            }
            value |= bit << i;
        }
        return value;
    }

    void AlignToByte() {
        m_bitBuffer = 0;
        m_bitCount = 0;
    }

    bool ReadRaw(uint8_t* destination, size_t count) {
        if (m_position + count > m_size) {
            m_overrun = true;
            return false;
        }
        std::memcpy(destination, m_data + m_position, count);
        m_position += count;
        return true;
    }

    bool Overran() const { return m_overrun; }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_position = 0;
    uint32_t m_bitBuffer = 0;
    int m_bitCount = 0;
    bool m_overrun = false;
};

// Walks the canonical code one bit at a time, as described in RFC 1951. Codes
// are at most 15 bits, so this terminates.
int DecodeSymbol(BitReader& reader, const HuffmanTable& table) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int length = 1; length < 16; ++length) {
        const int bit = reader.ReadBit();
        if (bit < 0) {
            return -1;
        }
        code |= bit;
        const int count = table.counts[length];
        if (code - first < count) {
            return table.symbols[static_cast<size_t>(index) + static_cast<size_t>(code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

void BuildFixedTables(HuffmanTable& literals, HuffmanTable& distances) {
    uint8_t literalLengths[288];
    for (int i = 0; i < 144; ++i) literalLengths[i] = 8;
    for (int i = 144; i < 256; ++i) literalLengths[i] = 9;
    for (int i = 256; i < 280; ++i) literalLengths[i] = 7;
    for (int i = 280; i < 288; ++i) literalLengths[i] = 8;
    literals.Build(literalLengths, 288);

    uint8_t distanceLengths[30];
    for (int i = 0; i < 30; ++i) distanceLengths[i] = 5;
    distances.Build(distanceLengths, 30);
}

bool ReadDynamicTables(BitReader& reader, HuffmanTable& literals, HuffmanTable& distances) {
    const int literalCount = reader.ReadBits(5);
    const int distanceCount = reader.ReadBits(5);
    const int codeLengthCount = reader.ReadBits(4);
    if (literalCount < 0 || distanceCount < 0 || codeLengthCount < 0) {
        return false;
    }

    const int totalLiterals = literalCount + 257;
    const int totalDistances = distanceCount + 1;
    const int totalCodeLengths = codeLengthCount + 4;
    if (totalLiterals > 288 || totalDistances > 30) {
        return false;
    }

    uint8_t codeLengthLengths[19] = {};
    for (int i = 0; i < totalCodeLengths; ++i) {
        const int value = reader.ReadBits(3);
        if (value < 0) {
            return false;
        }
        codeLengthLengths[kCodeLengthOrder[i]] = static_cast<uint8_t>(value);
    }

    HuffmanTable codeLengths;
    if (!codeLengths.Build(codeLengthLengths, 19)) {
        return false;
    }

    // The two alphabets are run-length encoded together, and a repeat can carry
    // across the boundary between them.
    std::vector<uint8_t> lengths(static_cast<size_t>(totalLiterals) + totalDistances, 0);
    size_t written = 0;
    while (written < lengths.size()) {
        const int symbol = DecodeSymbol(reader, codeLengths);
        if (symbol < 0) {
            return false;
        }

        if (symbol < 16) {
            lengths[written++] = static_cast<uint8_t>(symbol);
            continue;
        }

        int repeat = 0;
        uint8_t value = 0;
        if (symbol == 16) {
            if (written == 0) {
                return false;   // nothing to copy
            }
            value = lengths[written - 1];
            const int extra = reader.ReadBits(2);
            if (extra < 0) {
                return false;
            }
            repeat = 3 + extra;
        } else if (symbol == 17) {
            const int extra = reader.ReadBits(3);
            if (extra < 0) {
                return false;
            }
            repeat = 3 + extra;
        } else {
            const int extra = reader.ReadBits(7);
            if (extra < 0) {
                return false;
            }
            repeat = 11 + extra;
        }

        if (written + repeat > lengths.size()) {
            return false;
        }
        for (int i = 0; i < repeat; ++i) {
            lengths[written++] = value;
        }
    }

    return literals.Build(lengths.data(), static_cast<size_t>(totalLiterals)) &&
           distances.Build(lengths.data() + totalLiterals, static_cast<size_t>(totalDistances));
}

}

bool Inflate(const uint8_t* data,
             size_t size,
             size_t expectedSize,
             std::vector<uint8_t>& out,
             std::string* outError) {
    auto fail = [&](const char* message) {
        if (outError != nullptr) {
            *outError = message;
        }
        out.clear();
        return false;
    };

    if (data == nullptr || size == 0) {
        return fail("The compressed stream is empty.");
    }

    size_t offset = 0;
    // Sniff the zlib wrapper: low nibble 8 is the deflate method, and the two
    // header bytes together are a multiple of 31. A raw deflate stream almost
    // never satisfies both, and FBX writes the wrapper anyway.
    if (size >= 2) {
        const uint8_t cmf = data[0];
        const uint8_t flg = data[1];
        if ((cmf & 0x0F) == 8 && ((static_cast<uint32_t>(cmf) << 8) | flg) % 31 == 0) {
            if ((flg & 0x20) != 0) {
                return fail("Preset dictionaries are not supported.");
            }
            offset = 2;
        }
    }

    out.clear();
    out.reserve(expectedSize);

    BitReader reader(data + offset, size - offset);
    HuffmanTable literals;
    HuffmanTable distances;

    bool finalBlock = false;
    while (!finalBlock) {
        const int last = reader.ReadBit();
        const int type = reader.ReadBits(2);
        if (last < 0 || type < 0) {
            return fail("The compressed stream ended in the middle of a block header.");
        }
        finalBlock = (last != 0);

        if (type == 0) {
            reader.AlignToByte();
            uint8_t header[4];
            if (!reader.ReadRaw(header, 4)) {
                return fail("A stored block is truncated.");
            }
            const uint16_t length = static_cast<uint16_t>(header[0] | (header[1] << 8));
            const uint16_t inverse = static_cast<uint16_t>(header[2] | (header[3] << 8));
            if (static_cast<uint16_t>(~length) != inverse) {
                return fail("A stored block's length check failed.");
            }
            const size_t before = out.size();
            out.resize(before + length);
            if (length > 0 && !reader.ReadRaw(out.data() + before, length)) {
                return fail("A stored block is truncated.");
            }
            continue;
        }

        if (type == 1) {
            BuildFixedTables(literals, distances);
        } else if (type == 2) {
            if (!ReadDynamicTables(reader, literals, distances)) {
                return fail("A dynamic block's Huffman tables are malformed.");
            }
        } else {
            return fail("The compressed stream uses a reserved block type.");
        }

        while (true) {
            const int symbol = DecodeSymbol(reader, literals);
            if (symbol < 0) {
                return fail("The compressed stream ended in the middle of a block.");
            }
            if (symbol < 256) {
                out.push_back(static_cast<uint8_t>(symbol));
                continue;
            }
            if (symbol == 256) {
                break;   // end of block
            }

            const int lengthSymbol = symbol - 257;
            if (lengthSymbol >= 29) {
                return fail("The compressed stream uses an invalid length code.");
            }
            const int lengthExtra = reader.ReadBits(kLengthExtra[lengthSymbol]);
            if (lengthExtra < 0) {
                return fail("The compressed stream ended inside a length code.");
            }
            const size_t copyLength = static_cast<size_t>(kLengthBase[lengthSymbol]) + lengthExtra;

            const int distanceSymbol = DecodeSymbol(reader, distances);
            if (distanceSymbol < 0 || distanceSymbol >= 30) {
                return fail("The compressed stream uses an invalid distance code.");
            }
            const int distanceExtra = reader.ReadBits(kDistanceExtra[distanceSymbol]);
            if (distanceExtra < 0) {
                return fail("The compressed stream ended inside a distance code.");
            }
            const size_t distance = static_cast<size_t>(kDistanceBase[distanceSymbol]) + distanceExtra;

            if (distance > out.size()) {
                return fail("A back reference points before the start of the output.");
            }

            // Copied byte by byte on purpose: runs where distance is shorter
            // than the length are legal and rely on reading bytes this same
            // copy is writing.
            size_t source = out.size() - distance;
            for (size_t i = 0; i < copyLength; ++i) {
                out.push_back(out[source + i]);
            }
        }
    }

    if (reader.Overran()) {
        return fail("The compressed stream is truncated.");
    }
    if (expectedSize != 0 && out.size() != expectedSize) {
        return fail("The decompressed size does not match what the file declared.");
    }
    return true;
}

}
