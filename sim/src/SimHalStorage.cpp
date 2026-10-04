// CrossPoint simulator: HalStorage/HalFile on the host filesystem under
// sim::storageRoot(). Firmware paths ("/.crosspoint/x.json") map 1:1 below it.
#include <HalStorage.h>
#include <Logging.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <string>

#include "SimHost.h"
#include "SimStorage.h"

namespace {
std::string root = "/tmp/crosspoint-sim-sd";

std::string hostPath(const char* path) {
  std::string p = path ? path : "/";
  if (p.empty() || p[0] != '/') p = "/" + p;
  return root + p;
}

bool mkdirs(const std::string& full) {
  std::string cur;
  size_t pos = 0;
  while (pos != std::string::npos) {
    pos = full.find('/', pos + 1);
    cur = full.substr(0, pos);
    if (cur.empty()) continue;
    struct stat sb {};
    if (stat(cur.c_str(), &sb) != 0 && ::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
  }
  return true;
}
}  // namespace

namespace sim {
void setStorageRoot(const char* hostRoot) {
  root = hostRoot ? hostRoot : "";
  while (!root.empty() && root.back() == '/') root.pop_back();
  mkdirs(root);
}
const char* storageRoot() { return root.c_str(); }
}  // namespace sim

class HalFile::Impl {
 public:
  FILE* fp = nullptr;
  DIR* dir = nullptr;
  std::string path;  // firmware path
  bool writable = false;
  ~Impl() { closeAll(); }
  void closeAll() {
    if (fp) fclose(fp);
    if (dir) closedir(dir);
    fp = nullptr;
    dir = nullptr;
  }
};

HalStorage HalStorage::instance;

HalStorage::HalStorage() { storageMutex = xSemaphoreCreateRecursiveMutex(); }
bool HalStorage::begin() {
  initialized = mkdirs(root);
  return initialized;
}
bool HalStorage::ready() const { return true; }
void HalStorage::prepareForDeepSleep() {}
bool HalStorage::beginUsbDrive() { return false; }
bool HalStorage::disconnectUsbDriveHost() { return false; }
void HalStorage::endUsbDrive() {}
UsbDriveState HalStorage::usbDriveState() const { return UsbDriveState::Unsupported; }
bool HalStorage::usbDriveHostSuspended() const { return false; }

std::vector<String> HalStorage::listFiles(const char* path, const int maxFiles) {
  std::vector<String> out;
  DIR* d = opendir(hostPath(path).c_str());
  if (!d) return out;
  while (dirent* e = readdir(d)) {
    if (static_cast<int>(out.size()) >= maxFiles) break;
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    out.emplace_back(e->d_name);
  }
  closedir(d);
  return out;
}

String HalStorage::readFile(const char* path) {
  FILE* f = fopen(hostPath(path).c_str(), "rb");
  if (!f) return String();
  std::string data;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  fclose(f);
  return String(data);
}

bool HalStorage::readFileToStream(const char* path, Print& out, const size_t chunkSize) {
  FILE* f = fopen(hostPath(path).c_str(), "rb");
  if (!f) return false;
  std::string buf(chunkSize ? chunkSize : 256, '\0');
  size_t n;
  while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) out.write(reinterpret_cast<const uint8_t*>(buf.data()), n);
  fclose(f);
  return true;
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, const size_t bufferSize, const size_t maxBytes) {
  if (!buffer || bufferSize == 0) return 0;
  FILE* f = fopen(hostPath(path).c_str(), "rb");
  if (!f) return 0;
  size_t cap = bufferSize - 1;
  if (maxBytes && maxBytes < cap) cap = maxBytes;
  const size_t n = fread(buffer, 1, cap, f);
  buffer[n] = '\0';
  fclose(f);
  return n;
}

bool HalStorage::readFileToString(const char* moduleName, const std::string& path, const size_t cap, std::string& out) {
  FILE* f = fopen(hostPath(path.c_str()).c_str(), "rb");
  if (!f) return false;
  out.clear();
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    if (out.size() + n > cap) {
      fclose(f);
      LOG_ERR(moduleName, "%s exceeds %u bytes", path.c_str(), static_cast<unsigned>(cap));
      return false;
    }
    out.append(buf, n);
  }
  fclose(f);
  return true;
}

bool HalStorage::writeFile(const char* path, const String& content) {
  const std::string full = hostPath(path);
  mkdirs(full.substr(0, full.rfind('/')));
  FILE* f = fopen(full.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(content.c_str(), 1, content.length(), f) == content.length();
  fclose(f);
  sim::hostStorageChanged();
  return ok;
}

bool HalStorage::ensureDirectoryExists(const char* path) { return mkdirs(hostPath(path)); }

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  auto impl = std::make_unique<HalFile::Impl>();
  impl->path = path ? path : "/";
  const std::string full = hostPath(path);
  struct stat sb {};
  if (stat(full.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
    impl->dir = opendir(full.c_str());
    return HalFile(std::move(impl));
  }
  const int acc = oflag & O_ACCMODE;
  const char* mode = "rb";
  if (acc != O_RDONLY) {
    impl->writable = true;
    if (oflag & O_TRUNC) {
      mode = acc == O_RDWR ? "w+b" : "wb";
    } else if (oflag & O_APPEND) {
      mode = acc == O_RDWR ? "a+b" : "ab";
    } else {
      // Create if needed, keep contents, start at offset 0.
      FILE* probe = fopen(full.c_str(), "rb");
      if (probe) {
        fclose(probe);
        mode = "r+b";
      } else if (oflag & O_CREAT) {
        mode = "w+b";
      } else {
        return HalFile(std::move(impl));
      }
    }
    mkdirs(full.substr(0, full.rfind('/')));
  }
  impl->fp = fopen(full.c_str(), mode);
  return HalFile(std::move(impl));
}

bool HalStorage::mkdir(const char* path, bool) {
  const bool ok = mkdirs(hostPath(path));
  return ok;
}
bool HalStorage::exists(const char* path) {
  struct stat sb {};
  return stat(hostPath(path).c_str(), &sb) == 0;
}
bool HalStorage::remove(const char* path) {
  const bool ok = ::unlink(hostPath(path).c_str()) == 0;
  if (ok) sim::hostStorageChanged();
  return ok;
}
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  const bool ok = ::rename(hostPath(oldPath).c_str(), hostPath(newPath).c_str()) == 0;
  if (ok) sim::hostStorageChanged();
  return ok;
}
bool HalStorage::replaceFile(const char* tmpPath, const char* path) {
  ::unlink(hostPath(path).c_str());
  return rename(tmpPath, path);
}
bool HalStorage::rmdir(const char* path) { return ::rmdir(hostPath(path).c_str()) == 0; }

bool HalStorage::openFileForRead(const char*, const char* path, HalFile& file) {
  file = open(path, O_RDONLY);
  return file.isOpen() && !file.isDirectory();
}
bool HalStorage::openFileForRead(const char* m, const std::string& path, HalFile& file) {
  return openFileForRead(m, path.c_str(), file);
}
bool HalStorage::openFileForRead(const char* m, const String& path, HalFile& file) {
  return openFileForRead(m, path.c_str(), file);
}
bool HalStorage::openFileForWrite(const char*, const char* path, HalFile& file) {
  file = open(path, O_RDWR | O_CREAT | O_TRUNC);
  return file.isOpen();
}
bool HalStorage::openFileForWrite(const char* m, const std::string& path, HalFile& file) {
  return openFileForWrite(m, path.c_str(), file);
}
bool HalStorage::openFileForWrite(const char* m, const String& path, HalFile& file) {
  return openFileForWrite(m, path.c_str(), file);
}
bool HalStorage::removeDir(const char* path) {
  const std::string cmdPath = hostPath(path);
  DIR* d = opendir(cmdPath.c_str());
  if (!d) return false;
  while (dirent* e = readdir(d)) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    std::string child = std::string(path) + "/" + e->d_name;
    if (e->d_type == DT_DIR) {
      removeDir(child.c_str());
    } else {
      ::unlink(hostPath(child.c_str()).c_str());
    }
  }
  closedir(d);
  sim::hostStorageChanged();
  return ::rmdir(cmdPath.c_str()) == 0;
}

// --- HalFile -----------------------------------------------------------------
HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> i) : impl(std::move(i)) {}
HalFile::~HalFile() { close(); }
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&& other) {
  close();
  impl = std::move(other.impl);
  return *this;
}

void HalFile::flush() {
  if (impl && impl->fp) fflush(impl->fp);
}
size_t HalFile::getName(char* name, const size_t len) {
  if (!impl || !name || len == 0) return 0;
  const auto slash = impl->path.rfind('/');
  const std::string base = slash == std::string::npos ? impl->path : impl->path.substr(slash + 1);
  snprintf(name, len, "%s", base.c_str());
  return strlen(name);
}
size_t HalFile::size() { return static_cast<size_t>(fileSize64()); }
size_t HalFile::fileSize() { return static_cast<size_t>(fileSize64()); }
uint64_t HalFile::fileSize64() {
  if (!impl || !impl->fp) return 0;
  const long cur = ftell(impl->fp);
  fseek(impl->fp, 0, SEEK_END);
  const long end = ftell(impl->fp);
  fseek(impl->fp, cur, SEEK_SET);
  return end < 0 ? 0 : static_cast<uint64_t>(end);
}
uint32_t HalFile::modificationTime() { return 0; }
bool HalFile::seek(const size_t pos) { return impl && impl->fp && fseek(impl->fp, static_cast<long>(pos), SEEK_SET) == 0; }
bool HalFile::seek64(const uint64_t pos) { return seek(static_cast<size_t>(pos)); }
bool HalFile::seekCur(const int64_t offset) {
  return impl && impl->fp && fseek(impl->fp, static_cast<long>(offset), SEEK_CUR) == 0;
}
bool HalFile::seekSet(const size_t offset) { return seek(offset); }
bool HalFile::truncate(const uint64_t length) {
  if (!impl || !impl->fp) return false;
  fflush(impl->fp);
  return ftruncate(fileno(impl->fp), static_cast<off_t>(length)) == 0;
}
int HalFile::available() const {
  if (!impl || !impl->fp) return 0;
  const long cur = ftell(impl->fp);
  fseek(impl->fp, 0, SEEK_END);
  const long end = ftell(impl->fp);
  fseek(impl->fp, cur, SEEK_SET);
  return static_cast<int>(end - cur);
}
size_t HalFile::position() const { return impl && impl->fp ? static_cast<size_t>(ftell(impl->fp)) : 0; }
int HalFile::read(void* buf, const size_t count) {
  if (!impl || !impl->fp) return -1;
  return static_cast<int>(fread(buf, 1, count, impl->fp));
}
int HalFile::read() {
  uint8_t c;
  return read(&c, 1) == 1 ? c : -1;
}
size_t HalFile::write(const uint8_t* buf, const size_t count) {
  if (!impl || !impl->fp) return 0;
  return fwrite(buf, 1, count, impl->fp);
}
size_t HalFile::write(const void* buf, const size_t count) { return write(static_cast<const uint8_t*>(buf), count); }
size_t HalFile::write(const uint8_t b) { return write(&b, 1); }
bool HalFile::rename(const char* newPath) {
  if (!impl) return false;
  const bool ok = ::rename(hostPath(impl->path.c_str()).c_str(), hostPath(newPath).c_str()) == 0;
  if (ok) impl->path = newPath;
  return ok;
}
bool HalFile::isDirectory() const { return impl && impl->dir; }
void HalFile::rewindDirectory() {
  if (impl && impl->dir) rewinddir(impl->dir);
}
bool HalFile::close() {
  if (!impl) return false;
  const bool wrote = impl->writable && impl->fp;
  impl->closeAll();
  impl.reset();
  if (wrote) sim::hostStorageChanged();
  return true;
}
HalFile HalFile::openNextFile() {
  if (!impl || !impl->dir) return HalFile();
  while (dirent* e = readdir(impl->dir)) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    std::string child = impl->path;
    if (child.empty() || child.back() != '/') child += "/";
    child += e->d_name;
    return HalStorage::getInstance().open(child.c_str(), O_RDONLY);
  }
  return HalFile();
}
bool HalFile::isOpen() const { return impl && (impl->fp || impl->dir); }
HalFile::operator bool() const { return isOpen(); }
