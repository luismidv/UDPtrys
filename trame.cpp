#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

int main() {
    std::string folder = "./packets";
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {      // only normal files
            std::cout << "Processing file: " << entry.path().string()<< std::endl;
            // --- read file or process packet here ---
            // example: load entire file
            std::ifstream file(entry.path(), std::ios::binary);
            if (file) {
                std::vector<unsigned char> data(
                    (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()
                ); // do something with packet data
            }
        }
    }
    return 0;
}
