#pragma once

// Minimal reader for the .gdr2 replay format (maxnut/GDReplayFormat, gdr2 branch).
// Layout: "GDR" | varint version | cstring inputTag | metadata | deaths | inputs.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fwc {

struct Ev {
    uint64_t frame = 0;
    bool down = false;
    bool p2 = false;
    int button = 1; // 1 = jump, 2 = left, 3 = right
};

struct Replay {
    uint64_t formatVersion = 0;
    std::string inputTag, author, description, botName, levelName;
    uint64_t duration = 0, gameVersion = 0, framerate = 240, seed = 0, coins = 0;
    uint64_t botVersion = 0, levelID = 0;
    bool ldm = false, platformer = false;
    std::vector<uint64_t> deaths;
    std::vector<Ev> inputs; // sorted by frame, releases before presses on the same frame
};

namespace detail {
    struct Reader {
        uint8_t const* p;
        size_t n;
        size_t pos = 0;
        bool bad = false;

        uint8_t u8() {
            if (pos >= n) { bad = true; return 0; }
            return p[pos++];
        }
        uint64_t varint() {
            uint64_t r = 0;
            int shift = 0;
            while (true) {
                uint8_t b = u8();
                if (bad) return 0;
                if (shift < 64) r |= uint64_t(b & 0x7F) << shift;
                if (!(b & 0x80)) break;
                shift += 7;
                if (shift > 70) { bad = true; return 0; }
            }
            return r;
        }
        std::string cstr() {
            std::string s;
            while (true) {
                uint8_t b = u8();
                if (bad || b == 0) break;
                s.push_back(static_cast<char>(b));
            }
            return s;
        }
        void skip(uint64_t k) {
            if (k > n - pos) { bad = true; pos = n; }
            else pos += static_cast<size_t>(k);
        }
    };
}

inline bool parseGDR2(std::vector<uint8_t> const& data, Replay& out, std::string& err) {
    if (data.size() < 4 || data[0] != 'G' || data[1] != 'D' || data[2] != 'R') {
        err = "Not a .gdr2 file (missing \"GDR\" header). GDR1 / other formats are not supported.";
        return false;
    }
    detail::Reader r{data.data(), data.size()};
    r.pos = 3;

    Replay rp;
    rp.formatVersion = r.varint();
    rp.inputTag = r.cstr();
    rp.author = r.cstr();
    rp.description = r.cstr();
    rp.duration = r.varint();
    rp.gameVersion = r.varint();
    rp.framerate = r.varint();
    rp.seed = r.varint();
    rp.coins = r.varint();
    rp.ldm = r.u8() != 0;
    rp.platformer = r.u8() != 0;
    rp.botName = r.cstr();
    rp.botVersion = r.varint();
    rp.levelID = r.varint();
    rp.levelName = r.cstr();
    r.skip(r.varint()); // replay extension block

    uint64_t deathCount = r.varint();
    if (r.bad || deathCount > r.n) { err = "Corrupt header / death table."; return false; }
    uint64_t acc = 0;
    for (uint64_t i = 0; i < deathCount && !r.bad; ++i) {
        acc += r.varint();
        rp.deaths.push_back(acc);
    }

    uint64_t inputCount = r.varint();
    uint64_t p1Count = r.varint();
    if (r.bad || inputCount > r.n || p1Count > inputCount) {
        err = "Corrupt input table.";
        return false;
    }

    uint64_t frame = 0;
    for (uint64_t i = 0; i < inputCount && !r.bad; ++i) {
        if (i == p1Count) frame = 0; // P2 list restarts its delta chain
        uint8_t b = r.u8();
        bool cont = b & 1;
        bool down = (b >> 1) & 1;
        int button = 1;
        uint64_t delta;
        int shift;
        if (rp.platformer) {
            button = (b >> 2) & 3;
            delta = b >> 4;
            shift = 4;
        } else {
            delta = b >> 2;
            shift = 6;
        }
        if (cont) delta |= r.varint() << shift;
        if (!rp.inputTag.empty()) r.skip(r.varint()); // per-input extension

        frame += delta;
        Ev e;
        e.frame = frame;
        e.down = down;
        e.p2 = i >= p1Count;
        e.button = button;
        rp.inputs.push_back(e);
    }
    if (r.bad) { err = "File ended unexpectedly while reading inputs."; return false; }

    std::stable_sort(rp.inputs.begin(), rp.inputs.end(), [](Ev const& a, Ev const& b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        return a.down < b.down; // release first
    });

    out = std::move(rp);
    return true;
}

inline bool loadGDR2(std::filesystem::path const& path, Replay& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "Could not open file."; return false; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parseGDR2(data, out, err);
}

} // namespace fwc
