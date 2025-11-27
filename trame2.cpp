#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <iomanip>

namespace fs = std::filesystem;

// --- 1. Utility for Little Endian to Host Conversion ---

inline uint32_t le_to_h_u32(uint32_t value) {
    #if __BYTE_ORDER == __LITTLE_ENDIAN || defined(__LITTLE_ENDIAN__)
        return value;
    #else
        return (value >> 24) | ((value << 8) & 0x00FF0000) | ((value >> 8) & 0x0000FF00) | (value << 24);
    #endif
}

inline uint16_t le_to_h_u16(uint16_t value) {
    #if __BYTE_ORDER == __LITTLE_ENDIAN || defined(__LITTLE_ENDIAN__)
        return value;
    #else
        return (value >> 8) | (value << 8);
    #endif
}


// --- 2. Data Structure Definitions (Packed) ---
#pragma pack(push, 1)

// Only fields used for metadata are defined here.
struct SICK_DataOutput_Header {
    uint8_t version[4];         // 4 bytes | Struct Offset 0
    uint32_t device_sn;         // 4 bytes | Struct Offset 4
    uint32_t system_plug_sn;    // 4 bytes | Struct Offset 8
    uint8_t channel_num;        // 1 byte | Struct Offset 12
    uint8_t reserved_1[3];      // 3 bytes | Struct Offset 13
    uint32_t sequence_num;      // 4 bytes | Struct Offset 16
    uint32_t scan_num;          // 4 bytes | Struct Offset 20 <--- Scan ID
    uint32_t timestamp_sec;     // 4 bytes | Struct Offset 24
    uint32_t timestamp_usec;    // 4 bytes | Struct Offset 28
    // ... remaining offset fields are ignored for parsing ...
    uint8_t remaining_header[60 - 32]; // Fill the rest of the 60 bytes
}; // Total size: 60 bytes

#pragma pack(pop)

// --- 3. Parsing Function (Hardcoded Offsets based on Hex Dump) ---

void process_file_content(const std::vector<unsigned char>& data, const std::string& filename) {
    
    // Derived from the hex dump: A custom 20-byte preamble/file header precedes the SICK UDP header.
    constexpr size_t CUSTOM_PREAMBLE_SIZE = 20; 
    constexpr size_t SICK_HEADER_SIZE = 60;
    
    // CRITICAL FIX: The Measurement Data Block is assumed to immediately follow the header structure.
    constexpr size_t DATA_BLOCK_START_INDEX = CUSTOM_PREAMBLE_SIZE + SICK_HEADER_SIZE; // 20 + 60 = 80 bytes

    if (data.size() < DATA_BLOCK_START_INDEX) {
        std::cerr << "  [ERROR] File too small to contain header structure (" << data.size() 
                  << " bytes, expected >= " << DATA_BLOCK_START_INDEX << ")." << std::endl;
        return;
    }

    // --- 3.1. Get Header Info (Read SICK Header starting at index 20) ---
    
    const unsigned char* header_ptr = data.data() + CUSTOM_PREAMBLE_SIZE;
    
    SICK_DataOutput_Header header;
    std::memcpy(&header, header_ptr, sizeof(SICK_DataOutput_Header));

    // The Scan ID is correctly aligned at File Offset 40 (Struct Offset 20 + Preamble 20)
    uint32_t scan_id = le_to_h_u32(header.scan_num);
    
    // Print the custom preamble to verify alignment (MS3 MD)
    std::cout << "  [Preamble] First 8 bytes: " << std::string(data.data(), data.data() + 8) << std::endl;
    std::cout << "  [Metadata] Scan Identification (Scan Number): " << scan_id << std::endl;

    // --- 3.2. Calculate Measurement Data Length ---
    
    size_t total_data_bytes = data.size() - DATA_BLOCK_START_INDEX;
    constexpr size_t BYTES_PER_POINT = 4; // 2 bytes Distance/Status + 2 bytes RSSI/Intensity

    if (total_data_bytes % BYTES_PER_POINT != 0) {
        std::cerr << "  [WARNING] Remaining data size (" << total_data_bytes 
                  << " bytes) is not perfectly divisible by 4. Parsing may be incomplete." << std::endl;
    }
    
    size_t total_points = total_data_bytes / BYTES_PER_POINT;
    
    std::cout << "  [Data] Measurement Data starts at byte " << DATA_BLOCK_START_INDEX 
              << ". Total " << total_data_bytes << " bytes / " << total_points << " points." << std::endl;

    // --- 3.3. Process Measurement Data ---
    
    const unsigned char* data_ptr = data.data() + DATA_BLOCK_START_INDEX;
    size_t points_processed = 0;

    // Loop through the data points
    while (points_processed < total_points) {
        
        // --- Distance and Status (2 bytes, Little Endian) ---
        uint16_t dist_status_le;
        std::memcpy(&dist_status_le, data_ptr, 2);
        uint16_t dist_status = le_to_h_u16(dist_status_le);
        
        uint16_t distance_mm = dist_status & 0x1FFF; // Distance in the lower 13 bits (mm)
        uint8_t status_flags = (dist_status >> 13) & 0x07; // Status in the upper 3 bits

        // --- RSSI/Intensity (2 bytes, Little Endian) ---
        uint16_t rssi_le;
        std::memcpy(&rssi_le, data_ptr + 2, 2);
        uint16_t rssi = le_to_h_u16(rssi_le); 

        // --- Print Human-Readable Data ---
        std::cout << "    Point " << std::setw(4) << points_processed << ": ";
        std::cout << "Distance: " << std::setw(5) << distance_mm << " mm, ";
        std::cout << "Intensity (RSSI): " << std::setw(4) << rssi << ", ";
        std::cout << "Status Flags: 0x" << std::hex << std::setw(1) << static_cast<int>(status_flags) << std::dec;
        std::cout << std::endl;
        
        // Move to the next data point
        data_ptr += BYTES_PER_POINT;
        points_processed++;

        // Stop after 20 points for brevity
        if (points_processed >= 20) {
            std::cout << "    [...] Showing first 20 data points only (Total " << total_points << " points)." << std::endl;
            break;
        }
    }
}

// --- 4. Main Program Loop (Unchanged) ---

int main() {
    std::string folder = "./packets";

    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        std::cerr << "Error: Directory '" << folder << "' not found or is not a directory." << std::endl;
        return 1;
    }

    std::cout << "--- Starting Lidar Packet Parsing ---" << std::endl;
    int file_count = 0;

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            
            std::cout << "\n========================================================" << std::endl;
            std::cout << "Processing file: " << entry.path().string() << std::endl;
            
            std::ifstream file(entry.path(), std::ios::binary);
            
            if (file) {
                std::vector<unsigned char> data(
                    (std::istreambuf_iterator<char>(file)), 
                    std::istreambuf_iterator<char>()
                );
                
                if (!data.empty()) {
                    std::cout << "File size: " << data.size() << " bytes." << std::endl;
                    process_file_content(data, entry.path().string());
                } else {
                    std::cout << "  File is empty." << std::endl;
                }
                
                file_count++;
            } else {
                std::cerr << "Error: Could not open file: " << entry.path().string() << std::endl;
            }
        }
    }
    
    if (file_count == 0) {
        std::cout << "\nNo .bin files were processed in the directory." << std::endl;
    }

    return 0;
}