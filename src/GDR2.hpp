#pragma once

// Minimal reader for the .gdr2 replay format (maxnut/GDReplayFormat, gdr2 branch).
// Layout: "GDR" | varint version | cstring inputTag | metadata | deaths | inputs.

#include <algorithm>
#include <cstdio>
#include <cstring>
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
        // varint-length-prefixed string (EclipseBot / current libGDR layout)
        std::string str() {
            uint64_t len = varint();
            if (bad || len > n - pos) { bad = true; return {}; }
            std::string s(reinterpret_cast<char const*>(p + pos), static_cast<size_t>(len));
            pos += static_cast<size_t>(len);
            return s;
        }
        double fbe(int bytes) { // big-endian IEEE float (4) or double (8)
            if (bytes > static_cast<int>(n - pos)) { bad = true; return 0; }
            uint64_t v = 0;
            for (int i = 0; i < bytes; ++i) v = (v << 8) | p[pos++];
            if (bytes == 4) { uint32_t u = static_cast<uint32_t>(v); float f; std::memcpy(&f, &u, 4); return f; }
            double d; std::memcpy(&d, &v, 8); return d;
        }
        void skip(uint64_t k) {
            if (k > n - pos) { bad = true; pos = n; }
            else pos += static_cast<size_t>(k);
        }
    };
}

inline bool parseLayoutOld(std::vector<uint8_t> const& data, Replay& out, std::string& err) {
    if (data.size() < 4 || data[0] != 'G' || data[1] != 'D' || data[2] != 'R') {
        err = "Not a .gdr2 file (missing \"GDR\" header). GDR1 / other formats are not supported.";
        return false;
    }
    detail::Reader r{data.data(), data.size()};
    r.pos = 3;

    Replay rp;
    uint64_t deathCount = 0, inputCount = 0, p1Count = 0;
    size_t inputsStart = 0;
    auto diag = [&](char const* what) {
        auto trunc = [](std::string s) { if (s.size() > 20) s = s.substr(0, 20) + ".."; return s; };
        std::string d = std::string(what) + "\n\nfile size " + std::to_string(data.size()) + ", stopped at byte " +
            std::to_string(r.pos) + "\nversion=" + std::to_string(rp.formatVersion) + " tag='" + trunc(rp.inputTag) +
            "' author='" + trunc(rp.author) + "'\nduration=" + std::to_string(rp.duration) +
            " gv=" + std::to_string(rp.gameVersion) + " fps=" + std::to_string(rp.framerate) +
            " platformer=" + std::to_string(rp.platformer) + "\nbot='" + trunc(rp.botName) + "' v" +
            std::to_string(rp.botVersion) + " level='" + trunc(rp.levelName) + "'\ndeaths=" + std::to_string(deathCount) +
            " inputs=" + std::to_string(inputCount) + " p1=" + std::to_string(p1Count) + "\nfirst bytes:";
        char buf[8];
        for (size_t i = 0; i < data.size() && i < 48; ++i) {
            std::snprintf(buf, sizeof buf, " %02X", data[i]);
            d += buf;
        }
        return d;
    };

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

    deathCount = r.varint();
    if (r.bad || deathCount > r.n) { err = diag("Corrupt header / death table."); return false; }
    uint64_t acc = 0;
    for (uint64_t i = 0; i < deathCount && !r.bad; ++i) {
        acc += r.varint();
        rp.deaths.push_back(acc);
    }

    inputCount = r.varint();
    p1Count = r.varint();
    inputsStart = r.pos;
    if (r.bad || inputCount > r.n || p1Count > inputCount) {
        err = diag("Corrupt input table.");
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
    (void)inputsStart;
    if (r.bad) { err = diag("File ended unexpectedly while reading inputs."); return false; }

    std::stable_sort(rp.inputs.begin(), rp.inputs.end(), [](Ev const& a, Ev const& b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        return a.down < b.down; // release first
    });

    out = std::move(rp);
    return true;
}

// Layout written by EclipseBot and other current bots (verified against a real file):
// "GDR" | varint version | str tag | str author | str description | float32BE duration(s) |
// varint gameVersion | float64BE framerate | varint seed | varint coins | u8 ldm | u8 platformer |
// str botName | varint botVersion | varint levelID | str levelName | varint extSize + bytes |
// varint deathCount + varint deltas | varint inputCount | varint p1Count | inputs (plain varints)
// Each input = varint( frameDelta << 1 | down ), delta relative to the previous input of that player.
inline bool parseLayoutNew(std::vector<uint8_t> const& data, Replay& out, std::string& err) {
    if (data.size() < 4 || data[0] != 'G' || data[1] != 'D' || data[2] != 'R') {
        err = "Not a .gdr2 file (missing \"GDR\" header). GDR1 / other formats are not supported.";
        return false;
    }
    detail::Reader r{data.data(), data.size()};
    r.pos = 3;

    Replay rp;
    uint64_t deathCount = 0, inputCount = 0, p1Count = 0;
    auto diag = [&](char const* what) {
        auto trunc = [](std::string s) { if (s.size() > 20) s = s.substr(0, 20) + ".."; return s; };
        std::string d = std::string(what) + "\n\nfile size " + std::to_string(data.size()) + ", stopped at byte " +
            std::to_string(r.pos) + "\nversion=" + std::to_string(rp.formatVersion) + " tag='" + trunc(rp.inputTag) +
            "' author='" + trunc(rp.author) + "'\nduration=" + std::to_string(rp.duration) +
            " fps=" + std::to_string(rp.framerate) + " platformer=" + std::to_string(rp.platformer) +
            "\nbot='" + trunc(rp.botName) + "' level='" + trunc(rp.levelName) + "'\ndeaths=" +
            std::to_string(deathCount) + " inputs=" + std::to_string(inputCount) + " p1=" + std::to_string(p1Count) +
            "\nfirst bytes:";
        char buf[8];
        for (size_t i = 0; i < data.size() && i < 48; ++i) { std::snprintf(buf, sizeof buf, " %02X", data[i]); d += buf; }
        return d;
    };

    rp.formatVersion = r.varint();
    rp.inputTag = r.str();
    rp.author = r.str();
    rp.description = r.str();
    double durSec = r.fbe(4);
    rp.gameVersion = r.varint();
    double fps = r.fbe(8);
    if (r.bad || !(fps >= 1.0 && fps <= 100000.0)) { err = diag("Unexpected header layout (framerate)."); return false; }
    rp.framerate = static_cast<uint64_t>(fps + 0.5);
    rp.duration = durSec > 0 ? static_cast<uint64_t>(durSec * fps + 0.5) : 0;
    rp.seed = r.varint();
    rp.coins = r.varint();
    rp.ldm = r.u8() != 0;
    rp.platformer = r.u8() != 0;
    rp.botName = r.str();
    rp.botVersion = r.varint();
    rp.levelID = r.varint();
    rp.levelName = r.str();
    r.skip(r.varint());

    deathCount = r.varint();
    if (r.bad || deathCount > r.n) { err = diag("Corrupt header / death table."); return false; }
    uint64_t acc = 0;
    for (uint64_t i = 0; i < deathCount && !r.bad; ++i) { acc += r.varint(); rp.deaths.push_back(acc); }

    inputCount = r.varint();
    p1Count = r.varint();
    if (r.bad || inputCount > r.n || p1Count > inputCount) { err = diag("Corrupt input table."); return false; }

    uint64_t frame = 0;
    for (uint64_t i = 0; i < inputCount && !r.bad; ++i) {
        if (i == p1Count) frame = 0;
        uint64_t v = r.varint();
        Ev e;
        if (rp.platformer) {            // delta | button(2) | down  (best guess; platformer is unsupported anyway)
            e.down = v & 1;
            e.button = static_cast<int>((v >> 1) & 3);
            frame += v >> 3;
        } else {
            e.down = v & 1;
            e.button = 1;
            frame += v >> 1;
        }
        if (!rp.inputTag.empty()) r.skip(r.varint());
        e.frame = frame;
        e.p2 = i >= p1Count;
        rp.inputs.push_back(e);
    }
    if (r.bad) { err = diag("File ended unexpectedly while reading inputs."); return false; }
    if (r.pos != data.size()) { err = diag("Unexpected extra data after the inputs."); return false; }

    std::stable_sort(rp.inputs.begin(), rp.inputs.end(), [](Ev const& a, Ev const& b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        return a.down < b.down;
    });
    out = std::move(rp);
    return true;
}

// Tries the real-world layout first, then the layout described in the format's readme.
inline bool parseGDR2(std::vector<uint8_t> const& data, Replay& out, std::string& err) {
    std::string errNew, errOld;
    if (parseLayoutNew(data, out, errNew)) return true;
    if (parseLayoutOld(data, out, errOld)) return true;
    err = errNew;
    return false;
}

namespace detail {
    inline void putVarint(std::vector<uint8_t>& o, uint64_t v) {
        while (true) {
            uint8_t b = v & 0x7F;
            v >>= 7;
            if (v) o.push_back(b | 0x80);
            else { o.push_back(b); break; }
        }
    }
    inline void putStr(std::vector<uint8_t>& o, std::string const& s) {
        putVarint(o, s.size());
        o.insert(o.end(), s.begin(), s.end());
    }
    inline void putBE(std::vector<uint8_t>& o, uint64_t v, int bytes) {
        for (int i = bytes - 1; i >= 0; --i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
}

// Serialises a classic-mode replay in the same layout EclipseBot writes.
// rp.inputs must be sorted by frame.
inline std::vector<uint8_t> writeGDR2(Replay const& rp) {
    using namespace detail;
    std::vector<uint8_t> o{'G', 'D', 'R'};
    putVarint(o, 2);
    putStr(o, "");
    putStr(o, rp.author);
    putStr(o, rp.description);
    float dur = rp.framerate ? static_cast<float>(static_cast<double>(rp.duration) / static_cast<double>(rp.framerate)) : 0.f;
    uint32_t du; std::memcpy(&du, &dur, 4);
    putBE(o, du, 4);
    putVarint(o, rp.gameVersion);
    double fps = static_cast<double>(rp.framerate);
    uint64_t fu; std::memcpy(&fu, &fps, 8);
    putBE(o, fu, 8);
    putVarint(o, rp.seed);
    putVarint(o, rp.coins);
    o.push_back(rp.ldm ? 1 : 0);
    o.push_back(0);
    putStr(o, rp.botName);
    putVarint(o, rp.botVersion);
    putVarint(o, rp.levelID);
    putStr(o, rp.levelName);
    putVarint(o, 0);

    putVarint(o, rp.deaths.size());
    uint64_t prev = 0;
    for (auto d : rp.deaths) { putVarint(o, d - prev); prev = d; }

    std::vector<Ev> p1, p2;
    for (auto const& e : rp.inputs) (e.p2 ? p2 : p1).push_back(e);
    putVarint(o, p1.size() + p2.size());
    putVarint(o, p1.size());
    for (auto* list : {&p1, &p2}) {
        uint64_t last = 0;
        for (auto const& e : *list) {
            putVarint(o, ((e.frame - last) << 1) | (e.down ? 1 : 0));
            last = e.frame;
        }
    }
    return o;
}

inline bool loadGDR2(std::filesystem::path const& path, Replay& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "Could not open file."; return false; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parseGDR2(data, out, err);
}

} // namespace fwc
