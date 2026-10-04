#include "rood/AudioTimeline.hpp"

#include <cmath>
#include <stdexcept>

namespace {

void require(bool condition) {
    if (!condition) throw std::runtime_error("audio timeline result differs from expected output");
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.00001f;
}

} // namespace

int main() {
    rood::AudioTimeline timeline(2, 16, {
        {10, 0, 1, 0.5f},
        {20, 5, 0, 1.0f},
        {20, 0, 1, 1.0f},
    });
    const float stereo[] = {0.2f, 0.0f, 0.4f, 0.0f};
    const float surround[] = {
        0.3f, 0, 0, 0, 0, 0.7f,
        0.4f, 0, 0, 0, 0, 0.8f,
    };
    require(timeline.push(10, 4, 2, stereo, 2) == 2);
    require(timeline.push(20, 4, 6, surround, 2) == 2);

    float output[8] = {};
    require(timeline.pull(4, output) == 0);
    for (float sample : output) require(sample == 0.0f);
    float mixed[4] = {};
    require(timeline.pull(2, mixed) == 2);
    require(near(mixed[0], 0.7f) && near(mixed[1], 0.4f));
    require(near(mixed[2], 0.8f) && near(mixed[3], 0.6f));

    require(timeline.push(10, 1, 2, stereo, 2) == 0); // already played
    require(timeline.push(10, 30, 2, stereo, 2) == 0); // beyond bounded queue
    require(timeline.rejectedFrames() == 4);
    require(timeline.push(99, 7, 2, stereo, 2) == 0); // unmapped track
    require(timeline.silentFrames() == 4);

    timeline.reset();
    const float loud[] = {0, 0, 0, 0, 0, 0.8f};
    require(timeline.push(20, 0, 6, loud, 1) == 1);
    require(timeline.push(20, 0, 6, loud, 1) == 1);
    float clipped[2] = {};
    require(timeline.pull(1, clipped) == 1);
    require(clipped[0] == 1.0f && clipped[1] == 0.0f);

    // A stream may return with fewer channels after reconnecting. Keep the
    // valid route and leave the unavailable source channel silent.
    require(timeline.push(20, 1, 2, stereo, 1) == 1);
    float reduced[2] = {};
    require(timeline.pull(1, reduced) == 1);
    require(near(reduced[0], 0.0f) && near(reduced[1], 0.2f));

    rood::AudioTimeline unavailable(1, 8, {{21, 5, 0, 1.0f}});
    require(unavailable.push(21, 0, 1, stereo, 1) == 0);
    float silent = 1.0f;
    require(unavailable.pull(1, &silent) == 0 && silent == 0.0f);
    return 0;
}
