#include <iostream>
#include <fstream>
#include <string>

using namespace std;

int main()
{
    ofstream outFile("text_500MB.txt", ios::binary);
    if (!outFile)
    {
        cerr << "Failed to create file.\n";
        return 1;
    }

    string sentence = "The quick brown fox jumps over the lazy dog.\n";

    // We want 500MB. 500 * 1024 * 1024 = 524,288,000 bytes.
    size_t targetBytes = 524288000;
    size_t writtenBytes = 0;

    cout << "Generating 500MB file... please wait.\n";

    while (writtenBytes < targetBytes)
    {
        outFile.write(sentence.c_str(), sentence.size());
        writtenBytes += sentence.size();
    }

    outFile.close();
    cout << "Done! File text_500MB.txt created.\n";
    return 0;
}