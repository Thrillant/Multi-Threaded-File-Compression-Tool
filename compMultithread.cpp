#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include "common.h"
#include "ThreadSafeQueue.h"
#include <chrono>

using namespace std;

// ... [KEEP YOUR generateCodes() FUNCTION HERE] ...
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

// ... [KEEP YOUR processChunk() FUNCTION EXACTLY AS IT WAS IN PHASE 2] ...
CompressedChunk processChunk(const UncompressedChunk &chunk)
{
    CompressedChunk result;
    result.sequence_id = chunk.sequence_id;
    unordered_map<char, size_t> freqMap;
    for (char c : chunk.data)
        freqMap[c]++;

    priority_queue<shared_ptr<Node>, vector<shared_ptr<Node>>, Compare> pq;
    for (auto pair : freqMap)
        pq.push(make_shared<Node>(pair.first, pair.second));

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
            pq.push(make_shared<Node>(left->freq + right->freq, left, right));
        }
        root = pq.top();
    }

    unordered_map<char, string> huffmanCodes;
    if (root)
        generateCodes(root, "", huffmanCodes);

    vector<char> packedBits;
    unsigned char bitBuffer = 0;
    int bitCount = 0;
    for (char c : chunk.data)
    {
        for (char bit : huffmanCodes[c])
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

// --- THREADING ARCHITECTURE ---

// 1. Worker Thread Loop
void workerLoop(ThreadSafeQueue<UncompressedChunk> &workQueue, ThreadSafeQueue<CompressedChunk> &writeQueue)
{
    UncompressedChunk chunk;
    // Keep popping until the queue is empty AND closed by the producer
    while (workQueue.pop(chunk))
    {
        CompressedChunk compressed = processChunk(chunk);
        writeQueue.push(compressed); // Send to writer
    }
}

// 2. Writer Thread Loop
void writerLoop(ofstream &outFile, size_t totalChunks, ThreadSafeQueue<CompressedChunk> &writeQueue)
{
    map<size_t, CompressedChunk> outOfOrderBuffer;
    size_t expectedId = 0;

    // The writer knows exactly how many chunks to expect
    while (expectedId < totalChunks)
    {
        CompressedChunk chunk;
        if (writeQueue.pop(chunk))
        {
            // Store the incoming chunk in our map (which automatically sorts by sequence_id)
            outOfOrderBuffer[chunk.sequence_id] = move(chunk);

            // While we have the specific chunk we are waiting for, write it to disk!
            while (outOfOrderBuffer.count(expectedId))
            {
                CompressedChunk &c = outOfOrderBuffer[expectedId];

                outFile.write(reinterpret_cast<const char *>(&c.sequence_id), sizeof(c.sequence_id));
                outFile.write(c.compressed_payload.data(), c.compressed_payload.size());

                outOfOrderBuffer.erase(expectedId);
                expectedId++;
                cout << "Writer saved chunk " << (expectedId - 1) << " to disk.\n";
            }
        }
    }
}


int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cerr << "Usage: " << argv[0] << " <input_file> <output_file.bin>\n";
        return 1;
    }
    auto startTime = chrono::high_resolution_clock::now();

    ifstream inFile(argv[1], ios::binary | ios::ate);
    if (!inFile)
        return 1;

    size_t fileSize = inFile.tellg();
    inFile.seekg(0, ios::beg);

    ofstream outFile(argv[2], ios::binary);
    if (!outFile)
        return 1;

    // Write Global Header
    GlobalHeader header;
    copy(begin(MAGIC_NUMBER), end(MAGIC_NUMBER), header.magic);
    header.original_file_size = fileSize;
    header.chunk_size = CHUNK_SIZE;
    outFile.write(reinterpret_cast<const char *>(&header), sizeof(GlobalHeader));

    // Calculate exactly how many chunks this file will be divided into
    size_t totalChunks = (fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE;
    cout << "File size: " << fileSize << " bytes. Target chunks: " << totalChunks << "\n";

    // Initialize our two communication pipelines
    ThreadSafeQueue<UncompressedChunk> workQueue;
    ThreadSafeQueue<CompressedChunk> writeQueue;

    // Determine how many cores the CPU has (default to 4 if OS doesn't report)
    unsigned int numWorkers = thread::hardware_concurrency();
    if (numWorkers == 0)
        numWorkers = 4;
    cout << "Spawning " << numWorkers << " worker threads...\n";

    // 1. Spawn the Writer Thread
    thread writerThread(writerLoop, ref(outFile), totalChunks, ref(writeQueue));

    // 2. Spawn the Worker Threads
    vector<thread> workers;
    for (unsigned int i = 0; i < numWorkers; ++i)
    {
        workers.emplace_back(workerLoop, ref(workQueue), ref(writeQueue));
    }

    // 3. Main Thread acts as the Producer
    size_t seqId = 0;
    while (!inFile.eof())
    {
        UncompressedChunk currentChunk;
        currentChunk.sequence_id = seqId;
        currentChunk.data.resize(CHUNK_SIZE);

        inFile.read(currentChunk.data.data(), CHUNK_SIZE);
        size_t bytesRead = inFile.gcount();

        if (bytesRead < CHUNK_SIZE)
            currentChunk.data.resize(bytesRead);

        if (bytesRead > 0)
        {
            workQueue.push(move(currentChunk));
            seqId++;
        }
    }

    inFile.close();

    // 4. Cleanup and Join
    // Tell the workers no more chunks are coming
    workQueue.close();

    // Wait for all workers to finish processing
    for (auto &t : workers)
    {
        t.join();
    }

    // Wait for the writer to finish assembling the file
    writerThread.join();
    outFile.close();

    cout << "\nSuccess! Multithreaded compression complete.\n";
    auto stopTime = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(stopTime - startTime);
    cout << "Total Execution Time: " << duration.count() / 1000.0 << " seconds.\n";
    return 0;
}