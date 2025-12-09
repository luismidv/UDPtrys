if (received > 0) {
    packetCounter++;

    const uint8_t* data = (const uint8_t*)buffer;

    for (ssize_t i = 0; i < received; i++) {

        // --- START DETECTION (FF 07) ---
        if (!capturing &&
            i < received - 1 &&
            data[i] == 0xFF &&
            data[i + 1] == 0x07)
        {
            capturing = true;
            measureBuffer.clear();

            measureBuffer.push_back(0xFF);
            measureBuffer.push_back(0x07);

            i++;    // skip the 07 (we already consumed it)
            continue;
        }

        // --- CAPTURE BYTES WHILE IN A MEASUREMENT ---
        if (capturing) {
            measureBuffer.push_back(data[i]);

            // --- END DETECTION (00 29) ---
            int n = measureBuffer.size();
            if (n >= 2 &&
                measureBuffer[n-2] == 0x00 &&
                measureBuffer[n-1] == 0x29)
            {
                measureCounter++;

                std::cout << "Measurement #" << measureCounter
                          << " captured (" << measureBuffer.size()
                          << " bytes)\n";

                // TODO: check expected size here if needed

                capturing = false;
                measureBuffer.clear();
            }
        }
    }
}
