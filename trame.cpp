#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <iomanip>

namespace fs = std::filesystem;

int main() {
    std::string folder = "./packets";
    int printedFiles = 0;

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (printedFiles >= 3) break;        // stop after 3 files
        if (!entry.is_regular_file()) continue;

        // Ensure it's a .bin file (optional but safer)
        if (entry.path().extension() != ".bin") continue;

        std::cout << "File " << (printedFiles + 1)
                  << ": " << entry.path().string() << "\n";

        std::ifstream file(entry.path(), std::ios::binary);
        if (!file) {
            std::cerr << "Error opening file\n";
            continue;
        }

        // Read entire file
        std::vector<unsigned char> data(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        // Print all bytes as hex
        std::cout << "Content (" << data.size() << " bytes):\n";
        for (size_t i = 0; i < data.size(); ++i) {
            if (i % 16 == 0) std::cout << "\n";   // group lines
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(data[i]) << " ";
        }

        std::cout << std::dec << "\n\n";  // restore decimal formatting
        printedFiles++;
    }

    return 0;
}
