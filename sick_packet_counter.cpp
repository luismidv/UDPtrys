#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <chrono>
#include <fcntl.h>
#include <cstring>

int main() {
    using clock = std::chrono::high_resolution_clock;

    const int PORT = 1217;

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

    long packetCounter = 0;
    long measureCounter = 0;
    int packetsInCurrentMeasure = 0;
    bool lookingForStart = true;

    std::cout << "Listening...\n";

    while (true) {
        sockaddr_in sender;
        socklen_t senderLen = sizeof(sender);

        ssize_t received =
            recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr*)&sender, &senderLen);

        if (received > 0) {
            packetCounter++;

            // Check if packet contains FC 07
            bool isFC07 = false;
            for (int i = 0; i < received - 1; i++) {
                if ((unsigned char)buffer[i] == 0xFC &&
                    (unsigned char)buffer[i + 1] == 0x07) {
                    isFC07 = true;
                    break;
                }
            }

            if (isFC07) {
                // Start a new measure
                measureCounter++;
                packetsInCurrentMeasure = 1;

                std::cout << "New measurement started (FC07). Measure #: "
                          << measureCounter << "\n";
            }
            else if (packetsInCurrentMeasure > 0) {
                // Count remaining 4 packets
                packetsInCurrentMeasure++;
            }

            // When 5 packets collected → one full measurement
            if (packetsInCurrentMeasure == 5) {
                std::cout << "Measurement #" << measureCounter
                          << " complete (5 packets)\n";

                packetsInCurrentMeasure = 0;  // reset for next measure
            }
        }
    }
}
