#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <string>

using namespace std;
// 1. Node Structure
struct Node
{
    char data;
    char min_char; // Tracks the alphabetically smallest char in this branch
    size_t freq;
    shared_ptr<Node> left;
    shared_ptr<Node> right;

    // Leaf Node
    Node(char d, size_t f) : data(d), min_char(d), freq(f), left(nullptr), right(nullptr) {}

    // Internal Node
    Node(size_t f, shared_ptr<Node> l, shared_ptr<Node> r) : data('\0'), freq(f), left(l), right(r)
    {
        // The min_char of a parent is the smallest min_char of its children
        min_char = min(l->min_char, r->min_char);
    }
};

// 2. Custom comparator
struct Compare
{
    bool operator()(const shared_ptr<Node> &l, const shared_ptr<Node> &r)
    {
        // If frequencies are identical, break the tie alphabetically!
        if (l->freq == r->freq)
        {
            return l->min_char > r->min_char;
        }
        // Otherwise, sort by frequency as usual
        return l->freq > r->freq;
    }
};

// 2. Recursive function to generate Prefix Codes
void generateCodes(const shared_ptr<Node> &root, const string &code, unordered_map<char, string> &huffmanCodes)
{
    if (!root)
        return;

    // If it's a leaf node, it contains a character
    if (!root->left && !root->right)
    {
        huffmanCodes[root->data] = code;
    }

    generateCodes(root->left, code + "0", huffmanCodes);
    generateCodes(root->right, code + "1", huffmanCodes);
}

int main(int argc, char *argv[])
{
    // Ensure the user provided an input file and an output file name
    if (argc != 3)
    {
        cerr << "Usage: " << argv[0] << " <input_file> <output_file.bin>\n";
        return 1;
    }

    string inputFilename = argv[1];
    string outputFilename = argv[2];

    // --- STEP 1: READ THE FILE INTO MEMORY ---
    // Open in binary mode and seek to the end (ate) to get file size immediately
    ifstream inFile(inputFilename, ios::binary | ios::ate);
    if (!inFile)
    {
        cerr << "Error: Could not open input file.\n";
        return 1;
    }

    streamsize fileSize = inFile.tellg();
    inFile.seekg(0, ios::beg);

    vector<char> buffer(fileSize);
    if (!inFile.read(buffer.data(), fileSize))
    {
        cerr << "Error: Could not read file data.\n";
        return 1;
    }
    inFile.close();

    // --- STEP 2: FREQUENCY ANALYSIS ---
    unordered_map<char, size_t> freqMap;
    for (char c : buffer)
    {
        freqMap[c]++;
    }

    // --- STEP 3: BUILD THE HUFFMAN TREE ---
    priority_queue<shared_ptr<Node>, vector<shared_ptr<Node>>, Compare> pq;

    for (auto pair : freqMap)
    {
        pq.push(make_shared<Node>(pair.first, pair.second));
    }

    while (pq.size() > 1)
    {
        auto left = pq.top();
        pq.pop();
        auto right = pq.top();
        pq.pop();
        size_t sum = left->freq + right->freq;
        pq.push(make_shared<Node>(sum, left, right));
    }

    shared_ptr<Node> root = pq.top();

    // --- STEP 4: GENERATE CODES ---
    unordered_map<char, string> huffmanCodes;
    generateCodes(root, "", huffmanCodes);

    // --- STEP 5: WRITE OUTPUT FILE (HEADER + BIT PACKING) ---
    ofstream outFile(outputFilename, ios::binary);
    if (!outFile)
    {
        cerr << "Error: Could not create output file.\n";
        return 1;
    }

    // Write Header: Original file size (crucial for knowing when to stop decompressing)
    outFile.write(reinterpret_cast<const char *>(&fileSize), sizeof(fileSize));

    // Write Header: Frequency map size and data (so decompressor can rebuild tree)
    size_t mapSize = freqMap.size();
    outFile.write(reinterpret_cast<const char *>(&mapSize), sizeof(mapSize));
    for (auto pair : freqMap)
    {
        outFile.write(&pair.first, sizeof(pair.first));
        outFile.write(reinterpret_cast<const char *>(&pair.second), sizeof(pair.second));
    }

    // Write Data: Bit Packing
    unsigned char bitBuffer = 0;
    int bitCount = 0;

    for (char c : buffer)
    {
        string code = huffmanCodes[c];
        for (char bit : code)
        {
            bitBuffer <<= 1; // Shift bits left
            if (bit == '1')
            {
                bitBuffer |= 1; // Flip the lowest bit to 1
            }
            bitCount++;

            if (bitCount == 8)
            { // Buffer is full, write byte to file
                outFile.put(bitBuffer);
                bitBuffer = 0;
                bitCount = 0;
            }
        }
    }

    // Pad remaining bits if the file didn't end perfectly on an 8-bit boundary
    if (bitCount > 0)
    {
        bitBuffer <<= (8 - bitCount);
        outFile.put(bitBuffer);
    }

    outFile.close();
    cout << "Successfully compressed " << inputFilename << " to " << outputFilename << "\n";
    cout << "Original Size: " << fileSize << " bytes.\n";

    return 0;
}