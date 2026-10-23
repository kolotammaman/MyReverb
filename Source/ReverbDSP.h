/*
  ==============================================================================
    ReverbDSP.h  —  MyReverb v2 engine

    Pure C++ (no JUCE dependency) so it can be rendered and measured offline.

    Signal flow
      in ─► reverse splicer ─► pre-delay (fractional, smoothed)
         ─► 16-ch spread ─► 4-stage diffuser (delay + Hadamard + polarity)
         ─► 16-line modulated FDN
               per line: Hermite-interpolated modulated read
                         ─► in-loop allpass (density build-up)
                         ─► 3-band decay filter (bass / mid / air, Jot-style)
               Householder feedback matrix
               shimmer: octave pitch shifters fed back into the tank
         ─► early + late taps ─► low/high cut ─► width ─► duck ─► mix ─► soft clip
  ==============================================================================
*/
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mr
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr int   kCh    = 16;    // network width
constexpr int   kHalf  = kCh / 2;
constexpr int   kBlock = 32;    // control-rate sub-block

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf  (float a, float b, float t)   { return a + (b - a) * t; }

// ============================================================
// Rng  —  xorshift32, deterministic and allocation-free
// ============================================================
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    float uni()     { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (1.0f / 16777216.0f); }
    float bipolar() { return uni() * 2.0f - 1.0f; }
};

// ============================================================
// DelayLine  —  power-of-two circular buffer
//   read*(d) returns the sample written d samples ago (d >= 1),
//   so call read before push for an exact d-sample loop.
// ============================================================
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        int n = 1;
        while (n < maxDelay + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }

    inline void  push (float x)          { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float readInt (int d) const   { return buf[(size_t) ((w - d) & mask)]; }

    // block write: contiguous memcpy unless the span wraps
    void pushBlock (const float* src, int n)
    {
        const int first = std::min (n, mask + 1 - w);
        std::memcpy (&buf[(size_t) w], src, sizeof (float) * (size_t) first);
        if (first < n) std::memcpy (&buf[0], src + first, sizeof (float) * (size_t) (n - first));
        w = (w + n) & mask;
    }

    // block read after pushBlock(n): out[j] = sample j of that block delayed by (i + f), i >= 1.
    // Consecutive j read consecutive memory, so the common no-wrap case is a straight lerp.
    void readBlock (float* out, int n, int i, float f, float gain) const
    {
        const int start = (w - n - i + 1) & mask;      // newer tap for j = 0 (older tap is start - 1)
        if (start >= 1 && start + n <= mask + 1)
        {
            const float* a = &buf[(size_t) start];
            for (int j = 0; j < n; ++j) out[j] = (a[j] + (a[j - 1] - a[j]) * f) * gain;
        }
        else
        {
            for (int j = 0; j < n; ++j)
            {
                const float a = buf[(size_t) ((start + j) & mask)], b = buf[(size_t) ((start + j - 1) & mask)];
                out[j] = (a + (b - a) * f) * gain;
            }
        }
    }

    // pre-split fractional delay (int part >= 1, frac in [0,1)) — for delays fixed over a block
    inline float readSplit (int i, float f) const
    {
        const float a = buf[(size_t) ((w - i) & mask)];
        const float b = buf[(size_t) ((w - i - 1) & mask)];
        return a + (b - a) * f;
    }

    inline float readLinear (float d) const
    {
        const int   i = (int) d;
        const float f = d - (float) i;
        const float a = buf[(size_t) ((w - i) & mask)];
        const float b = buf[(size_t) ((w - i - 1) & mask)];
        return a + (b - a) * f;
    }

    // raw neighbours around integer delay i (i >= 2): newer, at, older, older+1
    inline void tap4 (int i, float& xm, float& x0, float& x1, float& x2) const
    {
        xm = buf[(size_t) ((w - i + 1) & mask)];
        x0 = buf[(size_t) ((w - i)     & mask)];
        x1 = buf[(size_t) ((w - i - 1) & mask)];
        x2 = buf[(size_t) ((w - i - 2) & mask)];
    }

    // 4-point Hermite — smooth enough for audible modulation without dulling the tail
    inline float readCubic (float d) const
    {
        const int   i  = (int) d;
        const float f  = d - (float) i;
        const float xm = buf[(size_t) ((w - i + 1) & mask)];
        const float x0 = buf[(size_t) ((w - i)     & mask)];
        const float x1 = buf[(size_t) ((w - i - 1) & mask)];
        const float x2 = buf[(size_t) ((w - i - 2) & mask)];
        const float c1 = 0.5f * (x1 - xm);
        const float c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// ============================================================
// OnePole  —  y += a (x - y)
// ============================================================
struct OnePole
{
    float a = 1.0f, y = 0.0f;
    void  setCutoff (float hz, double sr) { a = 1.0f - std::exp (-kTwoPi * hz / (float) sr); }
    inline float lp (float x) { y += a * (x - y); return y; }
    inline float hp (float x) { return x - lp (x); }
    void  reset() { y = 0.0f; }
};

// ============================================================
// Biquad  —  TDF-II, RBJ Butterworth low/high pass
// ============================================================
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setLowPass (float hz, double sr)  { design (hz, sr, false); }
    void setHighPass (float hz, double sr) { design (hz, sr, true); }

    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }

private:
    void design (float hz, double sr, bool high)
    {
        hz = clampf (hz, 10.0f, 0.49f * (float) sr);
        const float w0 = kTwoPi * hz / (float) sr;
        const float cw = std::cos (w0), alpha = std::sin (w0) / (2.0f * 0.70710678f);
        const float ia0 = 1.0f / (1.0f + alpha);
        if (high) { b0 = (1.0f + cw) * 0.5f * ia0; b1 = -(1.0f + cw) * ia0; }
        else      { b0 = (1.0f - cw) * 0.5f * ia0; b1 =  (1.0f - cw) * ia0; }
        b2 = b0;
        a1 = -2.0f * cw * ia0;
        a2 = (1.0f - alpha) * ia0;
    }
};

// ============================================================
// Householder feedback matrix (in place) — the Hadamard lives in the block diffuser
// ============================================================
inline void householder16 (float* x)
{
    float sum = 0.0f;
    for (int i = 0; i < kCh; ++i) sum += x[i];
    sum *= -2.0f / (float) kCh;
    for (int i = 0; i < kCh; ++i) x[i] += sum;
}

// ============================================================
// PitchShifter  —  two crossfaded taps on a delay line.
//   sin² windows sum to exactly 1, cubic reads, ~70 ms grains:
//   long enough that shimmer sounds like a choir, not a buzz.
// ============================================================
class PitchShifter
{
public:
    void prepare (double sr)
    {
        window = (float) (0.07 * sr);
        line.prepare ((int) window + 8);
        reset();
    }
    void reset() { line.clear(); phase = 0.0f; }
    void setRatio (float r) { ratio = r; }

    inline float process (float x)
    {
        line.push (x);
        phase += (ratio - 1.0f) / window;
        phase -= std::floor (phase);
        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;

        const float d1 = 2.0f + window * (1.0f - phase);
        const float d2 = 2.0f + window * (1.0f - p2);
        const float s1 = std::sin (kPi * phase);
        const float s2 = std::sin (kPi * p2);
        return line.readCubic (d1) * s1 * s1 + line.readCubic (d2) * s2 * s2;
    }

private:
    DelayLine line;
    float window = 3000.0f, phase = 0.0f, ratio = 2.0f;
};

// ============================================================
// Params  —  plain values, as the user sees them
// ============================================================
struct Params
{
    float size       = 0.65f;   // 0–1
    float decay      = 4.0f;    // seconds (RT60, mid band)
    float preDelayMs = 20.0f;
    float diffusion  = 0.8f;    // 0–1
    float damping    = 0.45f;   // 0–1  (high-frequency decay)
    float bass       = 1.15f;   // low-band RT multiplier, 0.5–2
    float lowCut     = 80.0f;   // Hz
    float highCut    = 14000.0f;// Hz
    float mod        = 0.35f;   // 0–1
    float shimmer    = 0.0f;    // 0–1
    float reverse    = 0.0f;    // 0–1
    float noise      = 0.0f;    // 0–1
    float duck       = 0.0f;    // 0–1
    float width      = 1.0f;    // 0–1.5
    float dry        = 1.0f;    // linear gain
    float wet        = 0.35f;   // linear gain
    bool  freeze     = false;
};

// ============================================================
// ReverbEngine
// ============================================================
class ReverbEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const float msToS = (float) sr * 0.001f;

        // Late-tank line lengths (ms @ size scale 1.0): 16 lines spread ~2.6:1, interleaved
        // between the left- and right-fed halves so neither side owns all the long lines
        static const float kLineMs[kCh] = { 39.1f, 47.3f, 56.9f, 67.7f, 80.3f, 91.1f, 101.3f, 37.3f,
                                            43.1f, 52.7f, 61.9f, 73.1f, 85.7f, 96.7f, 34.9f, 107.9f };
        // In-loop allpasses: short, fixed, staggered (no two alike)
        static const float kApMs[kCh]   = { 3.13f, 4.37f, 5.29f, 6.71f, 3.71f, 4.93f, 5.87f, 7.27f,
                                            2.81f, 3.97f, 4.61f, 6.13f, 3.43f, 5.57f, 6.37f, 7.79f };
        // Diffuser stage spans (ms @ scale 1.0) — each roughly doubles
        static const float kDiffMs[kStages] = { 4.6f, 8.9f, 17.3f, 31.1f };

        Rng r;  r.s = 0x51A7E5u;
        for (int s = 0; s < kStages; ++s)
            for (int c = 0; c < kCh; ++c)
            {
                // one delay per slot of the span, jittered — keeps the stage evenly filled
                diffMs[s][c]   = kDiffMs[s] * ((float) c + 0.15f + 0.7f * r.uni()) / (float) kCh;
                diffFlip[s][c] = r.uni() < 0.5f ? -1.0f : 1.0f;
                diffLine[s][c].prepare ((int) (diffMs[s][c] * kMaxScale * msToS) + 4 + kBlock);   // block-wise reads reach back n extra
            }

        for (int c = 0; c < kCh; ++c)
        {
            lineMs[c] = kLineMs[c];
            const int maxLen = (int) ((kLineMs[c] * kMaxScale + kMaxModMs) * msToS) + 8;
            line[c].prepare (maxLen);
            apLen[c] = std::max (1, (int) (kApMs[c] * msToS));
            apLine[c].prepare (apLen[c] + 2);

            // LFO rates spread 0.09–0.9 Hz, shuffled across lines so they never re-align
            const float rate = 0.09f * std::pow (10.0f, (float) ((c * 7) % kCh) / (float) (kCh - 1));
            const float inc  = kTwoPi * rate / (float) sr;
            lfoCos[c] = std::cos (inc);
            lfoSin[c] = std::sin (inc);
            lfoX[c] = std::cos (kTwoPi * (float) c / (float) kCh * 2.7f);
            lfoY[c] = std::sin (kTwoPi * (float) c / (float) kCh * 2.7f);
        }

        preDelay[0].prepare ((int) (kMaxPreDelayMs * msToS) + 4);
        preDelay[1].prepare ((int) (kMaxPreDelayMs * msToS) + 4);

        for (auto& ps : shifter) ps.prepare (sr);
        shimHp[0].setCutoff (220.0f, sr); shimHp[1].setCutoff (220.0f, sr);
        for (auto& f : shimLp) f.setLowPass (6500.0f, sr);

        noiseHp[0].setCutoff (900.0f, sr); noiseHp[1].setCutoff (900.0f, sr);
        noiseLp[0].setCutoff (9000.0f, sr); noiseLp[1].setCutoff (9000.0f, sr);

        revBufSize = (int) (kRevBufSecs * sr);
        revBuf[0].assign ((size_t) revBufSize, 0.0f);
        revBuf[1].assign ((size_t) revBufSize, 0.0f);

        bassSplitA = 1.0f - std::exp (-kTwoPi * 260.0f / (float) sr);

        const float envA = 1.0f - std::exp (-1.0f / (0.008f * (float) sr));
        const float envR = 1.0f - std::exp (-1.0f / (0.300f * (float) sr));
        duckAtk = envA; duckRel = envR;
        wetEnvA = 1.0f - std::exp (-1.0f / (0.020f * (float) sr));
        wetEnvR = 1.0f - std::exp (-1.0f / (0.250f * (float) sr));

        // per-sub-block smoothing coefficients
        const float blocksPerSec = (float) sr / (float) kBlock;
        kFast = 1.0f - std::exp (-1.0f / (0.030f * blocksPerSec));
        kMed  = 1.0f - std::exp (-1.0f / (0.080f * blocksPerSec));
        kSlow = 1.0f - std::exp (-1.0f / (0.350f * blocksPerSec));

        prepared = true;
        reset();
    }

    void reset()
    {
        for (int c = 0; c < kCh; ++c)
        {
            line[c].clear(); apLine[c].clear();
            bassLp[c] = 0.0f; airLp[c] = 0.0f;
            randCur[c] = randTarget[c] = 0.0f;
        }
        for (auto& st : diffLine) for (auto& d : st) d.clear();
        for (auto& d : preDelay) d.clear();
        for (auto& ps : shifter) ps.reset();
        for (auto& f : shimHp) f.reset();
        for (auto& f : shimLp) f.reset();
        for (auto& f : lowCutF) f.reset();
        for (auto& f : highCutF) f.reset();
        for (auto& f : noiseHp) f.reset();
        for (auto& f : noiseLp) f.reset();
        for (auto& b : revBuf) std::fill (b.begin(), b.end(), 0.0f);
        shimOut[0] = shimOut[1] = 0.0f;
        revWrite = 0; revActive = false; revCountdown = 0;
        duckEnv = wetEnv = 0.0f;
        snapSmoothers = true;
    }

    void setParams (const Params& p) { target = p; }

    // Stereo in place. Any block size.
    void process (float* L, float* R, int numSamples)
    {
        if (! prepared) return;
        float sumSq = 0.0f;

        for (int start = 0; start < numSamples; start += kBlock)
        {
            const int n = std::min (kBlock, numSamples - start);
            updateControl();
            sumSq += processSub (L + start, R + start, n);
        }

        const float rms = std::sqrt (sumSq / (float) std::max (1, numSamples));
        wetLevel.store (rms, std::memory_order_relaxed);
    }

    float getWetLevel() const { return wetLevel.load (std::memory_order_relaxed); }

    static constexpr float kMaxPreDelayMs = 500.0f;

private:
    static constexpr int   kStages     = 4;
    static constexpr float kMaxScale   = 1.6f;
    static constexpr float kMaxModMs   = 2.5f;
    static constexpr float kRevBufSecs = 4.0f;
    static constexpr float kLoudness   = 1.6f;

    // ---------------------------------------------------------------- state
    double sr = 48000.0;
    bool   prepared = false, snapSmoothers = true;
    Params target;

    DelayLine diffLine[kStages][kCh];
    float     diffMs[kStages][kCh] = {};
    float     diffFlip[kStages][kCh] = {};

    DelayLine line[kCh], apLine[kCh];
    float lineMs[kCh] = {};
    int   apLen[kCh] = {};
    float bassLp[kCh] = {}, airLp[kCh] = {};
    float bassSplitA = 0.03f;

    // modulation
    float lfoX[kCh] = {}, lfoY[kCh] = {}, lfoCos[kCh] = {}, lfoSin[kCh] = {};
    float randCur[kCh] = {}, randTarget[kCh] = {};
    Rng   modRng, noiseRng, revRng;

    DelayLine preDelay[2];
    PitchShifter shifter[2];
    OnePole shimHp[2];
    Biquad  shimLp[2];
    float   shimOut[2] = {};

    Biquad  lowCutF[2], highCutF[2];
    OnePole noiseHp[2], noiseLp[2];

    // reverse splicer
    std::vector<float> revBuf[2];
    int  revBufSize = 0, revWrite = 0, revRead = 0, revLen = 0, revLeft = 0, revCountdown = 0;
    bool revActive = false;

    float duckEnv = 0.0f, duckAtk = 0.0f, duckRel = 0.0f;
    float wetEnv = 0.0f, wetEnvA = 0.0f, wetEnvR = 0.0f;
    float kFast = 0.1f, kMed = 0.05f, kSlow = 0.01f;

    std::atomic<float> wetLevel { 0.0f };

    // ------------------------------------------------- smoothed control values
    struct Ctl
    {
        float scale = 1, decay = 4, preDelay = 0, diffusion = 0.8f, damping = 0.45f, bass = 1;
        float lowCut = 80, highCut = 14000, mod = 0.3f, shimmer = 0, noise = 0, duck = 0;
        float width = 1, dry = 1, wet = 0.35f, freeze = 0;
    } cur, prev;

    // per-sub-block derived values
    float lineLen[kCh] = {}, gMid[kCh] = {}, gBassRatio[kCh] = {}, gAirRatio[kCh] = {};
    float airA = 0.3f, apG = 0.5f, modDepth = 0.0f, inGain = 1.0f, earlyGain = 0.2f;
    int   diffInt[4][kCh] = {};
    float diffFrac[4][kCh] = {};
    float shimGain = 0.0f, wetNorm = 1.0f, stageW[4] = { 0, 0, 0, 1 };
    float lastLowCut = -1.0f, lastHighCut = -1.0f;

    static float sizeToScale (float size) { return 0.22f * std::pow (kMaxScale / 0.22f, clampf (size, 0.0f, 1.0f)); }

    void updateControl()
    {
        Ctl t;
        t.scale     = sizeToScale (target.size);
        t.decay     = clampf (target.decay, 0.1f, 60.0f);
        t.preDelay  = clampf (target.preDelayMs, 0.0f, kMaxPreDelayMs - 1.0f) * 0.001f * (float) sr;
        t.diffusion = clampf (target.diffusion, 0.0f, 1.0f);
        t.damping   = clampf (target.damping, 0.0f, 1.0f);
        t.bass      = clampf (target.bass, 0.25f, 3.0f);
        t.lowCut    = clampf (target.lowCut, 10.0f, 2000.0f);
        t.highCut   = clampf (target.highCut, 500.0f, 22000.0f);
        t.mod       = clampf (target.mod, 0.0f, 1.0f);
        t.shimmer   = clampf (target.shimmer, 0.0f, 1.0f);
        t.noise     = clampf (target.noise, 0.0f, 1.0f);
        t.duck      = clampf (target.duck, 0.0f, 1.0f);
        t.width     = clampf (target.width, 0.0f, 2.0f);
        t.dry       = std::max (0.0f, target.dry);
        t.wet       = std::max (0.0f, target.wet);
        t.freeze    = target.freeze ? 1.0f : 0.0f;

        prev = cur;
        if (snapSmoothers) { cur = t; prev = t; snapSmoothers = false; }
        else
        {
            auto sm = [] (float& c, float tv, float k) { c += (tv - c) * k; };
            sm (cur.scale,     t.scale,     kSlow);
            // decay glides in log space so long → short sweeps feel even
            cur.decay = std::exp (std::log (cur.decay) + (std::log (t.decay) - std::log (cur.decay)) * kMed);
            sm (cur.preDelay,  t.preDelay,  kMed);
            sm (cur.diffusion, t.diffusion, kFast);
            sm (cur.damping,   t.damping,   kFast);
            sm (cur.bass,      t.bass,      kFast);
            sm (cur.lowCut,    t.lowCut,    kFast);
            sm (cur.highCut,   t.highCut,   kFast);
            sm (cur.mod,       t.mod,       kFast);
            sm (cur.shimmer,   t.shimmer,   kFast);
            sm (cur.noise,     t.noise,     kFast);
            sm (cur.duck,      t.duck,      kFast);
            sm (cur.width,     t.width,     kFast);
            sm (cur.dry,       t.dry,       kFast);
            sm (cur.wet,       t.wet,       kFast);
            sm (cur.freeze,    t.freeze,    kMed);
        }

        const float fs    = (float) sr;
        const float frz   = cur.freeze;
        const float msToS = fs * 0.001f;

        // ---- tank geometry & decay filters (Jot: per-line gain from its own length)
        const float rtMid  = cur.decay;
        const float rtBass = cur.decay * cur.bass;
        // damping 0 → air decays as long as mids; 1 → 12x shorter
        const float airRatioRT = 1.0f / (1.0f + 11.0f * cur.damping * cur.damping);
        const float rtAir  = cur.decay * airRatioRT;
        const float airHz  = 9500.0f * std::pow (2400.0f / 9500.0f, cur.damping);
        airA = 1.0f - std::exp (-kTwoPi * airHz / fs);

        for (int c = 0; c < kCh; ++c)
        {
            lineLen[c] = lineMs[c] * cur.scale * msToS;
            const float D  = lineLen[c] + (float) apLen[c];          // allpass adds ~its delay
            const float k  = -6.9077553f * D / fs;                    // ln(10^-3)
            const float gm = std::exp (k / rtMid);
            const float gb = std::exp (k / rtBass);
            const float ga = std::exp (k / rtAir);
            gMid[c]       = lerpf (gm, 1.0f, frz);
            gBassRatio[c] = lerpf (gb / gm, 1.0f, frz);
            gAirRatio[c]  = lerpf (ga / gm, 1.0f, frz);
        }

        apG       = 0.18f + 0.45f * cur.diffusion;
        // stage blend: 0 → first stage only (sparse, slap-like onset), 1 → all four (silky)
        {
            const float pos = cur.diffusion * (float) (kStages - 1);
            const int   lo  = std::min ((int) pos, kStages - 2);
            const float fr  = pos - (float) lo;
            for (int s = 0; s < kStages; ++s) stageW[s] = 0.0f;
            stageW[lo]     = std::sqrt (1.0f - fr);   // equal-power: stage outputs are uncorrelated
            stageW[lo + 1] = std::sqrt (fr);
        }
        // modulation depth: a faint always-on drift (breaks up metallic modes even at Mod 0)
        // plus up to ~1.6 ms of lush chorus; small rooms keep most of it because they ring most
        modDepth  = (0.12f + 1.5f * cur.mod * std::sqrt (cur.mod)) * msToS * (0.7f + 0.3f * cur.scale / kMaxScale);
        for (int s = 0; s < kStages; ++s)
            for (int c = 0; c < kCh; ++c)
            {
                const float d = 1.0f + diffMs[s][c] * cur.scale * msToS;
                diffInt[s][c]  = (int) d;
                diffFrac[s][c] = d - (float) diffInt[s][c];
            }
        inGain    = 1.0f - frz;
        earlyGain = 0.3f * (1.0f - frz);
        shimGain  = 0.62f * std::pow (cur.shimmer, 0.8f) * (1.0f - frz);

        // Loudness: steady-state tank power ∝ RT / size. Fully undo size, undo half of decay
        // (in dB) so long tails still feel bigger without jumping out of the mix.
        wetNorm   = kLoudness * std::sqrt (cur.scale) / std::pow (cur.scale + 0.3f * std::min (cur.decay, 30.0f), 0.25f);

        if (std::abs (cur.lowCut - lastLowCut) > 0.01f)
        { lowCutF[0].setHighPass (cur.lowCut, sr); lowCutF[1].setHighPass (cur.lowCut, sr); lastLowCut = cur.lowCut; }
        if (std::abs (cur.highCut - lastHighCut) > 0.5f)
        { highCutF[0].setLowPass (cur.highCut, sr); highCutF[1].setLowPass (cur.highCut, sr); lastHighCut = cur.highCut; }

        // shimmer: a few cents of opposite detune per side — wide, never static
        shifter[0].setRatio (2.0f * 0.99711f);   // -5 cents
        shifter[1].setRatio (2.0f * 1.00289f);   // +5 cents

        // smoothed random component of modulation: new target each sub-block, glides slowly
        for (int c = 0; c < kCh; ++c)
        {
            if ((modRng.uni()) < 0.02f) randTarget[c] = modRng.bipolar();
            randCur[c] += (randTarget[c] - randCur[c]) * 0.004f;
        }

        // renormalise LFO phasors (rotation recurrence drifts slowly)
        for (int c = 0; c < kCh; ++c)
        {
            const float m = 1.0f / std::sqrt (lfoX[c] * lfoX[c] + lfoY[c] * lfoY[c]);
            lfoX[c] *= m; lfoY[c] *= m;
        }
    }

    // One control sub-block, processed as a pipeline of stages:
    //   1  input: reverse splicer + pre-delay          (per sample)
    //   2  diffuser                                     (block-wise per line: sequential memory, vectorised mixing)
    //   3  late tank + shimmer                          (per sample — it's a feedback loop)
    //   4  output: EQ, air, width, duck, mix            (per sample)
    float processSub (float* L, float* R, int n)
    {
        const float fs   = (float) sr;
        const float invN = 1.0f / (float) n;
        constexpr float spreadG = 0.35355339f;   // 1/sqrt(kHalf): energy-preserving fan-out

        float pdL[kBlock], pdR[kBlock];
        float x[kCh][kBlock], inj[kCh][kBlock];
        float lateL[kBlock], lateR[kBlock], earlyL[kBlock], earlyR[kBlock];

        // ================================================================ 1  input
        const int   revCheckEvery = (int) (0.1f * fs);
        const int   revFade       = (int) (0.03f * fs);
        const float reverseAmt    = clampf (target.reverse, 0.0f, 1.0f);

        for (int i = 0; i < n; ++i)
        {
            const float pd = lerpf (prev.preDelay, cur.preDelay, (float) (i + 1) * invN);
            float feedL = L[i], feedR = R[i];

            // reverse splicer: plays recent input backwards into the tank
            if (reverseAmt > 0.0f || revActive)
            {
                revBuf[0][(size_t) revWrite] = feedL;
                revBuf[1][(size_t) revWrite] = feedR;

                if (--revCountdown <= 0)
                {
                    revCountdown = revCheckEvery;
                    if (! revActive && reverseAmt > 0.0f && revRng.uni() < reverseAmt * 0.35f)
                    {
                        revActive = true;
                        revLen    = std::min ((int) ((0.35f + revRng.uni() * 1.4f) * fs), revBufSize - 1);
                        revLeft   = revLen;
                        revRead   = revWrite;
                    }
                }
                if (revActive)
                {
                    const int el = revLen - revLeft;
                    float fade = 1.0f;
                    if (el < revFade)           { const float s = std::sin (0.5f * kPi * (float) el / (float) revFade);      fade = s * s; }
                    else if (revLeft < revFade) { const float s = std::sin (0.5f * kPi * (float) revLeft / (float) revFade); fade = s * s; }
                    const float g = fade * 0.45f * reverseAmt;
                    feedL += revBuf[0][(size_t) revRead] * g;
                    feedR += revBuf[1][(size_t) revRead] * g;
                    if (--revRead < 0) revRead = revBufSize - 1;
                    if (--revLeft <= 0) revActive = false;
                }
                if (++revWrite >= revBufSize) revWrite = 0;
            }

            // pre-delay (fractional, glides without zipper)
            preDelay[0].push (feedL);
            preDelay[1].push (feedR);
            pdL[i] = preDelay[0].readLinear (pd + 1.0f) * inGain * spreadG;
            pdR[i] = preDelay[1].readLinear (pd + 1.0f) * inGain * spreadG;
        }

        // ================================================================ 2  diffuser
        for (int c = 0; c < kCh; ++c)
        {
            const float* src = c < kHalf ? pdL : pdR;
            const float  sgn = (c & 1) ? -1.0f : 1.0f;
            for (int i = 0; i < n; ++i) { x[c][i] = src[i] * sgn; inj[c][i] = 0.0f; }
        }

        for (int s = 0; s < kStages; ++s)
        {
            // delays are fixed for the block, and this path is feed-forward, so each line
            // can be written for the whole block and then read back sequentially
            for (int c = 0; c < kCh; ++c)
            {
                diffLine[s][c].pushBlock (x[c], n);
                diffLine[s][c].readBlock (x[c], n, diffInt[s][c], diffFrac[s][c], diffFlip[s][c]);
            }

            // 16-point Hadamard across channels, vectorised along time
            for (int h = 1; h < kCh; h <<= 1)
                for (int a = 0; a < kCh; a += h << 1)
                    for (int b = a; b < a + h; ++b)
                        for (int i = 0; i < n; ++i)
                        {
                            const float p = x[b][i], q = x[b + h][i];
                            x[b][i] = p + q; x[b + h][i] = p - q;
                        }

            constexpr float norm = 0.25f;   // 1/sqrt(16)
            const float w = stageW[s];
            for (int c = 0; c < kCh; ++c)
                for (int i = 0; i < n; ++i)
                {
                    x[c][i] *= norm;
                    inj[c][i] += x[c][i] * w;
                }
        }

        for (int i = 0; i < n; ++i)
        {
            float eL = 0.0f, eR = 0.0f;
            for (int c = 0; c < kHalf; ++c)
            {
                const float sgn = (c & 1) ? -1.0f : 1.0f;
                eL += inj[c][i] * sgn;
                eR += inj[c + kHalf][i] * sgn;
            }
            earlyL[i] = eL * spreadG * earlyGain;
            earlyR[i] = eR * spreadG * earlyGain;
        }

        // ================================================================ 3  late tank
        const bool shimOn = shimGain > 1.0e-4f;
        for (int i = 0; i < n; ++i)
        {
            // Structured as gather → flat per-line math → scatter so the math loops vectorise.
            float r[kCh], fr[kCh], p0[kCh], p1[kCh], p2[kCh], p3[kCh], apd[kCh];
            for (int c = 0; c < kCh; ++c)
            {
                // rotate LFO phasor
                const float nx = lfoX[c] * lfoCos[c] - lfoY[c] * lfoSin[c];
                const float ny = lfoX[c] * lfoSin[c] + lfoY[c] * lfoCos[c];
                lfoX[c] = nx; lfoY[c] = ny;
                fr[c] = lineLen[c] + modDepth * (1.0f + 0.65f * ny + 0.35f * randCur[c]);
            }
            for (int c = 0; c < kCh; ++c)
            {
                const int k = (int) fr[c];
                fr[c] -= (float) k;
                line[c].tap4 (k, p0[c], p1[c], p2[c], p3[c]);
                apd[c] = apLine[c].readInt (apLen[c]);
            }
            for (int c = 0; c < kCh; ++c)
            {
                // 4-point Hermite
                const float f  = fr[c];
                const float c1 = 0.5f * (p2[c] - p0[c]);
                const float c2 = p0[c] - 2.5f * p1[c] + 2.0f * p2[c] - 0.5f * p3[c];
                const float c3 = 0.5f * (p3[c] - p0[c]) + 1.5f * (p1[c] - p2[c]);
                r[c] = ((c3 * f + c2) * f + c1) * f + p1[c];
            }

            float lL = 0.0f, lR = 0.0f;
            for (int c = 0; c < kHalf; ++c)
            {
                const float sgn = (c & 1) ? -1.0f : 1.0f;
                lL += r[c] * sgn;
                lR += r[c + kHalf] * sgn;
            }
            lL *= spreadG;
            lR *= spreadG;
            lateL[i] = lL;
            lateR[i] = lR;

            for (int c = 0; c < kCh; ++c)
            {
                // in-loop allpass: each pass multiplies echo density
                const float v = r[c] - apG * apd[c];
                float y = apd[c] + apG * v;
                apd[c] = v;

                // 3-band decay: bass / mid / air.  Magnitude ≤ max(gBass, gMid) < 1 → stable
                bassLp[c] += bassSplitA * (y - bassLp[c]);
                y = (y - bassLp[c]) + bassLp[c] * gBassRatio[c];
                airLp[c] += airA * (y - airLp[c]);
                y = airLp[c] + (y - airLp[c]) * gAirRatio[c];
                r[c] = y * gMid[c];
            }

            householder16 (r);

            // shimmer re-injection (from the previous sample — a one-sample loop delay is fine)
            const float s0 = shimOut[0] * spreadG, s1 = shimOut[1] * spreadG;
            for (int c = 0; c < kCh; ++c)
            {
                const float sh = (c < kHalf ? s0 : s1) * ((c & 1) ? -1.0f : 1.0f);
                apLine[c].push (apd[c]);
                line[c].push (r[c] + inj[c][i] + sh);
            }

            // shimmer: octave up, band-limited, softly saturated, fed back into the tank
            if (shimOn)
            {
                float sL = shifter[0].process (shimHp[0].hp (lL));
                float sR = shifter[1].process (shimHp[1].hp (lR));
                shimOut[0] = std::tanh (shimLp[0].process (sL) * shimGain);
                shimOut[1] = std::tanh (shimLp[1].process (sR) * shimGain);
            }
            else
            {
                shimOut[0] = shimOut[1] = 0.0f;
            }
        }

        // ================================================================ 4  output
        float sumSq = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t    = (float) (i + 1) * invN;   // intra-block interpolation
            const float dryG = lerpf (prev.dry, cur.dry, t);
            const float wetG = lerpf (prev.wet, cur.wet, t);
            const float inL  = L[i], inR = R[i];

            float wL = (lateL[i] + earlyL[i]) * wetNorm;
            float wR = (lateR[i] + earlyR[i]) * wetNorm;
            wL = highCutF[0].process (lowCutF[0].process (wL));
            wR = highCutF[1].process (lowCutF[1].process (wR));

            // noise / air: band-limited grain that rides the tail envelope
            const float wa = 0.5f * (std::abs (wL) + std::abs (wR));
            wetEnv += (wa > wetEnv ? wetEnvA : wetEnvR) * (wa - wetEnv);
            if (cur.noise > 1.0e-4f)
            {
                const float amp = cur.noise * (0.9f * wetEnv + 0.0004f);
                wL += noiseLp[0].lp (noiseHp[0].hp (noiseRng.bipolar())) * amp;
                wR += noiseLp[1].lp (noiseHp[1].hp (noiseRng.bipolar())) * amp;
            }

            // width (mid/side)
            const float mid  = 0.5f * (wL + wR);
            const float side = 0.5f * (wL - wR) * cur.width;
            wL = mid + side;
            wR = mid - side;

            // ducking: dry level pushes the tail down, it blooms back in the gaps
            const float da = 0.5f * (std::abs (inL) + std::abs (inR));
            duckEnv += (da > duckEnv ? duckAtk : duckRel) * (da - duckEnv);
            const float duckG = 1.0f - cur.duck * clampf (duckEnv * 6.0f, 0.0f, 0.92f);

            wL = softClip (wL * duckG * wetG);
            wR = softClip (wR * duckG * wetG);
            sumSq += 0.5f * (wL * wL + wR * wR);

            // only the wet path is ever limited — the dry signal passes untouched
            L[i] = inL * dryG + wL;
            R[i] = inR * dryG + wR;
        }
        return sumSq;
    }

    // transparent below -1.4 dBFS, smooth knee to a 1.0 ceiling (safety net for runaway shimmer)
    static inline float softClip (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.85f) return x;
        const float y = 0.85f + 0.15f * std::tanh ((a - 0.85f) / 0.15f);
        return x < 0.0f ? -y : y;
    }
};

} // namespace mr
