#include "Recorder.hpp"

#include <Geode/Geode.hpp>

#include <ctime>
#include <fstream>

using namespace geode::prelude;

namespace fwc {

Recorder& Recorder::get() {
    static Recorder r;
    return r;
}

void Recorder::loadSetting() {
    m_enabled = Mod::get()->getSavedValue<bool>("recording-enabled", false);
}

void Recorder::setEnabled(bool v) {
    m_enabled = v;
    Mod::get()->setSavedValue("recording-enabled", v);
}

void Recorder::reset() {
    m_frame = 0;
    m_inputs.clear();
}

void Recorder::onInput(bool down, int button, bool isP1) {
    if (!m_enabled || button != 1) return;
    Ev e;
    e.frame = m_frame;
    e.down = down;
    e.p2 = !isP1;
    e.button = 1;
    m_inputs.push_back(e);
}

void Recorder::onStepEnd() {
    m_frame++;
}

bool Recorder::save(PlayLayer* pl, std::string& outPath, std::string& err) {
    if (!pl) { err = "Not in a level."; return false; }
    if (m_inputs.empty()) { err = "No inputs recorded in this attempt (is Rec ON? Start a new attempt after turning it on)."; return false; }

    Replay rp;
    rp.author = std::string(GJAccountManager::get()->m_username);
    rp.duration = m_frame;
    rp.gameVersion = GEODE_COMP_GD_VERSION;
    rp.framerate = static_cast<uint64_t>(Mod::get()->getSavedValue<int64_t>("record-tps", 240));
    rp.botName = "FrameWindowCounter";
    rp.botVersion = 1;
    rp.levelID = static_cast<uint64_t>(pl->m_level->m_levelID.value());
    rp.levelName = std::string(pl->m_level->m_levelName);
    rp.inputs = m_inputs;
    std::stable_sort(rp.inputs.begin(), rp.inputs.end(), [](Ev const& a, Ev const& b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        return a.down < b.down;
    });

    try {
        auto dir = Mod::get()->getSaveDir() / "macros";
        std::filesystem::create_directories(dir);

        std::string name;
        for (char c : rp.levelName) name.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        if (name.empty()) name = "level";
        char buf[32];
        std::time_t t = std::time(nullptr);
        std::strftime(buf, sizeof buf, "%Y%m%d_%H%M%S", std::localtime(&t));

        auto path = dir / (name + "_" + buf + ".gdr2");
        auto bytes = writeGDR2(rp);
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) { err = "Could not write the file."; return false; }
        outPath = path.string();
        return true;
    } catch (std::exception const& e) {
        err = e.what();
        return false;
    }
}

} // namespace fwc
