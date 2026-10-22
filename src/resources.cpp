#include "resources.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {

constexpr char kMagic[] = "SCream resource file";
constexpr size_t kHeaderSize = 0x104;
constexpr size_t kEntrySize = 92;
constexpr size_t kNameSize = 80;

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

}  // namespace

std::unique_ptr<ResArchive> ResArchive::open(const std::filesystem::path& path) {
    auto a = std::unique_ptr<ResArchive>(new ResArchive);
    a->path_ = path;
    a->file_.open(path, std::ios::binary);
    if (!a->file_)
        throw std::runtime_error("cannot open " + path.string());

    uint8_t head[kHeaderSize];
    if (!a->file_.read(reinterpret_cast<char*>(head), kHeaderSize) ||
        std::memcmp(head, kMagic, sizeof(kMagic)) != 0)
        throw std::runtime_error(path.string() + ": not a SCream resource file");
    uint32_t count = le32(head + 0x100);

    a->file_.seekg(0, std::ios::end);
    uint64_t fileSize = uint64_t(a->file_.tellg());
    a->file_.seekg(kHeaderSize);

    std::vector<uint8_t> raw(size_t(count) * kEntrySize);
    if (!a->file_.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size())))
        throw std::runtime_error(path.string() + ": truncated index");

    a->entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = raw.data() + size_t(i) * kEntrySize;
        Entry e;
        e.name.assign(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), kNameSize));
        e.type = Type(le32(p + kNameSize));
        e.offset = le32(p + kNameSize + 4);
        e.size = le32(p + kNameSize + 8);
        if (uint64_t(e.offset) + e.size > fileSize)
            throw std::runtime_error(path.string() + ": entry " + e.name + " runs past end of file");
        a->index_[e.name] = a->entries_.size();
        a->entries_.push_back(std::move(e));
    }
    return a;
}

const ResArchive::Entry* ResArchive::find(const std::string& name) const {
    auto it = index_.find(name);
    return it == index_.end() ? nullptr : &entries_[it->second];
}

std::vector<uint8_t> ResArchive::read(const Entry& e) {
    std::vector<uint8_t> data(e.size);
    file_.clear();
    file_.seekg(e.offset);
    file_.read(reinterpret_cast<char*>(data.data()), e.size);
    return data;
}

void Resources::mount(std::unique_ptr<ResArchive> archive) {
    mounts_.push_back({std::move(archive), {}});
}

void Resources::mountDirectory(const std::filesystem::path& dir) {
    mounts_.push_back({nullptr, dir});
}

std::vector<ResArchive::Entry> Resources::list() const {
    std::unordered_map<std::string, ResArchive::Entry> seen;
    for (const auto& m : mounts_)
        if (m.archive)
            for (const auto& e : m.archive->entries())
                seen[e.name] = e;
    std::vector<ResArchive::Entry> out;
    for (auto& [name, e] : seen)
        out.push_back(e);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}

bool Resources::exists(const std::string& name) const {
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        if (it->archive ? it->archive->find(name) != nullptr : std::filesystem::is_regular_file(it->dir / name))
            return true;
    }
    return false;
}

std::optional<std::vector<uint8_t>> Resources::read(const std::string& name) {
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        if (it->archive) {
            if (const auto* e = it->archive->find(name))
                return it->archive->read(*e);
        } else {
            std::ifstream f(it->dir / name, std::ios::binary);
            if (f)
                return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
        }
    }
    return std::nullopt;
}
