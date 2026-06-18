#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <string>
#include "common.h"

using namespace std;

// Recursive function to generate Prefix Codes
void generateCodes(const shared_ptr<Node> &root, const string &code, unordered_map<char, string> &huffmanCodes)
{
    if (!root)
        return;

    if (!root->left && !root->right)
    {
        huffmanCodes[root->data] = code;
    }

    generateCodes(root->left, code + "0", huffmanCodes);
    generateCodes(root->right, code + "1", huffmanCodes);
}

// Phase 2 Core: Processes a single 4MB chunk independently
CompressedChunk processChunk(const UncompressedChunk &chunk)
{
    CompressedChunk result;
    result.sequence_id = chunk.sequence_id;

    // 1. Local Frequency Analysis
    unordered_map<char, size_t> freqMap;
    for (char c : chunk.data)
    {
        freqMap[c]++;
    }

    // 2. Build Local Huffman Tree
    priority_queue<shared_ptr<Node>, vector<shared_ptr<Node>>, Compare> pq;
    for (auto pair : freqMap)
    {
        pq.push(make_shared<Node>(pair.first, pair.second));
    }

    // Edge Case: Chunk only has 1 unique character
    if (pq.size() == 1)
    {
        auto left = pq.top();
        pq.pop();
        auto right = make_shared<Node>('\0', 0); // Dummy node
        pq.push(make_shared<Node>(left->freq, left, right));
    }

    shared_ptr<Node> root = nullptr;
    if (!pq.empty())
    {
        while (pq.size() > 1)
        {
            auto left = pq.top();
            pq.pop();
            auto right = pq.top();
            pq.pop();
            size_t sum = left->freq + right->freq;
            pq.push(make_shared<Node>(sum, left, right));
        }
        root = pq.top();
    }

    // 3. Generate Local Codes
    unordered_map<char, string> huffmanCodes;
    if (root)
    {
        generateCodes(root, "", huffmanCodes);
    }

    // 4. Pack Bits into a temporary buffer
    vector<char> packedBits;
    unsigned char bitBuffer = 0;
    int bitCount = 0;

    for (char c : chunk.data)
    {
        string code = huffmanCodes[c];
        for (char bit : code)
        {
            bitBuffer <<= 1;
            if (bit == '1')
                bitBuffer |= 1;
            bitCount++;

            if (bitCount == 8)
            {
                packedBits.push_back(bitBuffer);
                bitBuffer = 0;
                bitCount = 0;
            }
        }
    }
    if (bitCount > 0)
    {
        bitBuffer <<= (8 - bitCount);
        packedBits.push_back(bitBuffer);
    }

    // 5. Serialize Metadata + Data into the payload vector
    // We use a lambda helper to append raw bytes to our vector easily
    auto appendData = [&result](const void *data, size_t size)
    {
        const char *bytes = reinterpret_cast<const char *>(data);
        result.compressed_payload.insert(result.compressed_payload.end(), bytes, bytes + size);
    };

    size_t uncompressedSize = chunk.data.size();
    appendData(&uncompressedSize, sizeof(uncompressedSize));

    size_t mapSize = freqMap.size();
    appendData(&mapSize, sizeof(mapSize));

    for (auto pair : freqMap)
    {
        appendData(&pair.first, sizeof(pair.first));
        appendData(&pair.second, sizeof(pair.second));
    }

    size_t packedSize = packedBits.size();
    appendData(&packedSize, sizeof(packedSize));
    appendData(packedBits.data(), packedSize);

    return result;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cerr << "Usage: " << argv[0] << " <input_file> <output_file.bin>\n";
        return 1;
    }

    string inputFilename = argv[1];
    string outputFilename = argv[2];

    ifstream inFile(inputFilename, ios::binary | ios::ate);
    if (!inFile)
    {
        cerr << "Error: Could not open input file.\n";
        return 1;
    }

    size_t fileSize = inFile.tellg();
    inFile.seekg(0, ios::beg);

    ofstream outFile(outputFilename, ios::binary);
    if (!outFile)
    {
        cerr << "Error: Could not create output file.\n";
        return 1;
    }

    // --- WRITE GLOBAL HEADER ---
    GlobalHeader header;
    copy(begin(MAGIC_NUMBER), end(MAGIC_NUMBER), header.magic);
    header.original_file_size = fileSize;
    header.chunk_size = CHUNK_SIZE;
    outFile.write(reinterpret_cast<const char *>(&header), sizeof(GlobalHeader));

    // --- THE CHUNKING LOOP ---
    size_t seqId = 0;
    while (!inFile.eof())
    {
        UncompressedChunk currentChunk;
        currentChunk.sequence_id = seqId;
        currentChunk.data.resize(CHUNK_SIZE);

        inFile.read(currentChunk.data.data(), CHUNK_SIZE);
        size_t bytesRead = inFile.gcount();

        if (bytesRead < CHUNK_SIZE)
        {
            currentChunk.data.resize(bytesRead);
        }

        if (bytesRead > 0)
        {
            CompressedChunk compressed = processChunk(currentChunk);

            // Write the Sequence ID, then write the entire serialized payload
            outFile.write(reinterpret_cast<const char *>(&compressed.sequence_id), sizeof(compressed.sequence_id));
            outFile.write(compressed.compressed_payload.data(), compressed.compressed_payload.size());

            cout << "Processed Chunk ID: " << seqId << " (" << bytesRead << " bytes uncompressed)\n";
            seqId++;
        }
    }

    inFile.close();
    outFile.close();
    cout << "Success! Partitioned and compressed file into " << seqId << " chunks.\n";
    return 0;
}