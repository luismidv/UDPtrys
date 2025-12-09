#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <chrono> // For time tracking
#include <fcntl.h>
#include <cerrno>
#include <cmath>
#include <vector> // For std::round or other math operations

// Original function: Searches for the FF07 sequence anywhere within the buffer.
std::vector<uint8_t> measureBuffer;
bool capturing = false;

bool containsFF07(const char* buf, ssize_t len) {
    if (len < 2) {
        return false;
    }
    for (int i = 0; i < len - 1; i++) {
        if ((unsigned char)buf[i] == 0xFF &&
            (unsigned char)buf[i + 1] == 0x07) {
            return true;
        }
    }
    return false;
}

bool startsWithFF07(const char* buf, ssize_t len) {
    for (int i = 0; i < len - 1; i++) {
        if ((uint8_t)buf[i] == 0xFF &&
            (uint8_t)buf[i+1] == 0x07)
            return true;
    }
    return false;
}

bool endsWith0029(const std::vector<uint8_t>& v) {
    int n = v.size();
    return (n >= 2 && v[n-2] == 0x00 && v[n-1] == 0x29);
}

int main() {
    using clock = std::chrono::high_resolution_clock;
    
    // --- Configuration ---
    const int PORT = 1217;
    const int PACKETS_PER_MEASURE = 5;
    const auto DURATION_LIMIT = std::chrono::minutes(5); 

    // --- Socket Setup (omitted error checking for brevity, assuming success) ---
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    int rcvbuf = 64 * 1024 * 1024;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    bind(sockfd, (sockaddr*)&addr, sizeof(addr));
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    char buffer[2048];

    // --- Counters ---
    long packetCounter = 0; // Total packets RECEIVED
    long measureCounter = 0; // Total measures/scans COMPLETED (FF07 seen)
    // -1: Waiting for the start (FF07) packet. 1 to 5: Measurement in progress.
    int packetsInCurrentMeasure = -1; 
    
    // --- Timing Variables ---
    auto startTime = clock::now();
    
    std::cout << "Listening on port " << PORT << "...\n";
    std::cout << "Monitoring for " << std::chrono::duration_cast<std::chrono::seconds>(DURATION_LIMIT).count() 
              << " seconds (" << PACKETS_PER_MEASURE << " packets per measure).\n\n";

    // --- State-based processing ---
    while (true) {
        // 1. Check Time Limit
        auto currentTime = clock::now();
        if (currentTime - startTime >= DURATION_LIMIT) {
            std::cout << "\n\n--- 5-MINUTE MONITORING COMPLETE ---\n";
            break; // Exit the loop
        }
        
        sockaddr_in sender;
        socklen_t senderLen = sizeof(sender);

        ssize_t received =
            recvfrom(sockfd, buffer, sizeof(buffer), 0,
                     (sockaddr*)&sender, &senderLen);

    if (received > 0) {
        packetCounter++;

        // Convert buffer to uint8_t stream
        const uint8_t* data = (const uint8_t*)buffer;

        for (ssize_t i = 0; i < received; i++) {

            // Detect FF 07 when we are not currently capturing
            if (!capturing &&
                i < received - 1 &&
                data[i] == 0xFF &&
                data[i + 1] == 0x07)
            {
                capturing = true;
                measureBuffer.clear();
                measureBuffer.push_back(0xFF);
                measureBuffer.push_back(0x07);
                i++;    // Skip extra byte since we consumed two
                continue;
            }

            // If capturing, append all bytes until we see 00 29
            if (capturing) {
                measureBuffer.push_back(data[i]);

            // Check end marker
            if (endsWith0029(measureBuffer)) {
                measureCounter++;

                std::cout << "Measurement #" << measureCounter
                          << " captured: " << measureBuffer.size()
                          << " bytes\n";

                // TODO: validate expected size here
                // e.g.: if (measureBuffer.size() != EXPECTED_SIZE)

                capturing = false;
                measureBuffer.clear();
            }
        }
    }
    }
    }
}
