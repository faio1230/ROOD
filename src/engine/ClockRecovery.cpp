#include "rood/ClockRecovery.hpp"

#include <algorithm>
#include <cmath>

namespace rood {

void ClockRecovery::reset() noexcept {
    referenceSet_ = false;
    warmupSeconds_ = 0.0;
    referenceLeadSeconds_ = 0.0;
    filteredErrorMs_ = 0.0;
    correctionPpm_ = 0.0;
}

double ClockRecovery::observe(double leadSeconds, double elapsedSeconds) noexcept {
    if (!std::isfinite(leadSeconds) || !std::isfinite(elapsedSeconds) ||
        leadSeconds < -2.0 || leadSeconds > 5.0 || elapsedSeconds < 0.0)
        return correctionPpm_;
    const double dt = std::min(elapsedSeconds, 1.0);
    if (!referenceSet_) {
        referenceLeadSeconds_ = leadSeconds;
        referenceSet_ = true;
        return correctionPpm_;
    }
    if (!locked()) {
        warmupSeconds_ += dt;
        const double alpha = dt / (1.0 + dt);
        referenceLeadSeconds_ += alpha * (leadSeconds - referenceLeadSeconds_);
        return correctionPpm_;
    }
    const double errorMs = std::clamp(
        (leadSeconds - referenceLeadSeconds_) * 1000.0, -25.0, 25.0);
    const double alpha = dt / (20.0 + dt);
    filteredErrorMs_ += alpha * (errorMs - filteredErrorMs_);
    correctionPpm_ = std::clamp(-20.0 * filteredErrorMs_, -500.0, 500.0);
    return correctionPpm_;
}

} // namespace rood
