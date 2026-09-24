// bbm_render — run a WAV file through the engine, offline. A build-host tool for
// listening and for A/B comparisons (e.g. against a NAM capture); never shipped.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
//
//   bbm_render [--sustain k] [--tone k] [--volume k] [--gate k] [--trim dB]
//              in.wav out.wav
//
// Reads 16/24/32-bit PCM or 32-bit float WAV (channels are averaged, as the plug-in
// does with a stereo input) and writes mono 32-bit float at the input's rate. Knobs
// are 0..1; the defaults are the plug-in's. The gate defaults to off here, since an
// A/B wants the pedal alone.
#include "dsp/BigMuffPi.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Audio {
  double rate = 0.0;
  std::vector<float> samples;
};

std::uint32_t le32(const unsigned char *p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t le16(const unsigned char *p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

bool readWav(const char *path, Audio &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;
  std::vector<unsigned char> b;
  f.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(f.tellg());
  f.seekg(0);
  b.resize(size);
  if (!f.read(reinterpret_cast<char *>(b.data()), static_cast<std::streamsize>(size)))
    return false;
  if (size < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 ||
      std::memcmp(b.data() + 8, "WAVE", 4) != 0)
    return false;
  unsigned format = 0, channels = 0, bits = 0;
  for (std::size_t at = 12; at + 8 <= size;) {
    const std::uint32_t len = le32(b.data() + at + 4);
    const unsigned char *body = b.data() + at + 8;
    if (at + 8 + len > size)
      return false;
    if (std::memcmp(b.data() + at, "fmt ", 4) == 0 && len >= 16) {
      format = le16(body);
      channels = le16(body + 2);
      out.rate = le32(body + 4);
      bits = le16(body + 14);
      if (format == 0xFFFE && len >= 26) // WAVE_FORMAT_EXTENSIBLE: the sub-format
        format = le16(body + 24);
    } else if (std::memcmp(b.data() + at, "data", 4) == 0 && channels > 0) {
      const std::size_t width = bits / 8;
      const std::size_t frames = len / (width * channels);
      out.samples.assign(frames, 0.0f);
      for (std::size_t n = 0; n < frames; ++n) {
        double sum = 0.0;
        for (std::size_t c = 0; c < channels; ++c) {
          const unsigned char *s = body + (n * channels + c) * width;
          double v = 0.0;
          if (format == 3 && bits == 32) {
            float x = 0.0f;
            std::memcpy(&x, s, 4);
            v = x;
          } else if (format == 1 && bits == 16) {
            v = static_cast<std::int16_t>(le16(s)) / 32768.0;
          } else if (format == 1 && bits == 24) {
            // Three bytes into the top of an int32, so the sign comes along.
            const std::uint32_t u = (static_cast<std::uint32_t>(s[0]) << 8) |
                                    (static_cast<std::uint32_t>(s[1]) << 16) |
                                    (static_cast<std::uint32_t>(s[2]) << 24);
            v = static_cast<std::int32_t>(u) / 2147483648.0;
          } else if (format == 1 && bits == 32) {
            v = static_cast<std::int32_t>(le32(s)) / 2147483648.0;
          } else {
            return false;
          }
          sum += v;
        }
        out.samples[n] = static_cast<float>(sum / channels);
      }
      return out.rate > 0.0;
    }
    at += 8 + len + (len & 1);
  }
  return false;
}

void put32(std::ofstream &f, std::uint32_t v) {
  const unsigned char b[4] = {
      static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
      static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
  f.write(reinterpret_cast<const char *>(b), 4);
}
void put16(std::ofstream &f, std::uint16_t v) {
  const unsigned char b[2] = {static_cast<unsigned char>(v),
                              static_cast<unsigned char>(v >> 8)};
  f.write(reinterpret_cast<const char *>(b), 2);
}

bool writeWav(const char *path, const Audio &a) {
  std::ofstream f(path, std::ios::binary);
  if (!f)
    return false;
  const auto bytes = static_cast<std::uint32_t>(a.samples.size() * 4);
  const auto rate = static_cast<std::uint32_t>(a.rate);
  f.write("RIFF", 4);
  put32(f, 36 + bytes);
  f.write("WAVEfmt ", 8);
  put32(f, 16);
  put16(f, 3); // IEEE float
  put16(f, 1);
  put32(f, rate);
  put32(f, rate * 4);
  put16(f, 4);
  put16(f, 32);
  f.write("data", 4);
  put32(f, bytes);
  f.write(reinterpret_cast<const char *>(a.samples.data()), bytes);
  return static_cast<bool>(f);
}

} // namespace

int main(int argc, char **argv) {
  bbm::Controls c;
  c.gate = 0.0f;
  std::vector<const char *> files;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&](float &dst) {
      if (i + 1 >= argc)
        return false;
      dst = std::strtof(argv[++i], nullptr);
      return true;
    };
    bool ok = true;
    if (arg == "--sustain")
      ok = value(c.sustain);
    else if (arg == "--tone")
      ok = value(c.tone);
    else if (arg == "--volume")
      ok = value(c.volume);
    else if (arg == "--gate")
      ok = value(c.gate);
    else if (arg == "--trim")
      ok = value(c.outputTrimDb);
    else
      files.push_back(argv[i]);
    if (!ok)
      files.clear();
  }
  if (files.size() != 2) {
    std::fprintf(stderr, "usage: bbm_render [--sustain k] [--tone k] [--volume k] "
                         "[--gate k] [--trim dB] in.wav out.wav\n");
    return 2;
  }
  Audio a;
  if (!readWav(files[0], a)) {
    std::fprintf(stderr, "bbm_render: cannot read %s (PCM 16/24/32 or float32 WAV)\n",
                 files[0]);
    return 1;
  }
  constexpr int kBlock = 512;
  bbm::BigMuffPi engine;
  engine.setControls(c);
  engine.prepare(a.rate, kBlock);
  engine.process(a.samples.data(), a.samples.data(), a.samples.size());
  if (!writeWav(files[1], a)) {
    std::fprintf(stderr, "bbm_render: cannot write %s\n", files[1]);
    return 1;
  }
  std::printf("%s: %zu samples at %.0f Hz, sustain %.2f tone %.2f volume %.2f\n",
              files[1], a.samples.size(), a.rate, double(c.sustain), double(c.tone),
              double(c.volume));
  return 0;
}
