#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <iomanip> // For std::setw, std::hex, std::dec

namespace fs = std::filesystem;

// --- 1. Utility for Little Endian to Host Conversion ---

/**
 * @brief Converts a 4-byte Little Endian value to the host system's endianness (usually for Scan ID).
 */
inline uint32_t le_to_h_u32(uint32_t value) {
    // Standard cross-platform check for Little Endian
    #if __BYTE_ORDER == __LITTLE_ENDIAN || defined(__LITTLE_ENDIAN__)
        return value;
    #else
        // Byte swap for Big Endian systems
        return (value >> 24) | ((value << 8) & 0x00FF0000) | ((value >> 8) & 0x0000FF00) | (value << 24);
    #endif
}

/**
 * @brief Converts a 2-byte Little Endian value to the host system's endianness (used for distance/RSSI).
 */
inline uint16_t le_to_h_u16(uint16_t value) {
    #if __BYTE_ORDER == __LITTLE_ENDIAN || defined(__LITTLE_ENDIAN__)
        return value;
    #else
        return (value >> 8) | (value << 8);
    #endif
}


// --- 2. Data Structure Definitions (Packed) ---
#pragma pack(push, 1)

// Based on the documentation (Table 5: Data output: Header)
struct SICK_DataOutput_Header {
    uint8_t version[4];         // 4 bytes: Version (0-3)
    uint32_t device_sn;         // 4 bytes: Device serial number
    uint32_t system_plug_sn;    // 4 bytes: Serial number of the system plug
    uint8_t channel_num;        // 1 byte: Channel number
    uint8_t reserved_1[3];      // 3 bytes: Reserved
    uint32_t sequence_num;      // 4 bytes: Sequence number
    uint32_t scan_num;          // 4 bytes: Scan number (Scan Identification)
    uint32_t timestamp_sec;     // 4 bytes: Time stamp (seconds)
    uint32_t timestamp_usec;    // 4 bytes: Time stamp (microseconds)
    uint32_t offset_device_status;      // 4 bytes: Offset to Device status block
    uint32_t offset_config;             // 4 bytes: Offset to Configuration block
    uint32_t offset_measurement_data;   // 4 bytes: Offset to Measurement data block
    uint32_t offset_field_interruption; // 4 bytes: Offset to Field interruption block
    uint32_t offset_application_data;   // 4 bytes: Offset to Application data block
    uint32_t offset_local_io;           // 4 bytes: Offset to Local I/O block
    uint32_t total_length;              // 4 bytes: Total length of the data instance (without headers)
}; // Total size: 60 bytes

#pragma pack(pop)

// --- 3. Parsing Function ---

void process_file_content(const std::vector<unsigned char>& data, const std::string& filename) {
    
    // The SICK Data Output Header is 60 bytes.
    constexpr size_t SICK_HEADER_SIZE = 60;

    if (data.size() < SICK_HEADER_SIZE) {
        std::cerr << "  [ERROR] File too small to contain a SICK header (" << data.size() << " bytes, expected >= " << SICK_HEADER_SIZE << ")." << std::endl;
        return;
    }

    // --- 3.1. Get Header Info (Assuming SICK Header starts at index 0) ---
    
    // We assume the SICK Data Output Header starts at the very beginning of the file.
    const unsigned char* header_ptr = data.data(); 
    
    SICK_DataOutput_Header header;
    std::memcpy(&header, header_ptr, sizeof(SICK_DataOutput_Header));

    uint32_t scan_id = le_to_h_u32(header.scan_num);
    uint32_t measurement_data_offset = le_to_h_u32(header.offset_measurement_data);
    uint32_t total_payload_length = le_to_h_u32(header.total_length); // Total length of data following the header

    std::cout << "\n  [Metadata] Scan Identification (Scan Number): " << scan_id << std::endl;

    if (measurement_data_offset == 0) {
        std::cout << "  [INFO] Measurement Data block not present (offset is 0)." << std::endl;
        return;
    }

    // --- 3.2. Check Bounds ---
    
    // The Measurement Data block starts at: SICK Header (60) + Measurement Data Offset
    size_t data_block_start_index = SICK_HEADER_SIZE + measurement_data_offset;

    // A valid block must start *before* the total file size and must be at least 4 bytes (for its own length field).
    if (data_block_start_index + 4 > data.size()) {
        std::cerr << "  [ERROR] Calculated Measurement Data offset (" << data_block_start_index 
                  << ") is out of file bounds (" << data.size() << "). Check file structure." << std::endl;
        return;
    }

    // --- 3.3. Locate and Process Measurement Data Block ---
    
    const unsigned char* block_ptr = data.data() + data_block_start_index;
    uint32_t block_length;
    
    std::memcpy(&block_length, block_ptr, 4);
    block_length = le_to_h_u32(block_length); 

    std::cout << "  [Data] Measurement Data Block Found (Length: " << block_length << " bytes)." << std::endl;

    // Measurement data starts 4 bytes after the block start (after the Length field)
    const unsigned char* data_ptr = block_ptr + 4;
    size_t current_byte_index = data_block_start_index + 4;
    size_t points_processed = 0;

    // Ensure the entire measurement block (including its length field) is within the file bounds
    if (data_block_start_index + 4 + block_length > data.size()) {
        std::cerr << "  [ERROR] Declared block length (" << block_length << " bytes) exceeds file end." << std::endl;
        return;
    }

    constexpr size_t BYTES_PER_POINT = 4; // 2 bytes Distance/Status + 2 bytes RSSI/Intensity

    // Loop through the data points
    while (current_byte_index + BYTES_PER_POINT <= data_block_start_index + 4 + block_length) {
        
        // --- Distance and Status (2 bytes, Little Endian) ---
        uint16_t dist_status_le;
        std::memcpy(&dist_status_le, data_ptr, 2);
        uint16_t dist_status = le_to_h_u16(dist_status_le);
        
        uint16_t distance_mm = dist_status & 0x1FFF; // Distance is in the lower 13 bits (mm)
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
        current_byte_index += BYTES_PER_POINT;
        points_processed++;

        // Stop after 20 points for brevity
        if (points_processed >= 20) {
            std::cout << "    [...] Showing first 20 data points only." << std::endl;
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
