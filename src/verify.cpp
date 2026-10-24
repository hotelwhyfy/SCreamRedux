// --verify-media: decode every image, sound and video in the archives once
// and report anything that fails. Ogg files are also walked page by page,
// because the decoders skip over damaged or missing data without an error.

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <ogg/ogg.h>

#include "audio.hpp"
#include "engine.hpp"
#include "images.hpp"
#include "video.hpp"

namespace {

// libogg checks each page's CRC, so damaged data shows up as skipped bytes,
// lost pages as a gap in a stream's page numbers, and a cut-off file as a
// stream without its last page. Empty if the file is sound.
std::string checkOggPages(const std::vector<uint8_t>& data) {
    ogg_sync_state sync;
    ogg_sync_init(&sync);
    char* buf = ogg_sync_buffer(&sync, long(data.size()));
    std::memcpy(buf, data.data(), data.size());
    ogg_sync_wrote(&sync, long(data.size()));
    std::map<int, long> nextPage;  // stream serial -> the page number due next
    std::set<int> ended;
    std::string problem;
    long offset = 0;
    ogg_page page;
    while (problem.empty()) {
        long n = ogg_sync_pageseek(&sync, &page);
        if (n == 0)
            break;  // no complete page left
        if (n < 0) {
            problem = "damaged data at byte " + std::to_string(offset);
            break;
        }
        int serial = ogg_page_serialno(&page);
        long pageno = ogg_page_pageno(&page);
        if (auto it = nextPage.find(serial); it != nextPage.end() && pageno != it->second)
            problem = "pages missing before byte " + std::to_string(offset);
        nextPage[serial] = pageno + 1;
        if (ogg_page_eos(&page))
            ended.insert(serial);
        offset += n;
    }
    ogg_sync_clear(&sync);
    if (problem.empty() && offset != long(data.size()))
        problem = "cut off at byte " + std::to_string(offset);
    for (const auto& [serial, next] : nextPage)
        if (problem.empty() && !ended.count(serial))
            problem = "cut off (a stream has no last page)";
    return problem;
}

bool isVideo(const std::vector<uint8_t>& data) {
    static const uint8_t kTheora[] = {0x80, 't', 'h', 'e', 'o', 'r', 'a'};
    auto head = data.begin() + long(std::min<size_t>(data.size(), 512));
    return std::search(data.begin(), head, kTheora, kTheora + 7) != head;
}

}  // namespace

int Engine::verifyMedia() {
    for (const char* name : {"scream.res", "scratches.res", "update.res"}) {
        auto path = findData(name);
        if (!path.empty())
            res.mount(ResArchive::open(path));
    }
    const auto entries = res.list();
    const bool tty = isatty(fileno(stderr));  // progress only for a terminal
    int bad = 0, videos = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (tty)
            std::fprintf(stderr, "\rverifying %zu/%zu", i + 1, entries.size());
        auto data = res.read(e.name);
        std::string problem;
        if (!data) {
            problem = "can't be read";
        } else if (e.type != ResArchive::Type::Ogg) {
            if (!decodeImage(*data))
                problem = "image does not decode";
        } else if (problem = checkOggPages(*data); !problem.empty()) {
        } else if (isVideo(*data)) {
            ++videos;
            Video v;
            if (!v.open(*data))
                problem = "video does not decode";
            for (double t = 0; problem.empty() && !v.finished(); t += 1)
                v.advance(t);
        } else if (!Audio::decodeCheck(*data)) {
            problem = "sound does not decode";
        }
        if (!problem.empty()) {
            ++bad;
            if (tty)
                std::fprintf(stderr, "\r%40s\r", "");
            std::printf("FAILED %s: %s\n", e.name.c_str(), problem.c_str());
        }
    }
    if (tty)
        std::fprintf(stderr, "\r%40s\r", "");
    std::printf("%d decoded (%d videos), %d failed\n", int(entries.size()) - bad, videos, bad);
    return bad ? 1 : 0;
}
