#include <iostream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}
#include <srt/srt.h>
#include <libomt.h>
#include <SpoutLibrary/SpoutLibrary.h>

int main() {
    const std::string configuration = avcodec_configuration();
    const std::string license = avcodec_license();
    std::cout << "FFmpeg avcodec version: " << avcodec_version() << '\n';
    std::cout << "FFmpeg license: " << license << '\n';
    std::cout << "FFmpeg configuration: " << configuration << '\n';
    std::cout << "libsrt version: " << srt_getversion() << '\n';

    // Taking these addresses forces the import libraries and DLLs to link.
    auto* volatile spoutSymbol = &GetSpout;
    auto* volatile omtSymbol = &omt_send_connections;
    std::cout << "Spout2 symbol: " << (spoutSymbol != nullptr) << '\n';
    std::cout << "OMT symbol: " << (omtSymbol != nullptr) << '\n';

    if (license.find("LGPL") == std::string::npos ||
        configuration.find("--enable-gpl") != std::string::npos ||
        configuration.find("--enable-nonfree") != std::string::npos) {
        std::cerr << "Unexpected FFmpeg GPL or nonfree build option\n";
        return 1;
    }
    return 0;
}
