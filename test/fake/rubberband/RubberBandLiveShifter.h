// Host-test stand-in for RubberBand 4.0 LiveShifter: exact delay line (no pitch change), same API surface the engine uses.
#pragma once
#include <vector>
#include <cstddef>
namespace RubberBand {
class RubberBandLiveShifter {
public:
    enum Option { OptionWindowShort = 0, OptionFormantShifted = 0, OptionChannelsApart = 0, OptionFormantPreserved = 0x01000000 };
    RubberBandLiveShifter(size_t sr, size_t ch, int opt) : sr_(sr), hist_(kDelay, 0.f) {}
    size_t getBlockSize() const { return 512; }
    size_t getStartDelay() const { return kStart; }
    void reset() { std::fill(hist_.begin(), hist_.end(), 0.f); pos_ = 0; }
    void setPitchScale(double) {}
    void setFormantScale(double) {}
    void shift(const float* const* in, float** out) {
        // output = input delayed by kStart samples (so engine alignment can be verified exactly)
        for (size_t i = 0; i < 512; i++) { out[0][i] = hist_[pos_]; hist_[pos_] = in[0][i]; pos_ = (pos_ + 1) % kDelay; }
    }
private:
    static constexpr size_t kStart = 1536, kDelay = 1536;
    size_t sr_; std::vector<float> hist_; size_t pos_ = 0;
};
}
