#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include "common.h"
#include "ThreadSafeQueue.h"

using namespace std;

// A new struct specifically to hold the data a worker needs to decode a chunk
struct DecompressTask
{
    size_t sequence_id;
    size_t uncompressed_size;
    unordered_map<char, size_t> freq_map;
    vector<char> packed_bits;
};

// 1. Worker Thread Loop
void workerLoop(ThreadSafeQueue<DecompressTask> &workQueue, ThreadSafeQueue<UncompressedChunk> &writeQueue)
{
    DecompressTask task;

    while (workQueue.pop(task))
    {
        UncompressedChunk result;
        result.sequence_id = task.sequence_id;

        // 1. Rebuild Local Huffman Tree
        priority_queue<shared_ptr<Node>, vector<shared_ptr<Node>>, Compare> pq;
        for (auto pair : task.freq_map)
        {
            pq.push(make_shared<Node>(pair.first, pair.second));
        }

        if (pq.size() == 1)
        {
            auto left = pq.top();
            pq.pop();
            pq.push(make_shared<Node>(left->freq, left, make_shared<Node>('\0', 0)));
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
                pq.push(make_shared<Node>(left->freq + right->freq, left, right));
            }
            root = pq.top();
        }

        // 2. Decode the Bits
        result.data.reserve(task.uncompressed_size); // Pre-allocate memory for speed
        if (root)
        {
            shared_ptr<Node> curr = root;
            size_t decoded = 0;

            for (char byte : task.packed_bits)
            {
                if (decoded >= task.uncompressed_size)
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
                        result.data.push_back(curr->data);
                        decoded++;
                        curr = root;

                        if (decoded >= task.uncompressed_size)
                            break;
                    }
                }
            }
        }
        // Send the fully restored text chunk to the writer
        writeQueue.push(move(result));
    }
}

// 2. Writer Thread Loop
void writerLoop(ofstream &outFile, size_t totalChunks, ThreadSafeQueue<UncompressedChunk> &writeQueue)
{
    map<size_t, UncompressedChunk> outOfOrderBuffer;
    size_t expectedId = 0;

    while (expectedId < totalChunks)
    {
        UncompressedChunk chunk;
        if (writeQueue.pop(chunk))
        {
            outOfOrderBuffer[chunk.sequence_id] = move(chunk);

            while (outOfOrderBuffer.count(expectedId))
            {
                UncompressedChunk &c = outOfOrderBuffer[expectedId];
                outFile.write(c.data.data(), c.data.size());
                outOfOrderBuffer.erase(expectedId);
                expectedId++;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cerr << "Usage: " << argv[0] << " <compressed_file.bin> <output_restored.txt>\n";
        return 1;
    }

    auto startTime = chrono::high_resolution_clock::now();

    ifstream inFile(argv[1], ios::binary);
    if (!inFile)
        return 1;

    ofstream outFile(argv[2], ios::binary);
    if (!outFile)
        return 1;

    // --- 1. READ GLOBAL HEADER ---
    GlobalHeader header;
    inFile.read(reinterpret_cast<char *>(&header), sizeof(GlobalHeader));

    if (string(header.magic, 4) != string(MAGIC_NUMBER, 4))
    {
        cerr << "Error: Not a valid MZIP file!\n";
        return 1;
    }

    // Calculate how many chunks we are dealing with so the Writer knows when to stop
    size_t totalChunks = (header.original_file_size + header.chunk_size - 1) / header.chunk_size;
    cout << "Detected " << totalChunks << " chunks to decompress...\n";

    // --- 2. MULTITHREADING SETUP ---
    ThreadSafeQueue<DecompressTask> workQueue;
    ThreadSafeQueue<UncompressedChunk> writeQueue;

    unsigned int numWorkers = thread::hardware_concurrency();
    if (numWorkers == 0)
        numWorkers = 4;

    thread writerThread(writerLoop, ref(outFile), totalChunks, ref(writeQueue));

    vector<thread> workers;
    for (unsigned int i = 0; i < numWorkers; ++i)
    {
        workers.emplace_back(workerLoop, ref(workQueue), ref(writeQueue));
    }

    // --- 3. PRODUCER LOOP (Main Thread) ---
    size_t chunksRead = 0;
    while (chunksRead < totalChunks && !inFile.eof())
    {
        DecompressTask task;

        // Read Sequence ID
        if (!inFile.read(reinterpret_cast<char *>(&task.sequence_id), sizeof(task.sequence_id)))
            break;

        // Read Uncompressed Size
        inFile.read(reinterpret_cast<char *>(&task.uncompressed_size), sizeof(task.uncompressed_size));

        // Read Frequency Map
        size_t mapSize;
        inFile.read(reinterpret_cast<char *>(&mapSize), sizeof(mapSize));
        for (size_t i = 0; i < mapSize; ++i)
        {
            char c;
            size_t freq;
            inFile.read(&c, sizeof(c));
            inFile.read(reinterpret_cast<char *>(&freq), sizeof(freq));
            task.freq_map[c] = freq;
        }

        // Read Packed Bits
        size_t packedSize;
        inFile.read(reinterpret_cast<char *>(&packedSize), sizeof(packedSize));
        task.packed_bits.resize(packedSize);
        inFile.read(task.packed_bits.data(), packedSize);

        workQueue.push(move(task));
        chunksRead++;
    }

    inFile.close();

    // --- 4. CLEANUP ---
    workQueue.close();
    for (auto &t : workers)
        t.join();
    writerThread.join();
    outFile.close();

    auto stopTime = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(stopTime - startTime);

    cout << "\nSuccess! Fully restored " << header.original_file_size << " bytes.\n";
    cout << "Total Decompression Time: " << duration.count() / 1000.0 << " seconds.\n";

    return 0;
}