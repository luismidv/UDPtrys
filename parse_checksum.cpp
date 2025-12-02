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
#include <cmath> // For std::pow in CRC table generation

// Define the number of packets to stack before parsing
constexpr int PACKETS_TO_STACK = 3; 
// Checksum is assumed to be 4 bytes (CRC32) at the end of the data.
constexpr size_t CHECKSUM_SIZE = 4;
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

// --- 2. Checksum Utility (CRC32 Implementation) ---

// Pre-calculated CRC table (static initialization for performance)
static uint32_t crc32_table[256];

/**
 * @brief Initializes the CRC32 lookup table using the standard polynomial.
 * (IEEE 802.3 / Ethernet, polynomial 0xEDB88320).
 */
void initialize_crc32_table() {
    uint32_t polynomial = 0xEDB88320; // Reversed polynomial
    for (int i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) {
                c = polynomial ^ (c >> 1);
            } else {
                c >>= 1;
            }
        }
        crc32_table[i] = c;
    }
}

/**
 * @brief Calculates the CRC32 checksum for a given block of data.
 * @param data Pointer to the start of the data.
 * @param length The length of the data to include in the checksum calculation.
 * @return The calculated 32-bit CRC.
 */
uint32_t calculate_crc32(const unsigned char* data, size_t length) {
    // Initial value for CRC32 (common for Ethernet/IEEE 802.3)
    uint32_t crc = 0xFFFFFFFF; 

    for (size_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }

    // Final XOR value (common for Ethernet/IEEE 802.3)
    return crc ^ 0xFFFFFFFF; 
}


/**
 * @brief Verifies the integrity of a single UDP packet using CRC32.
 * @param packet_data The raw received packet data.
 * @param total_size The total received size (including data and checksum).
 * @return true if the calculated CRC matches the CRC in the packet, false otherwise.
 */
bool verify_checksum(const unsigned char* packet_data, size_t total_size) {
    // 1. Check if the packet is large enough to contain the checksum
    if (total_size < CHECKSUM_SIZE) {
        std::cerr << "  [FAIL] Packet size (" << total_size << " bytes) is smaller than checksum size (" << CHECKSUM_SIZE << " bytes)." << std::endl;
        return false;
    }

    // 2. Determine the length of the data that contributes to the checksum
    // This assumes the checksum covers everything *except* the checksum itself.
    size_t data_length = total_size - CHECKSUM_SIZE;

    // 3. Calculate the CRC32 for the data portion
    uint32_t calculated_crc = calculate_crc32(packet_data, data_length);

    // 4. Extract the received CRC (Little Endian conversion assumed)
    uint32_t received_crc_le;
    std::memcpy(&received_crc_le, packet_data + data_length, CHECKSUM_SIZE);
    uint32_t received_crc = le_to_h_u32(received_crc_le);

    // 5. Compare
    if (calculated_crc == received_crc) {
        // std::cout << "  [PASS] Checksum verified successfully." << std::endl;
        return true;
    } else {
        std::cerr << "  [FAIL] Checksum Mismatch! Calculated: 0x" << std::hex << calculated_crc 
                  << ", Received: 0x" << received_crc << std::dec << ". DROPPING PACKET." << std::endl;
        return false;
    }
}

// --- 3. Data Structure Definitions (Packed) (Unchanged) ---
#pragma pack(push, 1)

// SICK_DataOutput_Header definition... (omitted for brevity, assume it is present)

struct SICK_DataOutput_Header {
    uint8_t version[4];
    uint32_t device_sn;
    uint32_t system_plug_sn;
    uint8_t channel_num;
    uint8_t reserved_1[3];
    uint32_t sequence_num;
    uint32_t scan_num;
    uint32_t timestamp_sec;
    uint32_t timestamp_usec;
    uint8_t remaining_header[60 - 32];
}; 

#pragma pack(pop)

// --- 4. Parsing Function (Minor change to account for Checksum) ---

/**
 * @brief Parses the concatenated data from multiple UDP packets.
 * * NOTE: When stacking, we assume the checksum bytes from the individual packets 
 * are carried into the stack and must be accounted for in the total data size.
 */
void process_packet_stack(const std::vector<unsigned char>& data, int total_packets) {
    
    std::cout << "\n========================================================" << std::endl;
    std::cout << "Starting Parsing of " << total_packets << " stacked packets (Total size: " 
              << data.size() << " bytes)." << std::endl;
    
    // We assume the parsing logic is applied to the *first* packet's header.
    constexpr size_t CUSTOM_PREAMBLE_SIZE = 20; 
    constexpr size_t SICK_HEADER_SIZE = 60;
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
    
    // The total number of checksum bytes included in the stacked data block.
    // Each of the 'total_packets' contributed one CHECKSUM_SIZE block.
    size_t total_checksum_bytes = total_packets * CHECKSUM_SIZE;

    // The actual measurement data (points) is the total payload minus all checksums.
    size_t total_measurement_data_bytes = total_payload_bytes - total_checksum_bytes;
    
    constexpr size_t BYTES_PER_POINT = 4; // 2 bytes Distance/Status + 2 bytes RSSI/Intensity

    if (total_measurement_data_bytes % BYTES_PER_POINT != 0) {
        std::cerr << "  [WARNING] Remaining data size (" << total_measurement_data_bytes 
                  << " bytes) is not perfectly divisible by 4. Parsing may be incomplete." << std::endl;
    }
    
    size_t total_points = total_measurement_data_bytes / BYTES_PER_POINT;
    
    std::cout << "  [Data] Total Data Block Size (with all checksums): " << total_payload_bytes << " bytes." << std::endl;
    std::cout << "  [Data] Total Measurement Points: " << total_points << std::endl;

    // --- 4.3. Process Measurement Data ---
    
    const unsigned char* data_ptr = data.data() + FIRST_HEADER_END_INDEX;
    size_t points_processed = 0;
    size_t data_bytes_remaining = total_measurement_data_bytes;
    int packet_counter = 0;

    // Loop through the data points in the combined stack
    while (points_processed < total_points) {
        
        // Check if we hit the end of a single packet's measurement data block
        // Assuming all packets are the same size, this is complex to calculate accurately
        // without knowing the fixed size of a single packet's data block.
        // A simpler approach is to loop through the points, and skip the checksum 
        // block *if* we know the fixed packet size.
        // For simplicity, we just process all `total_points` consecutively.

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
        data_bytes_remaining -= BYTES_PER_POINT;

        // CRITICAL: Skip the checksum bytes when moving from one packet's data to the next.
        // This is complex without knowing the fixed data size per packet.
        // Assuming *each* packet has N data bytes followed by 4 checksum bytes:
        if (data_bytes_remaining > 0 && 
            (data_ptr - data.data() - FIRST_HEADER_END_INDEX) % (data.size() / total_packets) == 0) {
            
            // This logic is simplified and may need tuning based on exact packet size.
            // We assume the data block of one packet is followed immediately by the checksum bytes of that packet.
            data_ptr += CHECKSUM_SIZE; 
            packet_counter++; // This skip marks the start of the next packet's data block
            
            // Print a separator to show packet boundaries in the stacked data
            if (packet_counter < total_packets) {
                std::cout << "    ------------------ (Skipping Checksum " << packet_counter << ") ------------------" << std::endl;
            }
        }


        // Stop after 20 points for brevity
        if (points_processed >= 20) {
            std::cout << "    [...]" << std::endl;
            break;
        }
    }
    std::cout << "========================================================" << std::endl;
}

// --- 5. Main Program Loop (UDP Listening, Checksum, and Stacking) ---

int main() {
    using clock = std::chrono::high_resolution_clock;
    const int PORT = 1217;
    
    // Initialize the CRC32 lookup table once
    initialize_crc32_table();

    // 5.1. Setup Socket
    // ... (Socket setup code is the same as before) ...
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
    // ... (End of socket setup) ...


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

            // --- 5.2.1. Checksum Verification ---
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
                // Checksum failed, drop the packet as requested.
                droppedCounter++;
                // Continue to the next iteration, skipping stack and parsing logic.
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
        
        // Add a small delay if needed to reduce CPU usage in a non-blocking loop
        // usleep(1000); // 1 ms sleep
    }
    
    close(sockfd);
    return 0;
}
