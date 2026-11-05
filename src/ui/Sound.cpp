#include "ui/Sound.h"

#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace playr {

bool g_soundOn = true;

namespace {

constexpr int kRate = 22050;
constexpr double kPi = 3.14159265358979323846;

std::vector<unsigned char> g_wav;  // must stay alive while the asynchronous playback runs

void Put32(std::vector<unsigned char>& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((v >> (8 * i)) & 0xFF); }
void Put16(std::vector<unsigned char>& out, uint16_t v) { for (int i = 0; i < 2; ++i) out.push_back((v >> (8 * i)) & 0xFF); }

}  // namespace

void PlaySpinSound(int cards, DWORD durationMs) {
    if (!g_soundOn || cards < 1) return;

    double chimeLength = 0.9;
    size_t total = (size_t)((durationMs / 1000.0 + chimeLength) * kRate);
    std::vector<float> mix(total, 0.0f);
    std::mt19937 rng{ 12345 };
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);

    // The reel position follows 1 - (1 - t)^4, so card k passes the center at t = 1 - (1 - k / cards)^(1/4).
    for (int k = 1; k <= cards; ++k) {
        double progress = (double)k / cards;
        double t = 1.0 - std::pow(1.0 - progress, 0.25);
        size_t start = (size_t)(t * durationMs / 1000.0 * kRate);
        double freq = 2300.0 - 1100.0 * progress;     // the clicks get lower and fuller as the wheel slows
        double amp = 0.30 + 0.35 * progress;
        int length = (int)(kRate * (0.012 + 0.010 * progress));
        for (int n = 0; n < length && start + n < total; ++n) {
            double env = std::exp(-n / (kRate * (0.0025 + 0.002 * progress)));
            double tone = std::sin(2.0 * kPi * freq * n / kRate);
            mix[start + n] += (float)(amp * env * (0.8 * tone + 0.35 * noise(rng)));
        }
    }

    // The chime when it lands: a fifth over the root, with a soft decay.
    size_t chimeStart = (size_t)(durationMs / 1000.0 * kRate);
    const double tones[3] = { 659.25, 987.77, 1318.51 };
    for (size_t n = 0; chimeStart + n < total; ++n) {
        double t = (double)n / kRate, env = std::exp(-t / 0.22) * std::min(1.0, t / 0.004);
        double sum = 0;
        for (int i = 0; i < 3; ++i) sum += std::sin(2.0 * kPi * tones[i] * t) * (i == 0 ? 0.5 : 0.3);
        mix[chimeStart + n] += (float)(0.55 * env * sum);
    }

    std::vector<unsigned char> wav;
    uint32_t dataBytes = (uint32_t)(total * 2);
    wav.insert(wav.end(), { 'R', 'I', 'F', 'F' });
    Put32(wav, 36 + dataBytes);
    wav.insert(wav.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    Put32(wav, 16);
    Put16(wav, 1);  // PCM
    Put16(wav, 1);  // mono
    Put32(wav, kRate);
    Put32(wav, kRate * 2);
    Put16(wav, 2);
    Put16(wav, 16);
    wav.insert(wav.end(), { 'd', 'a', 't', 'a' });
    Put32(wav, dataBytes);
    for (float sample : mix) Put16(wav, (uint16_t)(int16_t)(std::clamp(sample, -1.0f, 1.0f) * 26000.0f));

    PlaySoundW(nullptr, nullptr, 0);  // stop the previous one before its buffer is replaced
    g_wav = std::move(wav);
    PlaySoundW((LPCWSTR)g_wav.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void StopSpinSound() { PlaySoundW(nullptr, nullptr, 0); }

}  // namespace playr
