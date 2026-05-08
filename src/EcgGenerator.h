#pragma once

#include <vector>

namespace tts::server {

// Generates a synthetic ECG waveform sample-by-sample. A simple PQRST shape
// at a fixed rate of 60 bpm; useful for end-to-end protocol testing, not for
// medical analysis.
class EcgGenerator {
public:
    explicit EcgGenerator(int sampleRate);
    std::vector<float> next(int count);

private:
    int    sampleRate_;
    double t_   = 0.0;   // seconds since start
    double bpm_ = 60.0;
};

}  // namespace tts::server
