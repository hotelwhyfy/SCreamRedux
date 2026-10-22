#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// SCream .res archive. Layout documented in NOTES.md and tools/scream_res.py.
class ResArchive {
public:
    enum class Type : uint32_t { Jpeg = 1, Tga = 2, Ogg = 3 };

    struct Entry {
        std::string name;
        Type type;
        uint32_t offset;
        uint32_t size;
    };

    static std::unique_ptr<ResArchive> open(const std::filesystem::path& path);

    const Entry* find(const std::string& name) const;
    std::vector<uint8_t> read(const Entry& e);
    const std::filesystem::path& path() const { return path_; }
    size_t size() const { return entries_.size(); }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;
};

// Ordered stack of archives and loose-file directories. Later mounts override
// earlier ones, which is how update.res replaces files in scratches.res.
class Resources {
public:
    void mount(std::unique_ptr<ResArchive> archive);
    void mountDirectory(const std::filesystem::path& dir);

    bool exists(const std::string& name) const;
    std::optional<std::vector<uint8_t>> read(const std::string& name);

    // Every archive entry visible through the mounts (later mounts win).
    std::vector<ResArchive::Entry> list() const;

private:
    struct Mount {
        std::unique_ptr<ResArchive> archive;
        std::filesystem::path dir;
    };
    std::vector<Mount> mounts_;
};
