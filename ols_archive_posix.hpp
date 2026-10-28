// POSIX implementation of the archive file hooks (CLI and tests on Linux/WSL).
#pragma once
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include "ols_archive.hpp"

namespace olsarc {

inline std::string narrowPath(const ws& path) { return toUtf8(path); }

struct PosixFile : RandomFile, ArchiveFileOps::WriteFile {
  int fd = -1;
  uint64_t pos = 0;
  ~PosixFile() override { if (fd >= 0) ::close(fd); }
  uint64_t size() const override {
    struct stat st;
    return fstat(fd, &st) == 0 ? (uint64_t)st.st_size : 0;
  }
  bool readAt(uint64_t off, void* dst, size_t n) override {
    uint8_t* p = static_cast<uint8_t*>(dst);
    while (n) {
      const ssize_t got = ::pread(fd, p, n, (off_t)off);
      if (got <= 0) { if (got < 0 && errno == EINTR) continue; return false; }
      p += got; n -= (size_t)got; off += (uint64_t)got;
    }
    return true;
  }
  bool writeAt(uint64_t off, const void* src, size_t n) override {
    const uint8_t* p = static_cast<const uint8_t*>(src);
    while (n) {
      const ssize_t put = ::pwrite(fd, p, n, (off_t)off);
      if (put <= 0) { if (put < 0 && errno == EINTR) continue; return false; }
      p += put; n -= (size_t)put; off += (uint64_t)put;
    }
    return true;
  }
  bool write(const void* src, size_t n) override {
    if (!writeAt(pos, src, n)) return false;
    pos += n;
    return true;
  }
  uint64_t position() const override { return pos; }
  bool truncate(uint64_t newSize) override { return ::ftruncate(fd, (off_t)newSize) == 0; }
  bool flush() override { return ::fsync(fd) == 0; }
};

struct PosixFileOps : ArchiveFileOps {
  std::unique_ptr<ByteSource> openRead(const ws& path) override {
    auto f = std::make_unique<PosixFile>();
    f->fd = ::open(narrowPath(path).c_str(), O_RDONLY);
    if (f->fd < 0) return nullptr;
    return f;
  }
  std::unique_ptr<RandomFile> openReadWrite(const ws& path) override {
    auto f = std::make_unique<PosixFile>();
    f->fd = ::open(narrowPath(path).c_str(), O_RDWR);
    if (f->fd < 0) return nullptr;
    return f;
  }
  std::unique_ptr<WriteFile> create(const ws& path) override {
    auto f = std::make_unique<PosixFile>();
    f->fd = ::open(narrowPath(path).c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
    if (f->fd < 0) return nullptr;
    return f;
  }
  bool writeDurable(const ws& path, const Bytes& data) override {
    const int fd = ::open(narrowPath(path).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    PosixFile f;
    f.fd = fd;
    const bool ok = f.write(data.data(), data.size()) && f.flush();
    return ok;
  }
  bool readAll(const ws& path, Bytes& data) override {
    auto f = openRead(path);
    if (!f) return false;
    data.resize((size_t)f->size());
    return data.empty() || f->readAt(0, data.data(), data.size());
  }
  bool exists(const ws& path) override {
    struct stat st;
    return ::stat(narrowPath(path).c_str(), &st) == 0;
  }
  bool remove(const ws& path) override { return ::unlink(narrowPath(path).c_str()) == 0; }
  bool replace(const ws& target, const ws& replacement) override {
    return ::rename(narrowPath(replacement).c_str(), narrowPath(target).c_str()) == 0;
  }
  uint64_t freeSpace(const ws& path) override {
    std::string p = narrowPath(path);
    const size_t slash = p.find_last_of('/');
    p = slash == std::string::npos ? std::string(".") : p.substr(0, slash + 1);
    struct statvfs v;
    return statvfs(p.c_str(), &v) == 0 ? (uint64_t)v.f_bavail * v.f_frsize : 0;
  }
};

}  // namespace olsarc
