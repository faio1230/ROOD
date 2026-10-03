#include "rood/ChannelRouter.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

using rood::AudioBlockView;
using rood::ChannelRoute;
using rood::ChannelRouter;

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("channel router result differs from expected output");
    }
}

int main() {
    const ChannelRouter router(4, {
        {10, 1, 0, 1.0f},
        {10, 0, 2, 1.0f},
        {20, 0, 1, 0.5f},
        {20, 0, 3, 1.0f},
    });
    const float stereo[] = {0.1f, 0.2f, 0.3f, 0.4f};
    const float mono[] = {0.6f, 0.8f};
    const std::vector<AudioBlockView> blocks = {{10, 2, 2, stereo}, {20, 1, 2, mono}};
    float output[8] = {};
    require(router.render(blocks, 2, output));
    const float expected[] = {0.2f, 0.3f, 0.1f, 0.6f,
                              0.4f, 0.4f, 0.3f, 0.8f};
    for (int i = 0; i < 8; ++i) {
        require(std::fabs(output[i] - expected[i]) < 0.00001f);
    }

    require(router.render({{10, 2, 2, stereo}}, 2, output));
    require(output[1] == 0.0f && output[3] == 0.0f);

    const ChannelRouter summed(1, {{10, 0, 0, 1.0f}, {20, 0, 0, 1.0f}});
    const float loud_a[] = {0.8f};
    const float loud_b[] = {0.7f};
    float clipped = 0.0f;
    require(summed.render({{10, 1, 1, loud_a}, {20, 1, 1, loud_b}}, 1, &clipped));
    require(clipped == 1.0f);

    output[0] = 0.75f;
    require(!router.render({{10, 2, 1, stereo}}, 2, output));
    require(output[0] == 0.75f);
    require(!router.render({{10, 1, 2, stereo}}, 2, output));
    require(output[0] == 0.75f);

    bool rejected = false;
    try {
        ChannelRouter invalid(2, {{1, 0, 2, 1.0f}});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected);

    return 0;
}
