#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <iomanip>
#include <vector>

int main() {

    // The port your LiDAR sends data to
    const int PORT = 1217;   // example Velodyne port — change to yours

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
    //addr.sin_addr.s_addr = INADDR_ANY;
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
    int counter = 0;

    int packetCounter = 0;
    int fragmentCounter = 0;     // counts fragments in current LiDAR measurement
    std::vector<char> measurementData;  // stores 3 fragments

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

        std::cout << "Received packet #" << packetCounter << " (" << received << " bytes)\n";
        packetCounter++;

        // Append packet fragment to current LiDAR measurement
        measurementData.insert(measurementData.end(), buffer, buffer + received);
        fragmentCounter++;

        if (fragmentCounter == 3)
        {
            static int measurementIndex = 0;

            std::ostringstream filename;
            filename << folder << "measurement_" << std::setw(4) << std::setfill('0') << measurementIndex << ".bin";
            std::ofstream outfile(filename.str(), std::ios::binary);
            outfile.write(measurementData.data(), measurementData.size());
            outfile.close();

             std::cout << "✔ Saved FULL LiDAR measurement #" << measurementIndex
                      << " (size = " << measurementData.size()
                      << " bytes) as " << filename.str() << std::endl;

            measurementIndex++;

            // Reset for next LiDAR measurement
            fragmentCounter = 0;
            measurementData.clear();
        }
    }

    close(sockfd);
    return 0;
}