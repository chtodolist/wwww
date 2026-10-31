#include "Analyzer.hpp"

#include <Geode/Geode.hpp>

#include <chrono>
#include <fstream>
#include <map>

using namespace geode::prelude;

namespace fwc {

namespace {
    size_t lowerIdx(std::vector<Ev> const& v, uint64_t frame) {
        return static_cast<size_t>(std::lower_bound(v.begin(), v.end(), frame, [](Ev const& e, uint64_t f) {
            return e.frame < f;
        }) - v.begin());
    }

    std::string jsonEscape(std::string const& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
            else if (static_cast<unsigned char>(c) < 0x20) o.push_back(' ');
            else o.push_back(c);
        }
        return o;
    }
}

Analyzer& Analyzer::get() {
    static Analyzer a;
    return a;
}

// ---------------------------------------------------------------------------------------------
// data
// ---------------------------------------------------------------------------------------------

bool Analyzer::setReplay(Replay replay, std::filesystem::path source, std::string& err) {
    if (running()) cancel();

    m_replay = std::move(replay);
    m_source = std::move(source);
    m_hasReplay = true;
    m_evs = m_replay.inputs;
    m_clicks.clear();
    m_phase = Phase::Idle;
    m_message = "Macro loaded";

    if (m_replay.platformer) {
        err = "Platformer macros are not supported (classic mode only).";
        return false;
    }

    std::map<int, size_t> open;        // player -> index of click waiting for its release
    std::map<int, size_t> lastClick;   // player -> last click index
    std::map<int, uint64_t> lastRel;   // player -> last release frame

    for (size_t k = 0; k < m_evs.size(); ++k) {
        Ev const& e = m_evs[k];
        if (e.button != 1) continue;
        int key = e.p2 ? 1 : 0;
        if (e.down) {
            if (open.count(key)) continue; // already held
            Click c;
            c.press = e.frame;
            c.p2 = e.p2;
            c.button = e.button;
            c.pressIdx = k;
            if (lastRel.count(key)) c.prevRelease = lastRel[key];
            m_clicks.push_back(c);
            size_t idx = m_clicks.size() - 1;
            if (lastClick.count(key)) m_clicks[lastClick[key]].nextPress = e.frame;
            lastClick[key] = idx;
            open[key] = idx;
        } else {
            if (!open.count(key)) continue;
            Click& c = m_clicks[open[key]];
            c.release = e.frame;
            c.hasRelease = true;
            c.relIdx = k;
            lastRel[key] = e.frame;
            open.erase(key);
        }
    }

    if (m_clicks.empty()) {
        err = "This macro contains no clicks.";
        return false;
    }
    return true;
}

Summary Analyzer::summary() const {
    Summary s;
    s.total = static_cast<int>(m_clicks.size());
    for (auto const& c : m_clicks) {
        if (c.window < 0) continue;
        s.analysed++;
        if (c.window == 0) s.unverified++;
        else s.counts[std::min(c.window, 5) - 1]++;
    }
    return s;
}

float Analyzer::progress() const {
    if (m_clicks.empty()) return 0.f;
    if (m_phase == Phase::Finished) return 1.f;
    return static_cast<float>(m_i) / static_cast<float>(m_clicks.size());
}

bool Analyzer::exportResults(std::string& outPath, std::string& err) const {
    if (!m_hasReplay || m_clicks.empty()) { err = "Nothing to export."; return false; }
    try {
        auto dir = Mod::get()->getSaveDir() / "exports";
        std::filesystem::create_directories(dir);
        auto stem = m_source.stem().string();
        if (stem.empty()) stem = "macro";

        auto csv = dir / (stem + "_windows.csv");
        {
            std::ofstream f(csv);
            f << "index,player,press_frame,release_frame,time_s,window,earlier,later\n";
            for (size_t i = 0; i < m_clicks.size(); ++i) {
                auto const& c = m_clicks[i];
                f << (i + 1) << ',' << (c.p2 ? 2 : 1) << ',' << c.press << ','
                  << (c.hasRelease ? std::to_string(c.release) : std::string()) << ','
                  << (static_cast<double>(c.press) / static_cast<double>(m_replay.framerate)) << ','
                  << c.window << ',' << c.back << ',' << c.fwd << '\n';
            }
        }

        auto json = dir / (stem + "_summary.json");
        {
            auto s = summary();
            std::ofstream f(json);
            f << "{\n"
              << "  \"macro\": \"" << jsonEscape(stem) << "\",\n"
              << "  \"level\": \"" << jsonEscape(m_replay.levelName) << "\",\n"
              << "  \"framerate\": " << m_replay.framerate << ",\n"
              << "  \"total_clicks\": " << s.total << ",\n"
              << "  \"analysed_clicks\": " << s.analysed << ",\n"
              << "  \"windows\": {\n"
              << "    \"1\": " << s.counts[0] << ",\n"
              << "    \"2\": " << s.counts[1] << ",\n"
              << "    \"3\": " << s.counts[2] << ",\n"
              << "    \"4\": " << s.counts[3] << ",\n"
              << "    \"5_plus\": " << s.counts[4] << ",\n"
              << "    \"unverified\": " << s.unverified << "\n"
              << "  },\n"
              << "  \"max_shift\": " << m_maxShift << ",\n"
              << "  \"survival_margin\": " << m_margin << ",\n"
              << "  \"shift_release\": " << (m_shiftRelease ? "true" : "false") << "\n"
              << "}\n";
        }
        outPath = dir.string();
        return true;
    } catch (std::exception const& e) {
        err = e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------------------------
// control
// ---------------------------------------------------------------------------------------------

bool Analyzer::start(std::string& err) {
    if (!m_hasReplay || m_clicks.empty()) { err = "Load a macro first."; return false; }
    auto pl = PlayLayer::get();
    if (!pl) { err = "Open this from the pause menu inside a level."; return false; }

    auto mod = Mod::get();
    m_maxShift = static_cast<int>(mod->getSettingValue<int64_t>("max-shift"));
    m_margin = static_cast<int>(mod->getSettingValue<int64_t>("horizon-margin"));
    m_shiftRelease = mod->getSettingValue<bool>("shift-release");
    m_dt = 1.f / static_cast<float>(std::max<uint64_t>(1, m_replay.framerate));

    for (auto& c : m_clicks) { c.window = -1; c.back = c.fwd = 0; }

    m_i = 0;
    m_counter = 0;
    m_steps = 0;
    m_died = m_levelDone = m_stuck = false;
    m_trialRunning = false;
    m_held[0] = m_held[1] = false;
    m_holdCur = 0;
    m_injEvs = &m_evs;
    m_injCur = 0;
    if (m_cp) { m_cp->release(); m_cp = nullptr; }

    pl->resetLevelFromStart();
    m_phase = Phase::Starting;
    m_wait = 15;
    m_message = "Preparing level...";
    return true;
}

void Analyzer::cancel() {
    if (!running()) return;
    m_phase = Phase::Idle;
    m_message = "Cancelled";
    cleanup(PlayLayer::get());
}

void Analyzer::cleanup(PlayLayer* pl) {
    if (m_cp) { m_cp->release(); m_cp = nullptr; }
    m_stepping = false;
    m_injEvs = nullptr;
    if (pl) pl->resetLevelFromStart();
}

void Analyzer::fail(PlayLayer* pl, std::string msg) {
    m_phase = Phase::Failed;
    m_message = std::move(msg);
    cleanup(pl);
}

void Analyzer::finish(PlayLayer* pl) {
    m_phase = Phase::Finished;
    m_message = "Analysis complete";
    cleanup(pl);
}

void Analyzer::tick(double budgetMs) {
    if (!running()) return;
    auto pl = PlayLayer::get();
    if (!pl) { fail(nullptr, "Left the level during analysis."); return; }

    if (m_phase == Phase::Starting) {
        if (--m_wait > 0) return;
        m_phase = Phase::Advance;
        m_message = "Analysing...";
        m_counter = 0;
        m_steps = 0;
        m_died = false;
        seek(&m_evs, 0);
    }

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    while (running()) {
        double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        if (ms > budgetMs) break;
        if (m_phase == Phase::Advance) advance(pl);
        else if (m_phase == Phase::Trial) trialStep(pl);
    }
}

// ---------------------------------------------------------------------------------------------
// hooks
// ---------------------------------------------------------------------------------------------

void Analyzer::seek(std::vector<Ev> const* evs, uint64_t frame) {
    m_injEvs = evs;
    m_injCur = evs ? lowerIdx(*evs, frame) : 0;
}

void Analyzer::beforeStep(GJBaseGameLayer* layer) {
    if (!m_injEvs) return;
    auto const& v = *m_injEvs;
    while (m_injCur < v.size() && v[m_injCur].frame <= m_counter) {
        Ev const& e = v[m_injCur++];
        if (e.frame == m_counter) layer->handleButton(e.down, e.button, !e.p2);
    }
}

void Analyzer::afterStep() {
    m_counter++;
    m_steps++;
}

// ---------------------------------------------------------------------------------------------
// simulation
// ---------------------------------------------------------------------------------------------

bool Analyzer::stepOne(PlayLayer* pl) {
    uint64_t before = m_steps;
    m_stepping = true;
    for (int t = 0; t < 4 && m_steps == before; ++t) pl->update(m_dt);
    m_stepping = false;

    if (m_steps == before) { m_stuck = true; return false; }
    if (m_died || (pl->m_player1 && pl->m_player1->m_isDead)) return false;
    if (pl->m_levelEndAnimationStarted) m_levelDone = true;
    return true;
}

void Analyzer::takeCheckpoint(PlayLayer* pl) {
    if (m_cp) { m_cp->release(); m_cp = nullptr; }
    m_cp = pl->createCheckpoint();
    if (m_cp) m_cp->retain();
    m_cpFrame = m_counter;

    // button state at the checkpoint (loading a checkpoint drops held buttons)
    while (m_holdCur < m_evs.size() && m_evs[m_holdCur].frame < m_cpFrame) {
        Ev const& e = m_evs[m_holdCur++];
        if (e.button == 1) m_held[e.p2 ? 1 : 0] = e.down;
    }
}

void Analyzer::restoreCheckpoint(PlayLayer* pl) {
    pl->loadFromCheckpoint(m_cp);
    m_counter = m_cpFrame;
    m_died = false;
    m_levelDone = false;
    for (int p = 0; p < 2; ++p) {
        if (m_held[p]) pl->handleButton(true, 1, p == 0);
    }
}

void Analyzer::advance(PlayLayer* pl) {
    if (m_i >= m_clicks.size()) { finish(pl); return; }

    Click& c = m_clicks[m_i];
    uint64_t s = c.press > static_cast<uint64_t>(m_maxShift) ? c.press - m_maxShift : 0;

    if (m_counter < s) {
        if (!stepOne(pl)) {
            if (m_stuck) fail(pl, "The game did not advance a physics step. Is a physics bypass needed for this macro's TPS?");
            else fail(pl, "The macro dies at frame " + std::to_string(m_counter) +
                          " before click " + std::to_string(m_i + 1) + ". Showing partial results.");
        }
        return;
    }

    takeCheckpoint(pl);
    if (!m_cp) { fail(pl, "Could not create a checkpoint."); return; }

    // survival horizon for this click
    uint64_t nextAny = UINT64_MAX;
    for (size_t k = c.pressIdx + 1; k < m_evs.size(); ++k) {
        if (m_evs[k].down && m_evs[k].button == 1) { nextAny = m_evs[k].frame; break; }
    }
    uint64_t margin = static_cast<uint64_t>(m_margin);
    m_baseEnd = nextAny == UINT64_MAX ? c.press + margin : nextAny + margin;
    m_baseEnd = std::min(m_baseEnd, c.press + 240);
    m_baseEnd = std::max(m_baseEnd, c.press + margin);

    m_d = 0;
    m_dir = 0;
    m_back = m_fwd = 0;
    m_trialRunning = false;
    m_phase = Phase::Trial;
}

bool Analyzer::buildTrial(Click const& c, int d, uint64_t& end) {
    int64_t np = static_cast<int64_t>(c.press) + d;
    if (np < 0 || static_cast<uint64_t>(np) < m_cpFrame) return false;
    if (static_cast<uint64_t>(np) < c.prevRelease) return false;
    if (c.nextPress != UINT64_MAX && static_cast<uint64_t>(np) >= c.nextPress) return false;

    int64_t nr = -1;
    if (c.hasRelease) {
        nr = static_cast<int64_t>(c.release) + (m_shiftRelease ? d : 0);
        if (nr <= np) return false;
        if (c.nextPress != UINT64_MAX && static_cast<uint64_t>(nr) > c.nextPress) return false;
    }

    end = std::max(m_baseEnd, static_cast<uint64_t>(np) + static_cast<uint64_t>(m_margin));

    m_trialEvs.clear();
    for (size_t k = lowerIdx(m_evs, m_cpFrame); k < m_evs.size() && m_evs[k].frame <= end; ++k) {
        if (k == c.pressIdx || k == c.relIdx) continue;
        m_trialEvs.push_back(m_evs[k]);
    }
    Ev press;
    press.frame = static_cast<uint64_t>(np);
    press.down = true;
    press.p2 = c.p2;
    press.button = c.button;
    m_trialEvs.push_back(press);
    if (c.hasRelease) {
        Ev rel = press;
        rel.frame = static_cast<uint64_t>(nr);
        rel.down = false;
        m_trialEvs.push_back(rel);
    }
    std::stable_sort(m_trialEvs.begin(), m_trialEvs.end(), [](Ev const& a, Ev const& b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        return a.down < b.down;
    });
    return true;
}

void Analyzer::trialStep(PlayLayer* pl) {
    Click& c = m_clicks[m_i];

    if (!m_trialRunning) {
        uint64_t end = 0;
        if (!buildTrial(c, m_d, end)) { onTrialResult(pl, false); return; }
        restoreCheckpoint(pl);
        m_trialEnd = end;
        seek(&m_trialEvs, m_cpFrame);
        m_trialRunning = true;
        return;
    }

    if (m_counter > m_trialEnd) { onTrialResult(pl, true); return; }

    if (!stepOne(pl)) {
        if (m_stuck) { fail(pl, "The game stopped advancing physics steps."); return; }
        onTrialResult(pl, false);
        return;
    }
    if (m_levelDone) onTrialResult(pl, true);
}

void Analyzer::onTrialResult(PlayLayer* pl, bool ok) {
    m_trialRunning = false;
    Click& c = m_clicks[m_i];

    if (m_dir == 0) {
        if (!ok) { c.window = 0; endClick(pl); return; }
        m_dir = -1;
        m_d = -1;
        return;
    }
    if (m_dir < 0) {
        if (ok) {
            m_back++;
            if (m_back >= m_maxShift) { m_dir = 1; m_d = 1; }
            else m_d--;
        } else {
            m_dir = 1;
            m_d = 1;
        }
        return;
    }
    // m_dir > 0
    if (ok) {
        m_fwd++;
        if (m_fwd >= m_maxShift) { endClick(pl); return; }
        m_d++;
    } else {
        endClick(pl);
    }
}

void Analyzer::endClick(PlayLayer* pl) {
    Click& c = m_clicks[m_i];
    if (c.window != 0) {
        c.back = m_back;
        c.fwd = m_fwd;
        c.window = 1 + m_back + m_fwd;
    }
    // go back to the unmodified timeline at this click's checkpoint
    restoreCheckpoint(pl);
    seek(&m_evs, m_cpFrame);
    m_i++;
    m_phase = Phase::Advance;
}

} // namespace fwc
