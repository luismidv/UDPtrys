#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <iomanip>
#include <vector>
#include <sstream>

// --- 1. CRC16 CALCULATION (USING CRC-CCITT-KERMIT) ---

/**
 * @brief Calculates the CRC-CCITT (Kermit/X.25) checksum.
 * WARNING: The exact SICK CRC algorithm (polynomial, initial value, reflection)
 * must be verified against your MicroScan3 technical manual.
 * This implementation is a standard variant (0x1021 poly, 0x0000 init).
 */
unsigned short crc16_ccitt(const char *data, size_t length) {
    unsigned short crc = 0x0000;
    unsigned short poly = 0x1021; 

    for (size_t i = 0; i < length; i++) {
        crc ^= (unsigned char)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ poly;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// --- 2. STRUCTURE DEFINITION FOR ASSEMBLED DATA HEADER ---

// NOTE: The CRC is the last field of the "Structure Header" within the *assembled* data.
// We are using a 20-byte placeholder size for the Structure Header for reliable access.
// Offset 18 (20 bytes - 2 bytes for CRC) is the assumed position of the CRC.
#pragma pack(push, 1) // Ensure no padding
struct MicroScan3Header {
    uint16_t u16StartWord;    // Offset 0
    uint16_t u16Version;      // Offset 2
    uint32_t u32TotalLength;  // Offset 4 (Length of data block)
    char padding[10];         // Placeholder for Time, NTPTime, etc.
    uint16_t u16CRC16;        // Offset 18 (Last 2 bytes of the Structure Header)
};
#pragma pack(pop)

// --- 3. CHECKSUM VERIFICATION FUNCTION ---

bool check_checksum(const std::vector<char>& measurementData) {
    const size_t HEADER_SIZE = 20; // Assumed size of Structure Header
    
    if (measurementData.size() < HEADER_SIZE) {
        std::cerr << "Error: Reassembled measurement is too small to contain header.\n";
        return false;
    }

    // 1. EXTRACT EXPECTED CRC VALUE
    const MicroScan3Header* header = (const MicroScan3Header*)measurementData.data();
    unsigned short expected_crc = header->u16CRC16; 

    // 2. DEFINE THE DATA RANGE FOR CRC CALCULATION
    // The CRC is calculated over the *entire assembled data* EXCEPT for the final two bytes
    // (the u16CRC16 field itself).
    size_t crc_calc_length = measurementData.size() - sizeof(uint16_t);

    // 3. CALCULATE CRC
    unsigned short calculated_crc = crc16_ccitt(measurementData.data(), crc_calc_length);

    // 4. COMPARE
    if (calculated_crc == expected_crc) {
        std::cout << "Checksum OK. Expected: 0x" << std::hex << expected_crc 
                  << ", Calculated: 0x" << calculated_crc << std::dec << ".\n";
        return true;
    } else {
        std::cerr << "Checksum FAILED! Expected: 0x" << std::hex << expected_crc 
                  << ", Calculated: 0x" << calculated_crc << std::dec << ".\n";
        return false;
    }
}

int main() {
    // The port your MicroScan3 sends data to (default is often 2112 or similar, check config!)
    const int PORT = 1217; 
    const size_t UDP_HEADER_SIZE = 24; // Bytes of the 'MS3 MD...' header in each packet

    // Create socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket\n";
        return -1;
    }

    // Bind to all network interfaces on PORT
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind\n";
        close(sockfd);
        return -1;
    }

    std::cout << "Listening for UDP packets on port " << PORT << "...\n";

    // Buffer for incoming data
    const int BUFFER_SIZE = 2048;
    char buffer[BUFFER_SIZE];
    
    int packetCounter = 0;
    int fragmentCounter = 0;
    std::vector<char> measurementData; // stores the assembled data payload

    const std::string folder = "trys/"; // Make sure this folder exists
    while (true) {
        sockaddr_in senderAddr;
        socklen_t senderLen = sizeof(senderAddr);

        // Receive packet
        ssize_t received = recvfrom(
            sockfd,
            buffer,
            BUFFER_SIZE,
            0,
            (struct sockaddr*)&senderAddr,
            &senderLen
        );

        if (received < 0) {
            std::cerr << "Error receiving data\n";
            continue;
        }
        
        // --- CRITICAL CORRECTION: Skip the 24-byte UDP header ---
        if (received > UDP_HEADER_SIZE) {
            size_t payload_size = received - UDP_HEADER_SIZE;

            // Append ONLY the data payload, skipping the 24-byte header
            measurementData.insert(measurementData.end(), 
                                   buffer + UDP_HEADER_SIZE, 
                                   buffer + received);
            fragmentCounter++;
            
            std::cout << "Received fragment #" << fragmentCounter 
                      << " (Payload size: " << payload_size 
                      << " bytes)\n";
        } else {
            std::cerr << "Warning: Received packet too small to be a data fragment. Discarding.\n";
            continue;
        }
        // --------------------------------------------------------

        if (fragmentCounter == 3)
        {
            // --- NEW: CHECKSUM VERIFICATION ---
            bool is_checksum_valid = check_checksum(measurementData);
            // ------------------------------------

            if (is_checksum_valid) 
            {
                static int measurementIndex = 0;

                std::ostringstream filename;
                filename << folder << "measurement_" << std::setw(4) << std::setfill('0') << measurementIndex << ".bin";
                std::ofstream outfile(filename.str(), std::ios::binary);
                // Write the assembled data payload to the binary file
                outfile.write(measurementData.data(), measurementData.size());
                outfile.close();

                std::cout << "✔ Saved FULL LiDAR measurement #" << measurementIndex
                          << " (size = " << measurementData.size()
                          << " bytes) as " << filename.str() << std::endl;

                measurementIndex++;
            } else {
                std::cerr << "X Discarding corrupted measurement due to failed checksum.\n";
            }

            // Reset for next LiDAR measurement
            fragmentCounter = 0;
            measurementData.clear();
        }
        packetCounter++;
    }

    close(sockfd);
    return 0;
}