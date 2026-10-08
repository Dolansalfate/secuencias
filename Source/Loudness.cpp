#include "Loudness.h"
#include <cmath>
#include <algorithm>
#include <deque>

namespace
{
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        inline double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // Ponderación K (BS.1770-4) para cualquier frecuencia de muestreo, como en libebur128
    void makeKWeighting (double fs, Biquad& shelf, Biquad& highpass)
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            highpass.b0 = 1.0;
            highpass.b1 = -2.0;
            highpass.b2 = 1.0;
            highpass.a1 = 2.0 * (K * K - 1.0) / a0;
            highpass.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    double toLufs (double meanSquareSum)
    {
        return meanSquareSum > 1.0e-12 ? -0.691 + 10.0 * std::log10 (meanSquareSum) : loudness::unknown;
    }

    // Sonoridad integrada con puertas de un conjunto de bloques (potencia por bloque, suma de canales)
    double gatedLoudness (const std::vector<double>& blockPower, size_t from, size_t to)
    {
        double sum = 0.0;
        size_t count = 0;
        for (size_t i = from; i < to; ++i)
            if (toLufs (blockPower[i]) > -70.0) { sum += blockPower[i]; ++count; }
        if (count == 0)
            return loudness::unknown;
        const double relative = toLufs (sum / (double) count) - 10.0;
        sum = 0.0;
        count = 0;
        for (size_t i = from; i < to; ++i)
            if (toLufs (blockPower[i]) > relative) { sum += blockPower[i]; ++count; }
        return count > 0 ? toLufs (sum / (double) count) : loudness::unknown;
    }

    // Filtro polifásico x4 (sinc enventanado, 12 coeficientes por fase) para el pico real
    struct Upsampler4
    {
        static constexpr int phases = 4, taps = 12;
        double h[phases][taps];
        Upsampler4()
        {
            const int total = phases * taps;
            for (int n = 0; n < total; ++n)
            {
                const double t = ((double) n - (total - 1) / 2.0) / (double) phases;
                const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                const double window = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (n + 0.5) / total);
                h[n % phases][n / phases] = sinc * window;
            }
            for (int p = 0; p < phases; ++p)   // ganancia 1 en continua por fase
            {
                double sum = 0.0;
                for (int j = 0; j < taps; ++j) sum += h[p][j];
                for (int j = 0; j < taps; ++j) h[p][j] /= sum;
            }
        }
        double peakOf (const float* x, int n) const
        {
            double peak = 0.0;
            for (int i = 0; i < n; ++i)
            {
                peak = std::max (peak, (double) std::abs (x[i]));
                for (int p = 0; p < phases; ++p)
                {
                    double acc = 0.0;
                    for (int j = 0; j < taps; ++j)
                    {
                        const int k = i - j + taps / 2;
                        if (k >= 0 && k < n)
                            acc += h[p][j] * x[k];
                    }
                    peak = std::max (peak, std::abs (acc));
                }
            }
            return peak;
        }
    };
}

namespace loudness
{
    Result measure (const juce::AudioBuffer<float>& audio, double fs, const std::vector<Section>& sections, bool truePeak)
    {
        Result r;
        r.sections = sections;
        const int n = audio.getNumSamples();
        const int channels = juce::jmin (2, audio.getNumChannels());
        if (n <= 0 || channels <= 0 || fs <= 0.0)
            return r;

        // 1) Potencia media por bloque de 400 ms cada 100 ms, señal ponderada K, suma de canales
        const int blockLen = (int) std::lround (0.4 * fs), hop = (int) std::lround (0.1 * fs);
        std::vector<double> blockPower;
        std::vector<double> weightedSq ((size_t) n, 0.0);   // suma de canales del cuadrado ponderado
        for (int ch = 0; ch < channels; ++ch)
        {
            Biquad shelf, hp;
            makeKWeighting (fs, shelf, hp);
            const float* x = audio.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const double y = hp.process (shelf.process ((double) x[i]));
                weightedSq[(size_t) i] += y * y;
            }
        }
        if (n >= blockLen)
        {
            std::vector<double> prefix ((size_t) n + 1, 0.0);
            for (int i = 0; i < n; ++i)
                prefix[(size_t) i + 1] = prefix[(size_t) i] + weightedSq[(size_t) i];
            for (int start = 0; start + blockLen <= n; start += hop)
                blockPower.push_back ((prefix[(size_t) (start + blockLen)] - prefix[(size_t) start]) / (double) blockLen);
        }

        // 2) Sonoridad integrada de todo y por tramo (bloques enteramente dentro del tramo)
        r.whole.lufs = gatedLoudness (blockPower, 0, blockPower.size());
        for (auto& s : r.sections)
        {
            s.result = {};
            if (s.endSeconds <= s.startSeconds)
                continue;
            size_t from = blockPower.size(), to = 0;
            for (size_t b = 0; b < blockPower.size(); ++b)
            {
                const double blockStart = (double) b * hop / fs, blockEnd = ((double) b * hop + blockLen) / fs;
                if (blockStart >= s.startSeconds - 1.0e-6 && blockEnd <= s.endSeconds + 1.0e-6)
                {
                    from = std::min (from, b);
                    to = b + 1;
                }
            }
            if (to > from)
                s.result.lufs = gatedLoudness (blockPower, from, to);
        }

        // 3) Pico real (x4) de todo y por tramo
        if (! truePeak)
            return r;
        static const Upsampler4 upsampler;
        auto peakDb = [&] (int from, int to)
        {
            double peak = 0.0;
            for (int ch = 0; ch < channels; ++ch)
                peak = std::max (peak, upsampler.peakOf (audio.getReadPointer (ch) + from, to - from));
            return peak > 1.0e-9 ? 20.0 * std::log10 (peak) : unknown;
        };
        r.whole.truePeakDb = peakDb (0, n);
        for (auto& s : r.sections)
        {
            const int from = juce::jlimit (0, n, (int) std::lround (s.startSeconds * fs));
            const int to = juce::jlimit (0, n, (int) std::lround (s.endSeconds * fs));
            if (to > from)
                s.result.truePeakDb = peakDb (from, to);
        }
        return r;
    }

    double limiterGains (const juce::AudioBuffer<float>& audio, float gain, float ceiling, int lookahead, int release,
                         std::vector<float>& out)
    {
        const int n = audio.getNumSamples();
        const int channels = juce::jmin (2, audio.getNumChannels());
        out.assign ((size_t) juce::jmax (0, n), 1.0f);
        if (n <= 0 || channels <= 0 || ! (gain > 0.0f) || ! (ceiling > 0.0f))
            return 0.0;
        lookahead = juce::jmax (1, lookahead);

        // Ganancia necesaria en cada muestra (pico de la muestra o entre ella y la siguiente)
        std::vector<float> need ((size_t) n, 1.0f);
        bool any = false;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* x = audio.getReadPointer (ch);
            auto at = [x, n] (int i) { return x[juce::jlimit (0, n - 1, i)]; };
            for (int i = 0; i < n; ++i)
            {
                const float mid = (-at (i - 1) + 9.0f * at (i) + 9.0f * at (i + 1) - at (i + 2)) * (1.0f / 16.0f);
                const float peak = juce::jmax (std::abs (x[i]), std::abs (mid)) * gain;
                if (peak > ceiling)
                {
                    need[(size_t) i] = juce::jmin (need[(size_t) i], ceiling / peak);
                    any = true;
                }
            }
        }
        if (! any)
            return 0.0;

        // a[i] = mínimo de need en [i, i + lookahead] (cola monótona); b = media de a en [i - lookahead + 1, i]: en cada
        // pico p, todos los a promediados ya lo incluyen, así b[p] <= need[p]. Después la vuelta con un polo (solo sube
        // despacio; nunca por encima de b, así tampoco de need)
        std::vector<float> ahead ((size_t) n, 1.0f);
        std::deque<int> window;
        for (int i = n - 1; i >= 0; --i)
        {
            while (! window.empty() && need[(size_t) window.back()] >= need[(size_t) i])
                window.pop_back();
            window.push_back (i);
            while (window.front() > i + lookahead)
                window.pop_front();
            ahead[(size_t) i] = need[(size_t) window.front()];
        }
        // Antes del audio, la ventana se llena con a[0] (el mínimo de las primeras muestras): un pico al principio
        // también queda cubierto
        const float releaseCoef = 1.0f / (float) juce::jmax (1, release);
        const float before = ahead[0];
        double sum = (double) before * lookahead;
        float g = before, lowest = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            sum += (double) ahead[(size_t) i] - (double) (i >= lookahead ? ahead[(size_t) (i - lookahead)] : before);
            const float target = juce::jlimit (0.0f, 1.0f, (float) (sum / lookahead));
            g = target < g ? target : g + (target - g) * releaseCoef;
            out[(size_t) i] = g;
            lowest = juce::jmin (lowest, g);
        }
        return juce::Decibels::gainToDecibels (lowest, -100.0f);
    }

    double gainToTarget (const Measurement& m, double targetLufs, double maxPeakDb)
    {
        if (m.lufs <= unknown + 1.0)
            return 0.0;
        double gain = targetLufs - m.lufs;
        if (m.truePeakDb > unknown + 1.0)
            gain = std::min (gain, maxPeakDb - m.truePeakDb);   // sin pasar del pico permitido
        return juce::jlimit (-40.0, 40.0, gain);
    }
}
