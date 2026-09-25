// BigBubbleMuff — user presets on disk. See presetstore.h for the format and rules.
// Copyright (C) 2026  BigBubbleMuff contributors. SPDX-License-Identifier: MIT
#include "presets/presetstore.h"

#include "dsp/Checked.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <system_error>

namespace bbm::presets {

namespace {

constexpr std::string_view kHeader = "#BBMPRESET ";

// An owned file descriptor.
class Fd {
public:
  explicit Fd(int fd) noexcept : mFd(fd) {}
  ~Fd() {
    if (mFd >= 0)
      ::close(mFd);
  }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
  Fd(Fd &&) = delete;
  Fd &operator=(Fd &&) = delete;
  int get() const noexcept { return mFd; }
  bool ok() const noexcept { return mFd >= 0; }
  // Close now and report whether the close itself succeeded (a deferred write
  // error can surface here).
  bool close() noexcept {
    const int fd = mFd;
    mFd = -1;
    return fd < 0 || ::close(fd) == 0;
  }

private:
  int mFd;
};

struct DirCloser {
  void operator()(DIR *d) const noexcept { ::closedir(d); }
};

bool nameChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == ' ' || c == '.' || c == '_' || c == '-';
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    s.remove_suffix(1);
  return s;
}

// std::from_chars: locale-independent, no exceptions, and it must consume the
// whole field.
bool parseDouble(std::string_view s, double &out) {
  if (s.empty())
    return false;
  const char *const end = &s.front() + s.size();
  const auto [ptr, ec] = std::from_chars(&s.front(), end, out);
  return ec == std::errc() && ptr == end && std::isfinite(out);
}

bool parseInt(std::string_view s, int &out) {
  if (s.empty())
    return false;
  const char *const end = &s.front() + s.size();
  const auto [ptr, ec] = std::from_chars(&s.front(), end, out);
  return ec == std::errc() && ptr == end;
}

// mkdir, leaving an existing directory alone. Creates with 0700.
bool makeDir(const std::string &path) {
  if (::mkdir(path.c_str(), 0700) == 0)
    return true;
  if (errno != EEXIST)
    return false;
  struct stat st{};
  return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool writeAll(int fd, std::string_view text) {
  while (!text.empty()) {
    const ssize_t n = ::write(fd, text.data(), text.size());
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    text.remove_prefix(static_cast<std::size_t>(n));
  }
  return true;
}

bool lessName(const std::string &a, const std::string &b) {
  const auto lower = [](char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  };
  const auto [ia, ib] =
      std::mismatch(a.begin(), a.end(), b.begin(), b.end(),
                    [&](char x, char y) { return lower(x) == lower(y); });
  if (ia != a.end() && ib != b.end())
    return lower(*ia) < lower(*ib);
  if (a.size() != b.size())
    return a.size() < b.size();
  return a < b; // same letters: fall back to byte order so the sort is total
}

} // namespace

//------------------------------------------------------------------------
bool nameIsSafe(std::string_view name) {
  if (name.empty() || name.size() > kMaxNameLength)
    return false;
  if (name.front() == '.' || name.back() == ' ')
    return false;
  return std::all_of(name.begin(), name.end(), nameChar);
}

std::string serialise(const Norms &norm) {
  std::string out(kHeader);
  out += std::to_string(kFormatVersion);
  out += '\n';
  for (const Key &k : kKeys) {
    const double plain = toPlain(at(kParams, k.id), at(norm, k.id));
    // Shortest text that reads back to the same double.
    std::array<char, 32> buf{};
    const auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), plain);
    if (ec != std::errc())
      continue; // cannot happen for a finite double in 32 chars
    out += k.key;
    out += '=';
    out.append(buf.data(), ptr);
    out += '\n';
  }
  return out;
}

bool parse(std::string_view text, Norms &out) {
  if (text.size() > kMaxFileBytes)
    return false;

  Norms v{};
  for (int i = 0; i < kParamCount; ++i)
    at(v, i) = toNorm(at(kParams, i), at(kParams, i).def);
  // The parameters a preset does not carry keep what `out` has.
  for (int i = 0; i < kParamCount; ++i)
    if (std::none_of(kKeys.begin(), kKeys.end(),
                     [i](const Key &k) { return static_cast<int>(k.id) == i; }))
      at(v, i) = at(out, i);

  bool sawHeader = false;
  while (!text.empty()) {
    const std::size_t nl = text.find('\n');
    const std::string_view line =
        trim(text.substr(0, nl == std::string_view::npos ? text.size() : nl));
    text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);

    if (!sawHeader) {
      // The first line must be the header; no leading junk, no BOM.
      if (line.substr(0, kHeader.size()) != kHeader)
        return false;
      int version = 0;
      if (!parseInt(trim(line.substr(kHeader.size())), version) || version < 1 ||
          version > kFormatVersion)
        return false;
      sawHeader = true;
      continue;
    }
    if (line.empty() || line.front() == '#')
      continue;
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos)
      return false;
    const std::string_view key = trim(line.substr(0, eq));
    const std::string_view value = trim(line.substr(eq + 1));
    const auto it = std::find_if(kKeys.begin(), kKeys.end(),
                                 [key](const Key &k) { return key == k.key; });
    if (it == kKeys.end())
      continue; // unknown keys are ignored (forward compatibility)
    double plain = 0.0;
    if (!parseDouble(value, plain))
      return false;
    at(v, it->id) = toNorm(at(kParams, it->id), plain); // clamps
  }
  if (!sawHeader)
    return false;
  out = v;
  return true;
}

//------------------------------------------------------------------------
std::string Store::defaultDir() {
  const char *home = std::getenv("HOME");
  if (home == nullptr || home[0] != '/')
    return {};
  std::string dir(home);
  while (dir.size() > 1 && dir.back() == '/')
    dir.pop_back();
  if (dir == "/")
    dir.clear();
  return dir + "/.config/BigBubbleMuff/Presets";
}

std::string Store::pathFor(std::string_view name) const {
  if (mDir.empty() || !nameIsSafe(name))
    return {};
  std::string path = mDir;
  path += '/';
  path += name;
  path += kExtension;
  return path;
}

bool Store::ensureDir() const {
  if (mDir.empty() || mDir.front() != '/')
    return false;
  // Create each missing component (0700). Existing ones are left exactly as they
  // are: this never changes permissions it did not set.
  for (std::size_t pos = mDir.find('/', 1); pos != std::string::npos;
       pos = mDir.find('/', pos + 1))
    if (!makeDir(mDir.substr(0, pos)))
      return false;
  return makeDir(mDir);
}

std::vector<std::string> Store::list() const {
  std::vector<std::string> names;
  if (mDir.empty())
    return names;
  const std::unique_ptr<DIR, DirCloser> dir(::opendir(mDir.c_str()));
  if (!dir)
    return names;
  const int dfd = ::dirfd(dir.get());
  for (const dirent *e = ::readdir(dir.get()); e != nullptr && names.size() < kMaxListed;
       e = ::readdir(dir.get())) {
    const std::string_view file(static_cast<const char *>(e->d_name));
    if (file.size() <= kExtension.size() ||
        file.substr(file.size() - kExtension.size()) != kExtension)
      continue;
    const std::string_view name = file.substr(0, file.size() - kExtension.size());
    if (!nameIsSafe(name))
      continue;
    // Regular files only, judged without following a symlink.
    struct stat st{};
    if (::fstatat(dfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode))
      continue;
    names.emplace_back(name);
  }
  std::sort(names.begin(), names.end(), lessName);
  return names;
}

bool Store::exists(std::string_view name) const {
  const std::string path = pathFor(name);
  struct stat st{};
  return !path.empty() && ::lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool Store::save(std::string_view name, const Norms &norm) const {
  const std::string path = pathFor(name);
  if (path.empty() || !ensureDir())
    return false;
  const std::string text = serialise(norm);
  const std::string temp = path + ".tmp";

  // O_NOFOLLOW: a symlink planted at the temp name is refused, not written through.
  ::unlink(temp.c_str());
  Fd fd(::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (!fd.ok())
    return false;
  const bool written = writeAll(fd.get(), text) && ::fsync(fd.get()) == 0;
  if (!fd.close() || !written) {
    ::unlink(temp.c_str());
    return false;
  }
  // Renamed into place, so an interrupted save leaves the previous preset intact.
  if (::rename(temp.c_str(), path.c_str()) != 0) {
    ::unlink(temp.c_str());
    return false;
  }
  return true;
}

bool Store::load(std::string_view name, Norms &out) const {
  const std::string path = pathFor(name);
  if (path.empty())
    return false;
  const Fd fd(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  if (!fd.ok())
    return false;
  struct stat st{};
  if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
      static_cast<std::size_t>(st.st_size) > kMaxFileBytes)
    return false;

  // Read at most one byte past the cap, so a file that grew after fstat is still
  // caught rather than truncated into something that parses.
  std::string text(kMaxFileBytes + 1, '\0');
  std::size_t got = 0;
  while (got < text.size()) {
    const ssize_t n = ::read(fd.get(), text.data() + got, text.size() - got);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
      break;
    got += static_cast<std::size_t>(n);
  }
  if (got > kMaxFileBytes)
    return false;
  text.resize(got);
  return parse(text, out);
}

bool Store::remove(std::string_view name) const {
  const std::string path = pathFor(name);
  if (path.empty())
    return false;
  struct stat st{};
  if (::lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return false;
  return ::unlink(path.c_str()) == 0;
}

} // namespace bbm::presets
