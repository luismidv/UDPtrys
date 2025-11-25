#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <iomanip> // For std::hex and std::setw

namespace fs = std::filesystem;

// --- Utility Function to Print Binary Data as Hex ---
void print_file_content_as_hex(const std::vector<unsigned char>& data) {
    std::cout << "\n--- File Content (Hex Dump) ---" << std::endl;
    
    // Set up output for hex display
    std::cout << std::hex << std::setfill('0');

    // Print up to the first 100 bytes (adjust limit as needed)
    // Printing a very large file entirely might crash your terminal/program
    const size_t display_limit = std::min((size_t)100, data.size()); 

    for (size_t i = 0; i < display_limit; ++i) {
        // Print the byte value (cast to int to avoid char interpretation)
        std::cout << std::setw(2) << static_cast<int>(data[i]) << " ";
        
        // Add a newline every 16 bytes for readability
        if ((i + 1) % 16 == 0) {
            std::cout << std::endl;
        }
    }
    
    if (data.size() > display_limit) {
        std::cout << "\n... and " << (data.size() - display_limit) << " more bytes omitted." << std::endl;
    }
    
    std::cout << "\n--- End of Dump ---\n" << std::endl;
}
// ----------------------------------------------------

int main() {
    std::string folder = "./packets";

    // Check if the directory exists
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        std::cerr << "Error: Directory '" << folder << "' not found or is not a directory." << std::endl;
        return 1;
    }

    std::cout << "--- Starting File Processing (Printing Content) ---" << std::endl;
    int file_count = 0;

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            
            // Filter only for .bin files
            if (entry.path().extension() == ".bin") {
                
                std::cout << "Processing file: " << entry.path().string() << std::endl;
                
                // --- Read file content into a vector ---
                std::ifstream file(entry.path(), std::ios::binary);
                
                if (file) {
                    // Load entire file content into a vector of unsigned chars
                    std::vector<unsigned char> data(
                        (std::istreambuf_iterator<char>(file)), 
                        std::istreambuf_iterator<char>()
                    );
                    
                    // --- Print the content as Hex ---
                    if (!data.empty()) {
                        std::cout << "File size: " << data.size() << " bytes." << std::endl;
                        print_file_content_as_hex(data);
                    } else {
                        std::cout << "File is empty." << std::endl;
                    }
                    
                    file_count++;
                } else {
                    std::cerr << "Error: Could not open file: " << entry.path().string() << std::endl;
                }
            }
        }
    }
    
    if (file_count == 0) {
        std::cout << "No .bin files were processed in the directory." << std::endl;
    }

    return 0;
}
