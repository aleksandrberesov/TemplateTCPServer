#include "EcgGenerator.h"

#include <cmath>

namespace tts::server {

namespace {

double bump(double phase, double center, double width) {
    double x = (phase - center) / width;
    return std::exp(-x * x);
}

// PQRST approximated as a sum of Gaussians; phase in [0, 1).
double ecgSample(double phase) {
    double v = 0.0;
    v +=  0.10 * bump(phase, 0.20, 0.025);   // P
    v += -0.10 * bump(phase, 0.38, 0.010);   // Q
    v +=  1.00 * bump(phase, 0.40, 0.012);   // R
    v += -0.20 * bump(phase, 0.42, 0.012);   // S
    v +=  0.30 * bump(phase, 0.60, 0.040);   // T
    return v;
}

}  // namespace

EcgGenerator::EcgGenerator(int sampleRate)
    : sampleRate_(sampleRate > 0 ? sampleRate : 250) {}

std::vector<float> EcgGenerator::next(int count) {
    std::vector<float> out;
    out.reserve(count > 0 ? static_cast<size_t>(count) : 0);
    const double dt     = 1.0 / static_cast<double>(sampleRate_);
    const double period = 60.0 / bpm_;
    for (int i = 0; i < count; ++i) {
        double phase = std::fmod(t_, period) / period;
        out.push_back(static_cast<float>(ecgSample(phase)));
        t_ += dt;
    }
    return out;
}

}  // namespace tts::server
