#pragma once

namespace rood {

// Tracks the audio timeline's lead over the output device. The first five
// seconds establish the transport/decode lead; later changes drive a bounded
// soft resampling correction. Positive correction produces more output samples
// per source second.
class ClockRecovery {
public:
    void reset() noexcept;
    double observe(double leadSeconds, double elapsedSeconds) noexcept;
    double correctionPpm() const noexcept { return correctionPpm_; }
    double errorMs() const noexcept { return filteredErrorMs_; }
    bool locked() const noexcept { return warmupSeconds_ >= 5.0; }

private:
    bool referenceSet_ = false;
    double warmupSeconds_ = 0.0;
    double referenceLeadSeconds_ = 0.0;
    double filteredErrorMs_ = 0.0;
    double correctionPpm_ = 0.0;
};

} // namespace rood
