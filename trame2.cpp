#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstring> // For memcpy

namespace fs = std::filesystem;

// --- 1. Utility for Little Endian to Host Conversion ---

/**
 * @brief Converts a 4-byte Little Endian value to the host system's endianness (usually used for Scan ID).
 * @param value The 32-bit value in Little Endian format.
 * @return The 32-bit value in host format.
 */
inline uint32_t le_to_h_u32(uint32_t value) {
    // Check if the host is Little Endian (no conversion needed) or Big Endian (conversion needed)
    // This check is a common way to handle endianness cross-platform.
    #if __BYTE_ORDER == __LITTLE_ENDIAN || defined(__LITTLE_ENDIAN__)
        return value;
    #else
        // Manual byte swap for Big Endian systems (e.g., some PowerPC or older ARM)
        return (value >> 24) |
               ((value << 8) & 0x00FF0000) |
               ((value >> 8) & 0x0000FF00) |
               (value << 24);
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


// --- 2. Data Structure Definitions (Packed for accurate byte alignment) ---

// Use #pragma pack or equivalent to ensure no padding is added by the compiler.
#pragma pack(push, 1)

// Based on the documentation (Table 5: Data output: Header)
// The offsets here are RELATIVE to the start of the 60-byte SICK Data Output Header (which starts after the 24-byte UDP Header).
struct SICK_DataOutput_Header {
    uint8_t version[4];         // 4 bytes: Version (0-3)
    uint32_t device_sn;         // 4 bytes: Device serial number
    uint32_t system_plug_sn;    // 4 bytes: Serial number of the system plug
    uint8_t channel_num;        // 1 byte: Channel number
    uint8_t reserved_1[3];      // 3 bytes: Reserved
    uint32_t sequence_num;      // 4 bytes: Sequence number
    uint32_t scan_num;          // 4 bytes: Scan number (This is the "Scan Identification") 
    uint32_t timestamp_sec;     // 4 bytes: Time stamp (seconds)
    uint32_t timestamp_usec;    // 4 bytes: Time stamp (microseconds)
    uint32_t offset_device_status;      // 4 bytes: Offset to Device status block
    uint32_t offset_config;             // 4 bytes: Offset to Configuration block
    uint32_t offset_measurement_data;   // 4 bytes: Offset to Measurement data block (Crucial for finding the data)
    uint32_t offset_field_interruption; // 4 bytes: Offset to Field interruption block
    uint32_t offset_application_data;   // 4 bytes: Offset to Application data block
    uint32_t offset_local_io;           // 4 bytes: Offset to Local I/O block
    uint32_t total_length;              // 4 bytes: Total length of the data instance (without headers)
}; // Total size: 60 bytes

#pragma pack(pop)

// --- 3. Parsing Function ---

void process_file_content(const std::vector<unsigned char>& data, const std::string& filename) {
    
    // Minimum expected size: 24 (UDP Datagram Header) + 60 (SICK Header) = 84 bytes
    constexpr size_t MIN_SIZE = 84;
    constexpr size_t UDP_HEADER_SIZE = 24;

    if (data.size() < MIN_SIZE) {
        std::cerr << "  [ERROR] File too small to contain a valid packet (" << data.size() << " bytes)." << std::endl;
        return;
    }

    // --- 3.1. Get Header Info (Scan ID and Measurement Data Offset) ---
    
    // The SICK Data Output Header starts after the 24-byte UDP Datagram Header.
    const unsigned char* header_ptr = data.data() + UDP_HEADER_SIZE;
    
    // Use memcpy to safely copy data into the packed structure
    SICK_DataOutput_Header header;
    std::memcpy(&header, header_ptr, sizeof(SICK_DataOutput_Header));

    // Get the Scan ID (Scan Number) and convert from Little Endian
    uint32_t scan_id = le_to_h_u32(header.scan_num);
    
    // Get the offset to the Measurement Data block (offset is relative to the start of the SICK Header)
    uint32_t measurement_data_offset = le_to_h_u32(header.offset_measurement_data);

    std::cout << "\n  [Metadata] Scan Identification (Scan Number): " << scan_id << std::endl;

    if (measurement_data_offset == 0) {
        std::cout << "  [INFO] Measurement Data block not present (offset is 0)." << std::endl;
        return;
    }

    // --- 3.2. Locate and Process Measurement Data Block ---
    
    // The Measurement Data block starts at: 
    // UDP Header (24) + SICK Header (0) + Measurement Data Offset
    size_t data_block_start_index = UDP_HEADER_SIZE + measurement_data_offset;

    if (data_block_start_index + 4 >= data.size()) {
        std::cerr << "  [ERROR] Measurement Data offset is out of bounds." << std::endl;
        return;
    }

    // The first 4 bytes of the block are the Length of the data field.
    const unsigned char* block_ptr = data.data() + data_block_start_index;
    uint32_t block_length;
    
    // Use memcpy for safe access to the Little Endian length field
    std::memcpy(&block_length, block_ptr, 4);
    block_length = le_to_h_u32(block_length); 

    std::cout << "  [Data] Measurement Data Block Found (Length: " << block_length << " bytes)." << std::endl;

    // Measurement data starts 4 bytes after the block start (after the Length field)
    const unsigned char* data_ptr = block_ptr + 4;
    size_t current_byte_index = data_block_start_index + 4;
    size_t points_processed = 0;

    // Each data point is 4 bytes: 2 bytes for Distance/Status + 2 bytes for RSSI/Intensity 
    constexpr size_t BYTES_PER_POINT = 4; 

    // Loop through the data points
    while (current_byte_index + BYTES_PER_POINT <= data_block_start_index + 4 + block_length) {
        
        // --- Distance and Status (2 bytes, Little Endian) ---
        uint16_t dist_status_le;
        std::memcpy(&dist_status_le, data_ptr, 2);
        uint16_t dist_status = le_to_h_u16(dist_status_le);
        
        // Distance is in the lower 13 bits (0x1FFF). Factor is 1 for mm.
        uint16_t distance_mm = dist_status & 0x1FFF; 
        
        // Status is in the upper 3 bits (0xE000). (Typically for error/reflectivity/validity)
        uint8_t status_flags = (dist_status >> 13) & 0x07; 

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

        // Stop after 20 points to prevent excessive output for large files
        if (points_processed >= 20) {
            std::cout << "    [...] Showing first 20 data points only." << std::endl;
            break;
        }
    }
}

// --- 4. Main Program Loop (Modified from previous step) ---

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
                // Load entire file content into a vector of unsigned chars
                std::vector<unsigned char> data(
                    (std::istreambuf_iterator<char>(file)), 
                    std::istreambuf_iterator<char>()
                );
                
                if (!data.empty()) {
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
