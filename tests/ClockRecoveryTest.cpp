#include "rood/ClockRecovery.hpp"

#include <cmath>
#include <iostream>

int main() {
    rood::ClockRecovery recovery;
    double leadSeconds = 0.25;
    double ppm = 0.0;
    for (int second = 0; second < 3600; ++second) {
        // A source clock 100 ppm faster than the output device adds 0.36 s of
        // queue lead over an hour without compensation.
        leadSeconds += (100.0 + ppm) / 1000000.0;
        ppm = recovery.observe(leadSeconds, 1.0);
    }
    if (!recovery.locked() || std::fabs(ppm + 100.0) > 15.0 ||
        std::fabs(leadSeconds - 0.25) > 0.02) {
        std::cerr << "clock recovery failed: ppm=" << ppm
                  << " lead=" << leadSeconds << '\n';
        return 1;
    }
    const double beforeSpike = ppm;
    recovery.observe(leadSeconds + 0.5, 0.02);
    if (std::fabs(recovery.correctionPpm() - beforeSpike) > 2.0) {
        std::cerr << "clock recovery reacted too sharply to one jitter spike\n";
        return 1;
    }
    recovery.reset();
    if (recovery.locked() || recovery.correctionPpm() != 0.0) return 1;
    leadSeconds = 0.25;
    ppm = 0.0;
    for (int second = 0; second < 3600; ++second) {
        leadSeconds += (-100.0 + ppm) / 1000000.0;
        ppm = recovery.observe(leadSeconds, 1.0);
    }
    if (!recovery.locked() || std::fabs(ppm - 100.0) > 15.0 ||
        std::fabs(leadSeconds - 0.25) > 0.02) {
        std::cerr << "slow-source recovery failed: ppm=" << ppm
                  << " lead=" << leadSeconds << '\n';
        return 1;
    }
    return 0;
}
