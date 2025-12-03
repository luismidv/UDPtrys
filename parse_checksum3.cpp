 #include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <arpa/inet.h>
#include <unistd.h>
#include "fcntl.h"
#include <chrono>

// Define the number of packets to stack before parsing
constexpr int PACKETS_TO_STACK = 3; 
// Checksum is a single-byte XOR sum at the end of the packet.
constexpr size_t CHECKSUM_SIZE = 1;
// Maximum expected packet size including data and checksum
constexpr size_t MAX_PACKET_SIZE = 2048; 

// --- 1. Utility for Little Endian to Host Conversion (Unchanged) ---

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

// --- 2. Checksum Utility (Single-Byte XOR Implementation) ---

/**
 * @brief Calculates the single-byte XOR checksum for a given block of data.
 * The XOR sum is calculated over the entire data range.
 * @param data Pointer to the start of the data.
 * @param length The length of the data to include in the checksum calculation.
 * @return The calculated 8-bit XOR checksum.
 */
uint8_t calculate_xor_checksum(const unsigned char* data, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}


/**
 * @brief Verifies the integrity of a single UDP packet using the XOR checksum.
 * The checksum covers the entire payload excluding the final checksum byte.
 * @param packet_data The raw received packet data.
 * @param total_size The total received size (including data and checksum).
 * @return true if the calculated XOR matches the XOR in the packet, false otherwise.
 */
bool verify_checksum(const unsigned char* packet_data, size_t total_size) {
    // 1. Check size: need at least 1 byte for the checksum itself.
    if (total_size < CHECKSUM_SIZE) {
        std::cerr << "  [FAIL] Packet size (" << total_size << " bytes) is too small." << std::endl;
        return false;
    }

    // 2. Determine the length of the data that contributes to the checksum
    // This covers ALL bytes from the start up to the byte before the checksum.
    size_t data_length = total_size - CHECKSUM_SIZE;

    // 3. Calculate the XOR checksum for the data portion
    uint8_t calculated_checksum = calculate_xor_checksum(packet_data, data_length);

    // 4. Extract the received checksum (it's the very last byte)
    uint8_t received_checksum = packet_data[data_length];

    // 5. Compare
    if (calculated_checksum == received_checksum) {
        // std::cout << "  [PASS] Checksum verified successfully." << std::endl;
        return true;
    } else {
        std::cerr << "  [FAIL] Checksum Mismatch! Calculated: 0x" << std::hex 
                  << (int)calculated_checksum << ", Received: 0x" << (int)received_checksum 
                  << std::dec << ". DROPPING PACKET." << std::endl;
        return false;
    }
}

// --- 3. Data Structure Definitions (Packed) (Unchanged) ---
#pragma pack(push, 1)

// SICK_DataOutput_Header definition... (60 bytes total)
struct SICK_DataOutput_Header {
    uint8_t version[4];
    uint32_t device_sn;
    uint32_t system_plug_sn;
    uint8_t channel_num;
    uint8_t reserved_1[3];
    uint32_t sequence_num;
    uint32_t scan_num; // Scan ID
    uint32_t timestamp_sec;
    uint32_t timestamp_usec;
    uint8_t remaining_header[60 - 32];
}; 

#pragma pack(pop)

// --- 4. Parsing Function (Adjusted for 1-byte Checksum) ---

/**
 * @brief Parses the concatenated data from multiple UDP packets.
 */
void process_packet_stack(const std::vector<unsigned char>& data, int total_packets) {
    
    std::cout << "\n========================================================" << std::endl;
    std::cout << "Starting Parsing of " << total_packets << " stacked packets (Total size: " 
              << data.size() << " bytes)." << std::endl;
    
    // Header assumptions (based on previous code):
    constexpr size_t CUSTOM_PREAMBLE_SIZE = 20; 
    constexpr size_t SICK_HEADER_SIZE = 60;
    // Data starts after the first packet's SICK header
    constexpr size_t FIRST_HEADER_END_INDEX = CUSTOM_PREAMBLE_SIZE + SICK_HEADER_SIZE; // 80 bytes

    if (data.size() < FIRST_HEADER_END_INDEX) {
        std::cerr << "  [ERROR] Stacked buffer too small to contain a header." << std::endl;
        return;
    }

    // --- 4.1. Get Header Info ---
    const unsigned char* header_ptr = data.data() + CUSTOM_PREAMBLE_SIZE;
    SICK_DataOutput_Header header;
    std::memcpy(&header, header_ptr, sizeof(SICK_DataOutput_Header));
    uint32_t scan_id = le_to_h_u32(header.scan_num);
    std::cout << "  [Metadata] Scan Identification (Scan Number from first packet): " << scan_id << std::endl;

    // --- 4.2. Calculate Measurement Data Length for the *entire stack* ---
    
    // Total size of data PLUS the checksums from all packets.
    size_t total_payload_bytes = data.size() - FIRST_HEADER_END_INDEX; 
    
    // Total number of checksum bytes included in the stacked data block (1 byte per packet).
    size_t total_checksum_bytes = total_packets * CHECKSUM_SIZE; // e.g., 3 * 1 = 3 bytes

    // The actual measurement data (points) is the total payload minus all checksums.
    size_t total_measurement_data_bytes = total_payload_bytes - total_checksum_bytes;
    
    constexpr size_t BYTES_PER_POINT = 4; // 2 bytes Distance/Status + 2 bytes RSSI/Intensity

    if (total_measurement_data_bytes % BYTES_PER_POINT != 0) {
        std::cerr << "  [WARNING] Remaining data size (" << total_measurement_data_bytes 
                  << " bytes) is not perfectly divisible by 4. Parsing may be incomplete." << std::endl;
    }
    
    size_t total_points = total_measurement_data_bytes / BYTES_PER_POINT;
    
    std::cout << "  [Data] Total Measurement Points: " << total_points << std::endl;

    // --- 4.3. Process Measurement Data ---
    
    const unsigned char* data_ptr = data.data() + FIRST_HEADER_END_INDEX;
    size_t points_processed = 0;
    
    // The measurement data size for the first packet (based on your 1460 byte sample):
    // 1460 (total) - 80 (header) - 1 (checksum) = 1379 bytes
    // For simplicity, we track the offset relative to the start of the data payload (index 80).
    size_t data_payload_offset = 0;
    
    // The exact size of the measurement data for a single fragment is needed to correctly skip checksums.
    // Based on the 1460-byte fragment, the data chunk size is 1379 bytes. 
    constexpr size_t FIRST_PACKET_DATA_SIZE = 1379; // 1460 - 80 - 1
    // Assuming subsequent packets are smaller, e.g., max UDP size (1500) - 1 byte checksum, but we don't know the exact size of fragment 2 and 3.
    // For now, we assume a simple structure that repeats after the first header.

    // Loop through the data points in the combined stack
    while (points_processed < total_points) {
        
        // --- Distance and Status (2 bytes, Little Endian) ---
        uint16_t dist_status_le;
        std::memcpy(&dist_status_le, data_ptr, 2);
        uint16_t dist_status = le_to_h_u16(dist_status_le);
        
        uint16_t distance_mm = dist_status & 0x1FFF;
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
        data_payload_offset += BYTES_PER_POINT;
        points_processed++;

        // CRITICAL: Skip the checksum bytes when moving from one packet's data to the next.
        // This is complex and relies on knowing the exact size of the data chunks in each fragment.
        
        // For simplicity, this logic assumes the subsequent data fragments are also 1379 bytes long
        // if they are full-sized, which might not be true if they don't contain the 80-byte header.
        
        // Skip after the first packet's data chunk (1379 bytes)
        if (data_payload_offset == FIRST_PACKET_DATA_SIZE) {
            data_ptr += CHECKSUM_SIZE; // Skip the 1-byte checksum
            data_payload_offset += CHECKSUM_SIZE; // Adjusted offset
            std::cout << "    ------------------ (Skipping 1-byte Checksum 1) ------------------" << std::endl;
        }
        // Skip after the second packet's data chunk (assuming subsequent chunks are full 1460 bytes - 1 checksum = 1459 bytes total payload)
        // This logic is fragile but shows how to handle the skip based on fixed sizes.
        else if (data_payload_offset == FIRST_PACKET_DATA_SIZE + (data.size() / total_packets - CHECKSUM_SIZE)) {
            data_ptr += CHECKSUM_SIZE; // Skip the 1-byte checksum
            data_payload_offset += CHECKSUM_SIZE;
            std::cout << "    ------------------ (Skipping 1-byte Checksum 2) ------------------" << std::endl;
        }

        // Stop after 20 points for brevity
        if (points_processed >= 20) {
            std::cout << "    [...] Showing first 20 data points (Total " << total_points << " points)." << std::endl;
            break;
        }
    }
    std::cout << "========================================================" << std::endl;
}

// --- 5. Main Program Loop (UDP Listening, Checksum, and Stacking) ---

int main() {
    using clock = std::chrono::high_resolution_clock;
    const int PORT = 1217;
    
    // 5.1. Setup Socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { std::cerr << "Error: Could not create socket." << std::endl; return 1; }
    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    int rcvbuf = 64 * 1024 * 1024;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) { std::cerr << "Error: Could not bind to port " << PORT << std::endl; close(sockfd); return 1; }
    if (fcntl(sockfd, F_SETFL, O_NONBLOCK) < 0) { std::cerr << "Error: Could not set non-blocking mode." << std::endl; close(sockfd); return 1; }


    std::cout << "--- Starting Lidar Packet Listener ---" << std::endl;
    std::cout << "Listening for UDP packets on port " << PORT 
              << ". Will stack " << PACKETS_TO_STACK << " *verified* packets before parsing." << std::endl;
    
    // 5.2. Listening, Checksum, and Stacking Loop
    
    std::vector<unsigned char> current_packet_stack;
    int packets_in_stack = 0;
    
    char packet_buffer[MAX_PACKET_SIZE]; 
    long packetCounter = 0; // Total packets received
    long droppedCounter = 0; // Dropped packets due to checksum

    auto start_time = clock::now(); 

    while (true) {
        sockaddr_in sender;
        socklen_t senderLen = sizeof(sender);

        ssize_t received_bytes = recvfrom(sockfd, packet_buffer, MAX_PACKET_SIZE, 0,
                                          (sockaddr*)&sender, &senderLen);

        if (received_bytes > 0) {
            packetCounter++;

            // --- 5.2.1. Checksum Verification (XOR Sum) ---
            if (verify_checksum((unsigned char*)packet_buffer, received_bytes)) {
                
                // --- 5.2.2. Stack the verified packet data ---
                current_packet_stack.insert(
                    current_packet_stack.end(),
                    (unsigned char*)packet_buffer, 
                    (unsigned char*)packet_buffer + received_bytes
                );
                packets_in_stack++;

                // --- 5.2.3. Check if stacking limit is reached ---
                if (packets_in_stack >= PACKETS_TO_STACK) {
                    process_packet_stack(current_packet_stack, packets_in_stack);
                    
                    // Reset the stack for the next batch
                    current_packet_stack.clear();
                    packets_in_stack = 0;
                }
            } else {
                droppedCounter++;
            }

            // --- 5.2.4. Performance Monitoring ---
            if (packetCounter % 500 == 0) {
                auto now = clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

                std::cout << "[INFO] Received a total of " << packetCounter
                          << " packets (" << droppedCounter << " dropped) in " 
                          << elapsed.count() << " ms\n";

                start_time = clock::now();
            }
        } else if (received_bytes < 0 && (errno != EWOULDBLOCK && errno != EAGAIN)) {
            std::cerr << "Error in recvfrom: " << strerror(errno) << std::endl;
            break;
        } 
    }
    
    close(sockfd);
    return 0;
}
