#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <numeric>
#include <cerrno>

// Networking
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

bool containsFF07(const char* buf, ssize_t len) {
    if (len < 2) return false;
    for (ssize_t i = 0; i < len - 1; i++) {
        if ((unsigned char)buf[i] == 0xFF &&
            (unsigned char)buf[i + 1] == 0x07) {
            return true;
        }
    }
    return false;
}

int main() {
    using clock = std::chrono::steady_clock;

    std::ofstream resfile("log.txt", std::ios::app);
    if (!resfile) {
        std::cerr << "Failed to open log file\n";
        return 1;
    }

    const int PORT = 1217;
    const auto RUN_DURATION = std::chrono::minutes(8);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    char buffer[4096];
    sockaddr_in sender{};
    socklen_t senderLen = sizeof(sender);

    bool havePrev = false;
    clock::time_point prev;

    std::vector<double> intervals;

    auto start = clock::now();

    std::cout << "Running for 8 minutes... listening for FF07 packets.\n";

    while (true) {
        auto now = clock::now();
        if (now - start >= RUN_DURATION) break;

        ssize_t received = recvfrom(
            sockfd, buffer, sizeof(buffer),
            0, (sockaddr*)&sender, &senderLen
        );

        if (received > 0) {

            if (containsFF07(buffer, received)) {
                auto ts = clock::now();

                // Timestamp in ms for the log
                long long ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        ts.time_since_epoch()
                    ).count();

                if (havePrev) {
                    double dt = std::chrono::duration<double>(ts - prev).count();

                    std::cout << "FF07 interval: " << dt << " sec\n";
                    resfile << ms << " ms, " << dt << " sec\n";

                    intervals.push_back(dt);
                }

                prev = ts;
                havePrev = true;
            }
        }
        else if (received < 0 &&
                 errno != EWOULDBLOCK &&
                 errno != EAGAIN) {
            perror("recvfrom");
            break;
        }
    }

    close(sockfd);

    // ---- Summary ----
    std::cout << "\n--- Summary after 8 minutes ---\n";

    if (!intervals.empty()) {
        double mean = std::accumulate(intervals.begin(), intervals.end(), 0.0)
                      / intervals.size();

        std::cout << "Number of FF07 packets detected: "
                  << (intervals.size() + 1) << "\n";
        std::cout << "Mean FF07 interval: " << mean << " sec\n";
    } else {
        std::cout << "No FF07 intervals recorded.\n";
    }

    return 0;
}
