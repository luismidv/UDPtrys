#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include "fcntl.h"

int main() {
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
    while (packetCounter < 500) {
        sockaddr_in sender;
        socklen_t senderLen = sizeof(sender);
        ssize_t received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr*)&sender, &senderLen);
        if (received > 0) {
            packetCounter++;
        }
    }

    std::cout << "Listening...\n";

    while (true) {
        sockaddr_in sender;
        socklen_t senderLen = sizeof(sender);

        ssize_t received =recvfrom(sockfd, buffer, sizeof(buffer), 0,(sockaddr*)&sender, &senderLen);
        //struct mmsghdr msgs[64]; // batch 64 packets per syscall
        //int num = recvmmsg(sockfd, msgs, 64, 0, NULL);  

        if (received > 0) {
            packetCounter++;
            if (packetCounter % 500 == 0)
                std::cout << "Packets received: " << packetCounter << "\n";
        }
    }
}
