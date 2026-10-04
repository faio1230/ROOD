#pragma once

#include <cmath>
#include <optional>

namespace rood {

// Compare successive input timestamps before resampling. Output frame counts
// intentionally diverge from input PTS while correcting a clock difference.
class AudioPtsContinuity {
public:
    void reset() noexcept { expectedNextSeconds_.reset(); }

    std::optional<double> observe(std::optional<double> ptsSeconds,
                                  int sampleCount, int sampleRate) noexcept {
        if (!ptsSeconds || !std::isfinite(*ptsSeconds) ||
            sampleCount <= 0 || sampleRate <= 0) {
            reset();
            return std::nullopt;
        }
        const auto expected = expectedNextSeconds_;
        expectedNextSeconds_ = *ptsSeconds +
            static_cast<double>(sampleCount) / sampleRate;
        return expected ? std::optional<double>(*ptsSeconds - *expected) : std::nullopt;
    }

private:
    std::optional<double> expectedNextSeconds_;
};

} // namespace rood
