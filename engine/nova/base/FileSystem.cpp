#include "base/FileSystem.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "base/Log.h"

#if defined(_WIN32)
#  include <direct.h>
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace nova {
namespace {
constexpr const char* kChannel = "fs";
}

void FileSystem::setRoot(std::string root) {
    root_ = normalize(std::move(root));
}

std::string FileSystem::toEnginePath(const std::string& path) {
    std::string out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    while (!out.empty() && out[0] == '.') {
        if (out.size() >= 2 && out[1] == '/') out.erase(0, 2);
        else break;
    }
    return out;
}

std::string FileSystem::normalize(const std::string& path) {
    std::string s = toEnginePath(path);
    std::vector<std::string> parts;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty() || cur == ".") { cur.clear(); return; }
        if (cur == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(cur);
        }
        cur.clear();
    };
    for (char c : s) {
        if (c == '/') flush();
        else cur += c;
    }
    flush();

    // Preserve a leading slash for absolute paths.
    const bool absolute = !s.empty() && s[0] == '/';
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '/';
        out += parts[i];
    }
    if (absolute) out = "/" + out;
    if (out.empty()) out = ".";
    return out;
}

std::string FileSystem::resolve(const std::string& relative) const {
    const std::string rel = toEnginePath(relative);
    if (!rel.empty() && rel[0] == '/') return normalize(rel);
    if (root_.empty()) return normalize(rel);
    return normalize(root_ + "/" + rel);
}

std::string FileSystem::extensionOf(const std::string& path) {
    const std::string name = filenameOf(path);
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return "";
    std::string ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return ext;
}

std::string FileSystem::stemOf(const std::string& path) {
    const std::string name = filenameOf(path);
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string FileSystem::filenameOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string FileSystem::parentOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

std::string FileSystem::join(const std::string& a, const std::string& b) {
    if (a.empty()) return toEnginePath(b);
    if (b.empty()) return toEnginePath(a);
    if (b[0] == '/') return toEnginePath(b);
    return toEnginePath(a + "/" + b);
}

#if defined(_WIN32)
static bool statPath(const std::string& p, bool& isDir, uint64_t& size, int64_t& mtime) {
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &attr)) return false;
    isDir = (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    size = isDir ? 0 : (static_cast<uint64_t>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
    ULARGE_INTEGER t;
    t.LowPart = attr.ftLastWriteTime.dwLowDateTime;
    t.HighPart = attr.ftLastWriteTime.dwHighDateTime;
    mtime = static_cast<int64_t>(t.QuadPart / 10000000ULL - 11644473600LL);
    return true;
}
#else
static bool statPath(const std::string& p, bool& isDir, uint64_t& size, int64_t& mtime) {
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return false;
    isDir = S_ISDIR(st.st_mode);
    size = isDir ? 0u : static_cast<uint64_t>(st.st_size);
    mtime = static_cast<int64_t>(st.st_mtime);
    return true;
}
#endif

bool FileSystem::exists(const std::string& path) const {
    bool d; uint64_t s; int64_t t;
    return statPath(resolve(path), d, s, t);
}

bool FileSystem::isDirectory(const std::string& path) const {
    bool d; uint64_t s; int64_t t;
    return statPath(resolve(path), d, s, t) && d;
}

bool FileSystem::isFile(const std::string& path) const {
    bool d; uint64_t s; int64_t t;
    return statPath(resolve(path), d, s, t) && !d;
}

bool FileSystem::readText(const std::string& path, std::string& out) const {
    std::ifstream f(resolve(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool FileSystem::writeText(const std::string& path, const std::string& text) const {
    const std::string full = resolve(path);
    const std::string dir = parentOf(full);
    if (!dir.empty()) createDirectoriesAbsolute(dir);
    std::ofstream f(full, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return f.good();
}

bool FileSystem::writeTextAtomic(const std::string& path, const std::string& text) const {
    const std::string full = resolve(path);
    const std::string dir = parentOf(full);
    if (!dir.empty()) createDirectoriesAbsolute(dir);
    const std::string tmp = full + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f.good()) {
            f.close();
            ::remove(tmp.c_str());
            return false;
        }
    }
#if defined(_WIN32)
    if (!::MoveFileExA(tmp.c_str(), full.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        ::remove(tmp.c_str());
        return false;
    }
#else
    if (::rename(tmp.c_str(), full.c_str()) != 0) {
        ::remove(tmp.c_str());
        return false;
    }
#endif
    return true;
}

bool FileSystem::readBinary(const std::string& path, std::vector<uint8_t>& out) const {
    std::ifstream f(resolve(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(out.data()), size);
    return f.good() || f.eof();
}

bool FileSystem::writeBinary(const std::string& path, const uint8_t* data, size_t size) const {
    const std::string full = resolve(path);
    const std::string dir = parentOf(full);
    if (!dir.empty()) createDirectoriesAbsolute(dir);
    std::ofstream f(full, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (size) f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return f.good();
}

bool FileSystem::copyFile(const std::string& from, const std::string& to) const {
    std::vector<uint8_t> data;
    if (!readBinary(from, data)) return false;
    return writeBinary(to, data.data(), data.size());
}

bool FileSystem::createDirectoriesAbsolute(const std::string& abs) const {
    if (abs.empty()) return true;
    std::string acc;
    // Walk each component and mkdir, tolerating "already exists" (EEXIST).
    for (size_t i = 0; i <= abs.size(); ++i) {
        const bool atEnd = (i == abs.size());
        if ((atEnd || abs[i] == '/') && !acc.empty() && acc != "/") {
#if defined(_WIN32)
            ::_mkdir(acc.c_str());
#else
            ::mkdir(acc.c_str(), 0755);
#endif
        }
        if (!atEnd) acc += abs[i];
    }
    bool d; uint64_t s; int64_t t;
    return statPath(abs, d, s, t) && d;
}

bool FileSystem::createDirectories(const std::string& path) const {
    return createDirectoriesAbsolute(resolve(path));
}

bool FileSystem::remove(const std::string& path) const {
    return ::remove(resolve(path).c_str()) == 0;
}

bool FileSystem::removeTree(const std::string& path) const {
    const std::string full = resolve(path);
    if (isDirectory(path)) {
        for (const DirEntry& e : list(path)) {
            if (!removeTree(join(path, e.name))) return false;
        }
#if defined(_WIN32)
        return ::_rmdir(full.c_str()) == 0;
#else
        return ::rmdir(full.c_str()) == 0;
#endif
    }
    return ::remove(full.c_str()) == 0;
}

std::vector<DirEntry> FileSystem::list(const std::string& path) const {
    std::vector<DirEntry> out;
    const std::string full = resolve(path);
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    const std::string pattern = full + "\\*";
    HANDLE h = ::FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        DirEntry e;
        e.name = name;
        e.isDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.push_back(e);
    } while (::FindNextFileA(h, &fd));
    ::FindClose(h);
#else
    DIR* d = ::opendir(full.c_str());
    if (!d) return out;
    while (struct dirent* ent = ::readdir(d)) {
        const std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        DirEntry e;
        e.name = name;
        bool isDir; uint64_t size; int64_t mtime;
        if (statPath(full + "/" + name, isDir, size, mtime)) {
            e.isDirectory = isDir;
            e.sizeBytes = size;
            e.modifiedUnix = mtime;
        }
        out.push_back(e);
    }
    ::closedir(d);
#endif
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return a.name < b.name;
    });
    for (DirEntry& e : out) {
        if (!e.sizeBytes && !e.isDirectory) {
            bool d2; uint64_t s; int64_t t;
            if (statPath(full + "/" + e.name, d2, s, t)) e.sizeBytes = s;
        }
    }
    return out;
}

std::vector<std::string> FileSystem::walk(const std::string& path, const std::string& extFilter) const {
    std::vector<std::string> out;
    std::vector<std::string> stack{toEnginePath(path)};
    while (!stack.empty()) {
        const std::string cur = stack.back();
        stack.pop_back();
        for (const DirEntry& e : list(cur)) {
            const std::string child = join(cur, e.name);
            if (e.isDirectory) {
                stack.push_back(child);
            } else if (extFilter.empty() || extensionOf(child) == extFilter) {
                out.push_back(child);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

uint64_t FileSystem::fileSize(const std::string& path) const {
    bool d; uint64_t s; int64_t t;
    return statPath(resolve(path), d, s, t) ? s : 0;
}

int64_t FileSystem::modifiedTime(const std::string& path) const {
    bool d; uint64_t s; int64_t t;
    return statPath(resolve(path), d, s, t) ? t : 0;
}

FileSystem& defaultFs() {
    static FileSystem fs(".");
    return fs;
}

void setDefaultFsRoot(const std::string& root) { defaultFs().setRoot(root); }

}  // namespace nova
