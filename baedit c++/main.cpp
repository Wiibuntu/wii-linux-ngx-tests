#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <algorithm>

const std::string startMarker = "mark.start=1 ";
const std::string endMarker = " mark.end=1";

void usage() {
    std::cerr << "Usage: baedit zImage ['new kernel command line']\n";
    std::exit(1);
}

int main(int argc, char* argv[]) {
    if (argc != 3 && argc != 2) {
        usage();
    }

    std::string zImage = argv[1];

    // Read the file
    std::ifstream inFile(zImage, std::ios::binary);
    if (!inFile) {
        std::cerr << "Error reading file: " << zImage << "\n";
        std::exit(1);
    }

    std::vector<char> fileData((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();
    std::cout << ">OK: " << fileData.size() << " bytes read\n";

    // Find the start marker
    auto start = std::search(fileData.begin(), fileData.end(), startMarker.begin(), startMarker.end());
    if (start == fileData.end()) {
        std::cerr << "Cannot find start marker\n";
        std::exit(1);
    }
    start += startMarker.size();

    // Find the end marker
    auto end = std::search(start, fileData.end(), endMarker.begin(), endMarker.end());
    if (end == fileData.end()) {
        std::cerr << "Cannot find end marker\n";
        std::exit(1);
    }

    size_t availBytes = std::distance(start, end);
    std::string bootArgs(start, start + availBytes);
    std::cout << "current  bootargs = '" << bootArgs << "'\n";

    if (argc < 3) {
        return 0;
    }

    // Replace the boot arguments
    std::string newBootArgs = argv[2];
    if (newBootArgs.size() > availBytes) {
        std::cerr << "New command line is too long\n";
        std::exit(1);
    }

    // Pad with spaces
    newBootArgs.append(availBytes - newBootArgs.size(), ' ');
    std::cout << "replaced bootargs = '" << newBootArgs << "'\n";

    std::copy(newBootArgs.begin(), newBootArgs.end(), start);

    // Write the file
    std::ofstream outFile(zImage, std::ios::binary);
    if (!outFile) {
        std::cerr << "Error writing file: " << zImage << "\n";
        std::exit(1);
    }
    outFile.write(fileData.data(), fileData.size());
    outFile.close();
    std::cout << ">OK: " << fileData.size() << " bytes written\n";

    return 0;
}
