#include "video.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstring>

#include <ogg/ogg.h>
#include <theora/theoradec.h>

struct Video::State {
    ogg_sync_state sync{};
    ogg_stream_state stream{};
    bool streamInit = false;
    th_info info{};
    th_comment comment{};
    th_setup_info* setup = nullptr;
    th_dec_ctx* dec = nullptr;
    th_ycbcr_buffer ycbcr{};

    bool nextPacket(ogg_packet& op);

    State() {
        ogg_sync_init(&sync);
        th_info_init(&info);
        th_comment_init(&comment);
    }
    ~State() {
        if (dec) th_decode_free(dec);
        if (setup) th_setup_free(setup);
        th_comment_clear(&comment);
        th_info_clear(&info);
        if (streamInit) ogg_stream_clear(&stream);
        ogg_sync_clear(&sync);
    }
};

Video::Video() = default;
Video::~Video() = default;

// Pull the next packet of the Theora stream, demuxing pages as needed.
bool Video::State::nextPacket(ogg_packet& op) {
    for (;;) {
        if (streamInit && ogg_stream_packetout(&stream, &op) == 1)
            return true;
        ogg_page page;
        if (ogg_sync_pageout(&sync, &page) != 1)
            return false;  // the whole file was handed to the sync layer up front
        if (!streamInit) {
            if (!ogg_page_bos(&page))
                continue;
            ogg_stream_init(&stream, ogg_page_serialno(&page));
            streamInit = true;
        }
        if (ogg_page_serialno(&page) == stream.serialno)
            ogg_stream_pagein(&stream, &page);
    }
}

bool Video::open(std::vector<uint8_t> ogg) {
    s_ = std::make_unique<State>();
    data_ = std::move(ogg);
    char* buf = ogg_sync_buffer(&s_->sync, long(data_.size()));
    std::memcpy(buf, data_.data(), data_.size());
    ogg_sync_wrote(&s_->sync, long(data_.size()));

    ogg_packet op;
    int headers = 0;
    while (headers < 3) {
        if (!s_->nextPacket(op) || th_decode_headerin(&s_->info, &s_->comment, &s_->setup, &op) <= 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "not a Theora stream");
            return false;
        }
        ++headers;
    }
    s_->dec = th_decode_alloc(&s_->info, s_->setup);
    if (!s_->dec)
        return false;
    width_ = int(s_->info.pic_width);
    height_ = int(s_->info.pic_height);
    if (s_->info.fps_denominator)
        fps_ = double(s_->info.fps_numerator) / s_->info.fps_denominator;
    rgba_.assign(size_t(width_) * height_ * 4, 255);
    return true;
}

bool Video::advance(double seconds) {
    if (finished_ || !s_ || !s_->dec)
        return false;
    long due = long(seconds * fps_);
    bool decoded = false;
    while (frame_ < due) {
        ogg_packet op;
        if (!s_->nextPacket(op)) {
            finished_ = true;
            break;
        }
        // Duplicate frames come through as empty packets and keep the last picture.
        if (th_decode_packetin(s_->dec, &op, nullptr) == 0)
            decoded = true;
        ++frame_;
    }
    if (decoded) {
        th_decode_ycbcr_out(s_->dec, s_->ycbcr);
        convert();
    }
    return decoded;
}

// BT.601 limited-range Y'CbCr 4:2:0 to RGBA.
void Video::convert() {
    const th_img_plane* p = s_->ycbcr;
    const int px = int(s_->info.pic_x), py = int(s_->info.pic_y);
    for (int y = 0; y < height_; ++y) {
        const uint8_t* Y = p[0].data + (py + y) * p[0].stride + px;
        const uint8_t* Cb = p[1].data + ((py + y) >> 1) * p[1].stride + (px >> 1);
        const uint8_t* Cr = p[2].data + ((py + y) >> 1) * p[2].stride + (px >> 1);
        uint8_t* out = rgba_.data() + size_t(y) * width_ * 4;
        for (int x = 0; x < width_; ++x) {
            int c = 298 * (Y[x] - 16), d = Cb[x >> 1] - 128, e = Cr[x >> 1] - 128;
            out[x * 4 + 0] = uint8_t(std::clamp((c + 409 * e + 128) >> 8, 0, 255));
            out[x * 4 + 1] = uint8_t(std::clamp((c - 100 * d - 208 * e + 128) >> 8, 0, 255));
            out[x * 4 + 2] = uint8_t(std::clamp((c + 516 * d + 128) >> 8, 0, 255));
        }
    }
}
