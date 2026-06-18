#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <string>
#include "common.h"

using namespace std;

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cerr << "Usage: " << argv[0] << " <compressed_file.bin> <output_restored.txt>\n";
        return 1;
    }

    string inputFilename = argv[1];
    string outputFilename = argv[2];

    ifstream inFile(inputFilename, ios::binary);
    if (!inFile)
    {
        cerr << "Error: Could not open compressed file.\n";
        return 1;
    }

    ofstream outFile(outputFilename, ios::binary);
    if (!outFile)
    {
        cerr << "Error: Could not create output file.\n";
        return 1;
    }

    // --- 1. READ GLOBAL HEADER ---
    GlobalHeader header;
    inFile.read(reinterpret_cast<char *>(&header), sizeof(GlobalHeader));

    // Verify Magic Number
    if (string(header.magic, 4) != string(MAGIC_NUMBER, 4))
    {
        cerr << "Error: Not a valid MZIP file!\n";
        return 1;
    }

    size_t totalDecoded = 0;

    // --- 2. CHUNK DECOMPRESSION LOOP ---
    while (totalDecoded < header.original_file_size)
    {
        size_t seqId;
        inFile.read(reinterpret_cast<char *>(&seqId), sizeof(seqId));

        size_t uncompressedSize;
        inFile.read(reinterpret_cast<char *>(&uncompressedSize), sizeof(uncompressedSize));

        size_t mapSize;
        inFile.read(reinterpret_cast<char *>(&mapSize), sizeof(mapSize));

        // Rebuild local map
        unordered_map<char, size_t> freqMap;
        for (size_t i = 0; i < mapSize; ++i)
        {
            char c;
            size_t freq;
            inFile.read(&c, sizeof(c));
            inFile.read(reinterpret_cast<char *>(&freq), sizeof(freq));
            freqMap[c] = freq;
        }

        // Rebuild local tree
        priority_queue<shared_ptr<Node>, vector<shared_ptr<Node>>, Compare> pq;
        for (auto pair : freqMap)
        {
            pq.push(make_shared<Node>(pair.first, pair.second));
        }

        if (pq.size() == 1)
        {
            auto left = pq.top();
            pq.pop();
            auto right = make_shared<Node>('\0', 0);
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

        // Read Packed Bits
        size_t packedSize;
        inFile.read(reinterpret_cast<char *>(&packedSize), sizeof(packedSize));

        vector<char> packedBits(packedSize);
        inFile.read(packedBits.data(), packedSize);

        // Decode the chunk
        if (root)
        {
            shared_ptr<Node> curr = root;
            size_t decodedThisChunk = 0;

            for (char byte : packedBits)
            {
                if (decodedThisChunk >= uncompressedSize)
                    break;

                for (int i = 7; i >= 0; --i)
                {
                    bool bit = (byte >> i) & 1;

                    if (bit == 0)
                        curr = curr->left;
                    else
                        curr = curr->right;

                    if (!curr->left && !curr->right)
                    {
                        outFile.put(curr->data);
                        decodedThisChunk++;
                        curr = root;

                        if (decodedThisChunk >= uncompressedSize)
                            break;
                    }
                }
            }
        }

        totalDecoded += uncompressedSize;
        cout << "Restored Chunk ID: " << seqId << " (" << uncompressedSize << " bytes)\n";
    }

    inFile.close();
    outFile.close();

    cout << "Success! Fully restored " << totalDecoded << " bytes.\n";
    return 0;
}