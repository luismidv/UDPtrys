// --- 2. Checksum Utility (8-bit Sum Implementation) ---

// Keep the same offset, as the error is now likely the algorithm
constexpr size_t HEADER_AND_PREAMBLE_SIZE = 80;
constexpr size_t CHECKSUM_SIZE = 1;


/**
 * @brief Calculates the 8-bit Sum Checksum (modulo 256) for a given block of data.
 * @param data Pointer to the start of the data.
 * @param length The length of the data to include in the checksum calculation.
 * @return The calculated 8-bit sum (S).
 */
uint8_t calculate_sum_checksum(const unsigned char* data, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        // The cast to uint8_t automatically applies modulo 256 (wraparound)
        sum += data[i]; 
    }
    return sum;
}


/**
 * @brief Verifies the integrity of a single UDP packet using the 8-bit Sum Checksum.
 * @param packet_data The raw received packet data.
 * @param total_size The total received size (including data and checksum).
 * @return true if the check is successful, false otherwise.
 */
bool verify_checksum(const unsigned char* packet_data, size_t total_size) {
    if (total_size <= HEADER_AND_PREAMBLE_SIZE + CHECKSUM_SIZE) {
        std::cerr << "  [FAIL] Packet size (" << total_size << " bytes) is too small." << std::endl;
        return false;
    }

    // 1. Determine the length of the data that contributes to the checksum
    // This is the entire data packet minus the fixed header size and the checksum byte itself.
    size_t data_length_for_checksum = total_size - HEADER_AND_PREAMBLE_SIZE - CHECKSUM_SIZE;

    // 2. Calculate the 8-bit Sum for the data portion (starting at offset 80)
    uint8_t sum = calculate_sum_checksum(
        packet_data + HEADER_AND_PREAMBLE_SIZE, 
        data_length_for_checksum
    );
    
    // 3. The SICK sensor most likely sends the 1's complement of the sum.
    // 1's complement is 0xFF minus the sum.
    uint8_t calculated_checksum = (uint8_t)~(sum); // Bitwise NOT is 1's complement

    // 4. Extract the received checksum (it's the very last byte)
    uint8_t received_checksum = packet_data[total_size - CHECKSUM_SIZE];

    // 5. Compare
    if (calculated_checksum == received_checksum) {
        // std::cout << "  [PASS] Checksum verified successfully." << std::endl;
        return true;
    } else {
        std::cerr << "  [FAIL] Checksum Mismatch! Calculated: 0x" << std::hex 
                  << (int)calculated_checksum << ", Received: 0x" << (int)received_checksum 
                  << std::dec << ". DROPPING PACKET." << std::endl;
        return false;
    }
}
