#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>

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

int main(int argc, char *argv[])
{
    // Ensure the user provided the compressed file and output file names
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

    // --- STEP 1: READ THE HEADER ---
    streamsize originalFileSize;
    inFile.read(reinterpret_cast<char *>(&originalFileSize), sizeof(originalFileSize));

    size_t mapSize;
    inFile.read(reinterpret_cast<char *>(&mapSize), sizeof(mapSize));

    unordered_map<char, size_t> freqMap;
    for (size_t i = 0; i < mapSize; ++i)
    {
        char c;
        size_t freq;
        inFile.read(&c, sizeof(c));
        inFile.read(reinterpret_cast<char *>(&freq), sizeof(freq));
        freqMap[c] = freq;
    }

    // --- STEP 2: REBUILD THE HUFFMAN TREE ---
    priority_queue<shared_ptr<Node>, vector<shared_ptr<Node>>, Compare> pq;

    for (auto pair : freqMap)
    {
        pq.push(make_shared<Node>(pair.first, pair.second));
    }

    // Handle edge case of an empty file or a file with only 1 unique character
    if (pq.empty())
    {
        ofstream emptyFile(outputFilename);
        return 0;
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

    // --- STEP 3: READ BITS AND DECOMPRESS ---
    ofstream outFile(outputFilename, ios::binary);
    if (!outFile)
    {
        cerr << "Error: Could not create output file.\n";
        return 1;
    }

    shared_ptr<Node> curr = root;
    char byte;
    streamsize decodedChars = 0;

    // Read byte by byte from the compressed data section
    while (inFile.get(byte) && decodedChars < originalFileSize)
    {
        // Read bit by bit from left to right (MSB to LSB)
        for (int i = 7; i >= 0; --i)
        {
            // Extract the i-th bit
            bool bit = (byte >> i) & 1;

            if (bit == 0)
            {
                curr = curr->left;
            }
            else
            {
                curr = curr->right;
            }

            // If we hit a leaf node, we found a character
            if (!curr->left && !curr->right)
            {
                outFile.put(curr->data);
                decodedChars++;
                curr = root; // Reset to top of tree fornext character

                // Stop exactly when we've restored all original characters, ignoring any padding bits at the end of the final byte.
                if (decodedChars == originalFileSize)
                {
                    break;
                }
            }
        }
    }

    inFile.close();
    outFile.close();

    cout << "Successfully decompressed " << inputFilename << " to " << outputFilename << "\n";
    cout << "Restored Size: " << decodedChars << " bytes.\n";

    return 0;
}