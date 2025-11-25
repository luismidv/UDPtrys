#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

int main() {
    std::string folder = "./packets";
    int file_count = 0;
    const int max_files_to_print = 3; // Define the limit

    // Check if the directory exists
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        std::cerr << "Error: Directory '" << folder << "' not found or is not a directory." << std::endl;
        return 1;
    }

    std::cout << "--- Listing First " << max_files_to_print << " Lidar Scan Files ---" << std::endl;

    for (const auto& entry : fs::directory_iterator(folder)) {
        // 1. Check if it's a regular file
        if (entry.is_regular_file()) {
            
            // 2. Check for the .bin extension
            if (entry.path().extension() == ".bin") {
                
                // Print the file path
                std::cout << "Processing file " << (file_count + 1) << ": " << entry.path().string() << std::endl;
                
                // Increment the counter
                file_count++;

                // 3. Check if the limit has been reached
                if (file_count >= max_files_to_print) {
                    std::cout << "--- Limit reached. Stopping iteration. ---" << std::endl;
                    break; // Exit the for loop
                }
            }
        }
    }
    
    if (file_count == 0) {
        std::cout << "No .bin files found in the directory." << std::endl;
    }

    return 0;
}
