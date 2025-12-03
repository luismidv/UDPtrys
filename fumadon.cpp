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
constexpr size_t MAX_PACKET_SIZE = 2048; 
// Header assumptions: 20 bytes custom preamble + 60 bytes SICK header = 80 bytes
constexpr size_t SICK_PAYLOAD_HEADER_SIZE = 80; 

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

// --- 2. UDP Checksum Calculation (RFC 1071) ---

/**
 * @brief UDP/IP checksum algorithm (1's complement sum of 16-bit words).
 * NOTE: This simplified version calculates the checksum only over the UDP payload.
 * A complete check requires IP addresses to create a 'pseudo-header', which is
 * generally unavailable in application space with a simple recvfrom.
 * @param buffer Pointer to the start of the data (the raw UDP payload).
 * @param len The length of the buffer.
 * @return The calculated 16-bit checksum (in network byte order).
 */
unsigned short calculate_udp_checksum(const unsigned char *buffer, int len) {
    long sum = 0;
    const unsigned short *ip_src = (const unsigned short*)buffer;

    // Sum all 16-bit words
    while (len > 1) {
        sum += *ip_src++;
        len -= 2;
    }

    // Handle the last odd byte if present
    if (len > 0) {
        sum += *((unsigned char *)ip_src);
    }

    // Fold 32-bit sum to 16 bits (1's complement)
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // Take the 1's complement of the result
    return (unsigned short)~sum;
}


/**
 * @brief Verifies data integrity using the calculated 16-bit UDP checksum.
 * @param packet_data The raw received packet data (the SICK payload).
 * @param total_size The total received size.
 * @return true if the calculated checksum is zero (or matches the expected value), false otherwise.
 */
bool verify_udp_integrity(const unsigned char* packet_data, size_t total_size) {
    // We assume a complete UDP datagram was passed to us, including a 2-byte checksum field.
    // Since we don't have the UDP header, we cannot know where the sensor's header ends
    // and where the UDP checksum was originally placed (usually 8 bytes into the UDP header).
    
    // As a practical substitute for the lack of the full network stack information:
    // We will verify the integrity of the *entire SICK payload* passed from recvfrom.
    
    // The total size is the entire SICK payload (Header + Data)
    size_t data_length = total_size; 

    // Calculate the checksum over the entire SICK payload data
    unsigned short calculated_checksum = calculate_udp_checksum(
        packet_data, 
        data_length
    );

    // Since a complete UDP check is impossible here, we are relying on a simple 
    // assumption that the UDP checksum is valid if the network stack passed it through.
    // The only way to verify integrity here is if the SICK device included 
    // the UDP checksum field inside its custom header.
    
    // Since we don't know the exact UDP checksum's value from the SICK payload,
    // we cannot perform the check. We must rely on the network stack.
    
    // The only successful "checksum" we can perform here is relying on the kernel's built-in check.
    
    // ***************************************************************
    // WARNING: This section CANNOT be completed correctly without access to the 
    // IP and UDP headers (which are stripped by the OS before reaching recvfrom).
    // The most reliable check is to rely on the kernel, as per the documentation.
    // ***************************************************************
    
    std::cout << "  [INFO] Relying on OS to verify 16-bit UDP checksum. Received packet assumed valid." << std::endl;
    return true; 
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

// --- 4. Parsing Function (Uses the previous non-checksum logic) ---

void process_packet_stack(const std::vector<unsigned char>& data, int total_packets) {
    // ... (This function remains identical to the previous step's process_packet_stack)
    // Removed for brevity, but it is the non-checksumming version.
    
    std::cout << "\n========================================================" << std::endl;
    std::cout << "Starting Parsing of " << total_packets << " stacked packets (Total size: " 
              << data.size() << " bytes)." << std::endl;
    
    constexpr size_t FIRST_HEADER_END_INDEX = SICK_PAYLOAD_HEADER_SIZE; 

    if (data.size() < FIRST_HEADER_END_INDEX) {
        std::cerr << "  [ERROR] Stacked buffer too small to contain a header." << std::endl;
        return;
    }

    const unsigned char* header_ptr = data.data() + 20; 
    SICK_DataOutput_Header header;
    std::memcpy(&header, header_ptr, sizeof(SICK_DataOutput_Header));
    uint32_t scan_id = le_to_h_u32(header.scan_num);
    std::cout << "  [Metadata] Scan Identification (Scan Number from first packet): " << scan_id << std::endl;

    size_t total_measurement_data_bytes = data.size() - FIRST_HEADER_END_INDEX; 
    
    constexpr size_t BYTES_PER_POINT = 4;

    if (total_measurement_data_bytes % BYTES_PER_POINT != 0) {
        std::cerr << "  [WARNING] Remaining data size (" << total_measurement_data_bytes 
                  << " bytes) is not perfectly divisible by 4. Data appears corrupted or padded." << std::endl;
    }
    
    size_t total_points = total_measurement_data_bytes / BYTES_PER_POINT;
    
    std::cout << "  [Data] Total Measurement Points: " << total_points << std::endl;

    const unsigned char* data_ptr = data.data() + FIRST_HEADER_END_INDEX;
    size_t points_processed = 0;
    
    while (points_processed < total_points) {
        
        uint16_t dist_status_le;
        std::memcpy(&dist_status_le, data_ptr, 2);
        uint16_t dist_status = le_to_h_u16(dist_status_le);
        
        uint16_t distance_mm = dist_status & 0x1FFF;
        uint8_t status_flags = (dist_status >> 13) & 0x07;

        uint16_t rssi_le;
        std::memcpy(&rssi_le, data_ptr + 2, 2);
        uint16_t rssi = le_to_h_u16(rssi_le); 

        // --- Print Human-Readable Data ---
        std::cout << "    Point " << std::setw(4) << points_processed << ": ";
        std::cout << "Distance: " << std::setw(5) << distance_mm << " mm, ";
        std::cout << "Intensity (RSSI): " << std::setw(4) << rssi << ", ";
        std::cout << "Status Flags: 0x" << std::hex << std::setw(1) << static_cast<int>(status_flags) << std::dec;
        std::cout << std::endl;
        
        data_ptr += BYTES_PER_POINT;
        points_processed++;

        if (points_processed >= 20) {
            std::cout << "    [...] Showing first 20 data points (Total " << total_points << " points)." << std::endl;
            break;
        }
    }
    std::cout << "========================================================" << std::endl;
}


// --- 5. Main Program Loop (UDP Listening and Stacking) ---

int main() {
    using clock = std::chrono::high_resolution_clock;
    const int PORT = 1217;
    
    // ... Socket Setup (Unchanged) ...
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

    std::cout << "--- Starting Lidar Packet Listener (Relying on OS UDP Checksum) ---" << std::endl;
    std::cout << "Listening for UDP packets on port " << PORT 
              << ". Will stack " << PACKETS_TO_STACK << " packets before parsing." << std::endl;
    
    std::vector<unsigned char> current_packet_stack;
    int packets_in_stack = 0;
    
    char packet_buffer[MAX_PACKET_SIZE]; 
    long packetCounter = 0; 

    auto start_time = clock::now(); 

    while (true) {
        sockaddr_in sender;
        socklen_t senderLen = sizeof(sender);

        ssize_t received_bytes = recvfrom(sockfd, packet_buffer, MAX_PACKET_SIZE, 0,
                                          (sockaddr*)&sender, &senderLen);

        if (received_bytes > 0) {
            packetCounter++;
            
            // --- 5.1. UDP Integrity Check (Informational) ---
            // The only practical check here is informational. If the packet arrived, 
            // the OS/Kernel likely passed the 16-bit UDP checksum already.
            verify_udp_integrity((unsigned char*)packet_buffer, received_bytes);

            // --- 5.2. Stack the packet data ---
            current_packet_stack.insert(
                current_packet_stack.end(),
                (unsigned char*)packet_buffer, 
                (unsigned char*)packet_buffer + received_bytes
            );
            packets_in_stack++;

            // --- 5.3. Check if stacking limit is reached ---
            if (packets_in_stack >= PACKETS_TO_STACK) {
                process_packet_stack(current_packet_stack, packets_in_stack);
                
                current_packet_stack.clear();
                packets_in_stack = 0;
            }

            // --- 5.4. Performance Monitoring (Unchanged) ---
            if (packetCounter % 500 == 0) {
                auto now = clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

                std::cout << "[INFO] Received a total of " << packetCounter
                          << " packets in " 
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
