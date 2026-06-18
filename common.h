#pragma once
#include <vector>
#include <cstddef>
#include <memory>
#include <algorithm>

// Standard chunk size: 4 Megabytes
constexpr size_t CHUNK_SIZE = 4 * 1024 * 1024;

// A unique signature to identify our custom file type ("MZIP" for Multi-Zip)
const char MAGIC_NUMBER[4] = {'M', 'Z', 'I', 'P'};

// 1. Global Header (Sits at the very top of the .bin file)
struct GlobalHeader
{
    char magic[4];
    size_t original_file_size;
    size_t chunk_size;
};

// 2. Uncompressed Chunk (What the Reader Thread will produce)
struct UncompressedChunk
{
    size_t sequence_id;     // 0, 1, 2, 3... to keep order
    std::vector<char> data; // The raw 4MB of characters
};

// 3. Compressed Chunk (What the Worker Threads will produce)
struct CompressedChunk
{
    size_t sequence_id;
    std::vector<char> compressed_payload; // Contains the local freq map + bit-packed data
};

// 4. Our bulletproof Node structure
struct Node
{
    char data;
    char min_char;
    size_t freq;
    std::shared_ptr<Node> left;
    std::shared_ptr<Node> right;

    Node(char d, size_t f) : data(d), min_char(d), freq(f), left(nullptr), right(nullptr) {}
    Node(size_t f, std::shared_ptr<Node> l, std::shared_ptr<Node> r)
        : data('\0'), freq(f), left(l), right(r)
    {
        min_char = std::min(l->min_char, r->min_char);
    }
};

// 5. Our bulletproof Comparator
struct Compare
{
    bool operator()(const std::shared_ptr<Node> &l, const std::shared_ptr<Node> &r)
    {
        if (l->freq == r->freq)
            return l->min_char > r->min_char;
        return l->freq > r->freq;
    }
};