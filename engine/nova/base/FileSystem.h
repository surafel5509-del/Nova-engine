// Nova Engine — virtual filesystem and file IO.
//
// Scenes, assets and projects are addressed with forward-slash relative paths
// ("assets/hero.png") that resolve against a project root. This keeps content
// portable between the Android app, the desktop player and the web editor.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nova {

struct DirEntry {
    std::string name;
    bool isDirectory = false;
    uint64_t sizeBytes = 0;
    int64_t modifiedUnix = 0;
};

/// Platform file access rooted at a project directory.
class FileSystem {
public:
    FileSystem() = default;
    explicit FileSystem(std::string root) : root_(std::move(root)) {}

    /// Sets the project root. Relative paths resolve against it.
    void setRoot(std::string root);
    const std::string& root() const { return root_; }

    /// Normalizes separators and resolves "." / ".."; never escapes the root.
    static std::string normalize(const std::string& path);
    /// Converts a backslash path to forward slashes and strips "./".
    static std::string toEnginePath(const std::string& path);
    std::string resolve(const std::string& relative) const;

    static std::string extensionOf(const std::string& path);
    static std::string stemOf(const std::string& path);
    static std::string filenameOf(const std::string& path);
    static std::string parentOf(const std::string& path);
    static std::string join(const std::string& a, const std::string& b);

    bool exists(const std::string& path) const;
    bool isDirectory(const std::string& path) const;
    bool isFile(const std::string& path) const;

    bool readText(const std::string& path, std::string& out) const;
    bool writeText(const std::string& path, const std::string& text) const;
    bool readBinary(const std::string& path, std::vector<uint8_t>& out) const;
    bool writeBinary(const std::string& path, const uint8_t* data, size_t size) const;
    /// Copies a file, creating parent directories as needed.
    bool copyFile(const std::string& from, const std::string& to) const;
    bool remove(const std::string& path) const;
    /// Recursive delete. Refuses to operate on a path outside the root.
    bool removeTree(const std::string& path) const;
    bool createDirectories(const std::string& path) const;

    /// Non-recursive listing, sorted directories-first then by name.
    std::vector<DirEntry> list(const std::string& path) const;
    /// Recursive walk collecting files; [extFilter] e.g. "png" (no dot).
    std::vector<std::string> walk(const std::string& path,
                                  const std::string& extFilter = "") const;

    uint64_t fileSize(const std::string& path) const;
    int64_t modifiedTime(const std::string& path) const;

    /// Writes atomically: temp file + rename, so a crash cannot truncate data.
    bool writeTextAtomic(const std::string& path, const std::string& text) const;

private:
    /// mkdir -p for a path that has already been resolved against the root.
    /// Internal helpers must use this so they never re-resolve an absolute.
    bool createDirectoriesAbsolute(const std::string& abs) const;
    std::string root_;
};

/// Process-wide default filesystem, rooted at the current working directory.
FileSystem& defaultFs();
void setDefaultFsRoot(const std::string& root);

}  // namespace nova
