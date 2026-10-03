#include <SpoutLibrary/SpoutLibrary.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: rood_spout_probe SENDER_NAME [SECONDS]\n";
        return 2;
    }
    const int seconds = argc == 3 ? std::atoi(argv[2]) : 15;
    if (seconds < 1 || seconds > 120) return 2;

    SPOUTLIBRARY* receiver = GetSpout();
    if (!receiver || !receiver->CreateOpenGL()) {
        std::cerr << "Spout receiver OpenGL initialization failed\n";
        if (receiver) receiver->Release();
        return 1;
    }
    receiver->SetReceiverName(argv[1]);
    unsigned width = 0;
    unsigned height = 0;
    int frames = 0;
    std::uint64_t sampleSum = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (receiver->ReceiveTexture()) {
            const unsigned nextWidth = receiver->GetSenderWidth();
            const unsigned nextHeight = receiver->GetSenderHeight();
            if (nextWidth > 0 && nextHeight > 0 && nextWidth <= 8192 && nextHeight <= 8192 &&
                static_cast<std::uint64_t>(nextWidth) * nextHeight <= 16777216) {
                width = nextWidth;
                height = nextHeight;
                std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
                if (receiver->ReceiveImage(pixels.data(), GL_RGBA)) {
                    ++frames;
                    for (std::size_t i = 0; i < pixels.size(); i += std::max<std::size_t>(4, pixels.size() / 1024))
                        sampleSum += pixels[i];
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    receiver->ReleaseReceiver();
    receiver->CloseOpenGL();
    receiver->Release();
    std::cout << "spoutProbe frames=" << frames << " size=" << width << 'x' << height
              << " pixelSampleSum=" << sampleSum << std::endl;
    return frames > 0 && sampleSum > 0 ? 0 : 1;
}
