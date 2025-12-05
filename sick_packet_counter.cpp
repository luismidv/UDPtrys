bool containsFF07(const char* buf, ssize_t len) {
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

    std::cout << "Listening...\n";

    while (true) {
        sockaddr_in sender;
        socklen_t senderLen = sizeof(sender);

        ssize_t received =
            recvfrom(sockfd, buffer, sizeof(buffer), 0,
                     (sockaddr*)&sender, &senderLen);

        if (received > 0) {
            packetCounter++;

            bool isFF07 = containsFF07(buffer, received);

            if (isFF07) {
                // Start a new measurement
                measureCounter++;
                packetsInCurrentMeasure = 1;

                std::cout << ">>> Measurement START (FF07)  #"
                          << measureCounter << "\n";
            }
            else if (packetsInCurrentMeasure > 0) {
                // Continue adding packets to this measurement
                packetsInCurrentMeasure++;
            }

            if (packetsInCurrentMeasure == 5) {
                std::cout << ">>> Measurement COMPLETE (#"
                          << measureCounter << ") - 5 packets\n\n";

                packetsInCurrentMeasure = 0;
            }
        }
    }
}
