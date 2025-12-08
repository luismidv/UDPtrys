#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <chrono> // For time tracking
#include <fcntl.h>
#include <cerrno>
#include <cmath> // For std::round or other math operations

// Original function: Searches for the FF07 sequence anywhere within the buffer.
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

            // Check for START marker (FF07) ONLY if we are waiting for one
            bool isFF07Packet = false;
            if (packetsInCurrentMeasure == -1) {
                isFF07Packet = containsFF07(buffer, received);
            }
            
            if (isFF07Packet) {
                // Handle case where a packet was lost from the previous scan
                if (packetsInCurrentMeasure > 0 && packetsInCurrentMeasure < PACKETS_PER_MEASURE) {
                     // Note: Since the loop resets packetsInCurrentMeasure to -1 on completion,
                     // this check is currently redundant but good for robustness if logic changes.
                }

                measureCounter++;
                packetsInCurrentMeasure = 1;

                // Optional: Print only every X measures to reduce log spam
                if (measureCounter % 100 == 1) { 
                    std::cout << ">>> Measurement START (FF07 found) #"
                              << measureCounter << " (Packet " << packetCounter << ")\n";
                }
            }
            else if (packetsInCurrentMeasure >= 1 && packetsInCurrentMeasure < PACKETS_PER_MEASURE) {
                // Continue adding packets to the current measurement
                packetsInCurrentMeasure++;
            }
            
            // Check for COMPLETION
            if (packetsInCurrentMeasure == PACKETS_PER_MEASURE) {
                // Optional: Print completion only every X measures
                if (measureCounter % 100 == 0) {
                    std::cout << ">>> Measurement COMPLETE (#"
                              << measureCounter << ") - Received " << PACKETS_PER_MEASURE << " packets.\n";
                }
                // Reset state to wait for the next measure start
                packetsInCurrentMeasure = -1; 
            }
        }
        else if (received < 0 && (errno != EWOULDBLOCK && errno != EAGAIN)) {
            perror("recvfrom error");
            break;
        }
    }
    
    close(sockfd);
    
    // --- Calculation and Output of Results ---
    
    // Calculate the total expected packets
    const long expectedPackets = measureCounter * PACKETS_PER_MEASURE;
    
    // Calculate lost packets
    const long lostPackets = expectedPackets - packetCounter;
    
    // Calculate packet loss rate
    double lossRate = 0.0;
    if (expectedPackets > 0) {
        lossRate = ((double)lostPackets / expectedPackets) * 100.0;
    }

    std::cout << "\nSummary after " << std::chrono::duration_cast<std::chrono::seconds>(DURATION_LIMIT).count() << " seconds:\n";
    std::cout << "------------------------------------------\n";
    std::cout << "✅ " << measureCounter << " **scans** have been collected.\n";
    std::cout << "📦 " << packetCounter << " **packets** have been received in total.\n";
    std::cout << "🔍 Expected packets (scans * 5): " << expectedPackets << "\n";
    std::cout << "❌ Packets lost: " << lostPackets << "\n";
    std::cout << "📊 **Packet Loss Rate:** " << lossRate << "%\n";
    std::cout << "------------------------------------------\n";

    return 0;
}
