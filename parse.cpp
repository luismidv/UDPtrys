#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <arpa/inet.h> // For socket programming
#include <unistd.h>    // For close()
#include "fcntl.h"     // For fcntl()
#include <chrono>      // For timing

// Define the number of packets to stack before parsing
constexpr int PACKETS_TO_STACK = 3; 

// --- 1. Utility for Little Endian to Host Conversion ---

inline uint32_t le_to_h_u32(uint32_t value) {
    // Standard cross-platform implementation using network/host byte order macros
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
    uint8_t version[4];        // 4 bytes | Struct Offset 0
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

// --- 3. Parsing Function (Adapted to accept a generic buffer) ---

/**
 * @brief Parses the concatenated data from multiple UDP packets.
 * * @param data The combined packet data.
 * @param total_packets The number of packets that were stacked.
 */
void process_packet_stack(const std::vector<unsigned char>& data, int total_packets) {
    
    std::cout << "\n========================================================" << std::endl;
    std::cout << "Starting Parsing of " << total_packets << " stacked packets (Total size: " 
              << data.size() << " bytes)." << std::endl;
    
    // We assume the stacked packets are just one long continuous buffer.
    // The parsing logic is applied to the *first* packet's header.
    
    constexpr size_t CUSTOM_PREAMBLE_SIZE = 20; 
    constexpr size_t SICK_HEADER_SIZE = 60;
    
    // CRITICAL FIX: The Measurement Data Block is assumed to immediately follow the header structure.
    // This assumes the first packet's structure holds true for all of them for concatenation.
    constexpr size_t DATA_BLOCK_START_INDEX = CUSTOM_PREAMBLE_SIZE + SICK_HEADER_SIZE; // 80 bytes

    // We only check the first packet's header for metadata like Scan ID.
    if (data.size() < DATA_BLOCK_START_INDEX) {
        std::cerr << "  [ERROR] Stacked buffer too small to contain a header." << std::endl;
        return;
    }

    // --- 3.1. Get Header Info (Read SICK Header starting at index 20 of the first packet) ---
    
    const unsigned char* header_ptr = data.data() + CUSTOM_PREAMBLE_SIZE;
    
    SICK_DataOutput_Header header;
    std::memcpy(&header, header_ptr, sizeof(SICK_DataOutput_Header));

    uint32_t scan_id = le_to_h_u32(header.scan_num);
    
    std::cout << "  [Metadata] Scan Identification (Scan Number from first packet): " << scan_id << std::endl;

    // --- 3.2. Calculate Measurement Data Length for the *entire stack* ---
    
    // The *effective* data size is the total size minus the header of the *first* packet.
    size_t total_data_bytes = data.size() - DATA_BLOCK_START_INDEX;
    constexpr size_t BYTES_PER_POINT = 4; // 2 bytes Distance/Status + 2 bytes RSSI/Intensity

    if (total_data_bytes % BYTES_PER_POINT != 0) {
        std::cerr << "  [WARNING] Remaining data size (" << total_data_bytes 
                  << " bytes) is not perfectly divisible by 4. Parsing may be incomplete." << std::endl;
    }
    
    size_t total_points = total_data_bytes / BYTES_PER_POINT;
    
    std::cout << "  [Data] Measurement Data starts at byte " << DATA_BLOCK_START_INDEX 
              << ". Total " << total_data_bytes << " bytes / " << total_points << " points in stack." << std::endl;

    // --- 3.3. Process Measurement Data ---
    
    const unsigned char* data_ptr = data.data() + DATA_BLOCK_START_INDEX;
    size_t points_processed = 0;

    // Loop through the data points in the combined stack
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

        // Stop after a reasonable number of points for brevity
        if (points_processed >= 20) {
            std::cout << "    [...]" << std::endl;
            // You can optionally break here or continue to process all points
        }
    }
    std::cout << "========================================================" << std::endl;
}

// --- 4. Main Program Loop (UDP Listening and Stacking) ---

int main() {
    using clock = std::chrono::high_resolution_clock;
    const int PORT = 1217;
    const size_t MAX_PACKET_SIZE = 2048; // Max buffer size for one packet

    // 4.1. Setup Socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Error: Could not create socket." << std::endl;
        return 1;
    }

    // Set socket options for reuse and buffer size
    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    int rcvbuf = 64 * 1024 * 1024; // 64 MB
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    // Bind the socket to the port
    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Error: Could not bind to port " << PORT << std::endl;
        close(sockfd);
        return 1;
    }

    // Set socket to non-blocking mode - important for continuous listening loop
    if (fcntl(sockfd, F_SETFL, O_NONBLOCK) < 0) {
        std::cerr << "Error: Could not set non-blocking mode." << std::endl;
        close(sockfd);
        return 1;
    }

    std::cout << "--- Starting Lidar Packet Listener ---" << std::endl;
    std::cout << "Listening for UDP packets on port " << PORT 
              << ". Will stack " << PACKETS_TO_STACK << " packets before parsing." << std::endl;
    
    // 4.2. Listening and Stacking Loop
    
    std::vector<unsigned char> current_packet_stack;
    int packets_in_stack = 0;
    
    // A temporary buffer to hold the data of the currently received packet
    char packet_buffer[MAX_PACKET_SIZE]; 
    long packetCounter = 0; // Total packets received

    auto start_time = clock::now(); // For performance monitoring

    while (true) {
        sockaddr_in sender;
        socklen_t senderLen = sizeof(sender);

        // Receive the UDP packet
        ssize_t received_bytes = recvfrom(sockfd, packet_buffer, MAX_PACKET_SIZE, 0,
                                          (sockaddr*)&sender, &senderLen);

        if (received_bytes > 0) {
            packetCounter++;

            // --- 4.2.1. Stack the packet data ---
            
            // Append the new packet's data to the end of the stacking buffer
            current_packet_stack.insert(
                current_packet_stack.end(),
                (unsigned char*)packet_buffer, 
                (unsigned char*)packet_buffer + received_bytes
            );
            packets_in_stack++;

            // --- 4.2.2. Check if stacking limit is reached ---
            if (packets_in_stack >= PACKETS_TO_STACK) {
                
                // --- Process the stacked buffer ---
                process_packet_stack(current_packet_stack, packets_in_stack);
                
                // --- Reset the stack for the next batch ---
                current_packet_stack.clear();
                packets_in_stack = 0;
            }

            // --- 4.2.3. Performance Monitoring (Every 500 total packets) ---
            if (packetCounter % 500 == 0) {
                auto now = clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

                std::cout << "[INFO] Received a total of " << packetCounter
                          << " packets in " << elapsed.count() << " ms\n";

                start_time = clock::now();  // restart timer for next 500
            }
        } else if (received_bytes < 0 && (errno != EWOULDBLOCK && errno != EAGAIN)) {
            // Check for a real error (not just EWOULDBLOCK/EAGAIN from non-blocking socket)
            std::cerr << "Error in recvfrom: " << strerror(errno) << std::endl;
            break;
        } 
        
        // In a non-blocking loop, you might add a small sleep here
        // to prevent 100% CPU usage if packets arrive slowly.
        // E.g., std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    close(sockfd);
    return 0;
}
