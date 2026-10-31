#pragma once

#include "GDR2.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class GJBaseGameLayer;
class PlayLayer;
class CheckpointObject;

namespace fwc {

struct Click {
    uint64_t press = 0;
    uint64_t release = 0;
    bool hasRelease = false;
    bool p2 = false;
    int button = 1;
    size_t pressIdx = 0;
    size_t relIdx = SIZE_MAX;
    uint64_t prevRelease = 0;              // release frame of the previous click on the same player
    uint64_t nextPress = UINT64_MAX;       // press frame of the next click on the same player

    int back = 0;     // frames the click can be moved earlier
    int fwd = 0;      // frames the click can be moved later
    int window = -1;  // -1 = not analysed, 0 = baseline did not survive, >=1 = window size in frames
};

struct Summary {
    int counts[5] = {0, 0, 0, 0, 0}; // 1, 2, 3, 4, 5+
    int unverified = 0;
    int analysed = 0;
    int total = 0;
};

enum class Phase { Idle, Starting, Advance, Trial, Finished, Failed };

class Analyzer {
public:
    static Analyzer& get();

    // --- data ---
    bool setReplay(Replay replay, std::filesystem::path source, std::string& err);
    bool hasReplay() const { return m_hasReplay; }
    Replay const& replay() const { return m_replay; }
    std::vector<Click> const& clicks() const { return m_clicks; }
    std::filesystem::path const& source() const { return m_source; }
    Summary summary() const;
    bool exportResults(std::string& outPath, std::string& err) const;

    // --- control ---
    bool start(std::string& err);
    void cancel();
    void tick(double budgetMs);
    bool running() const {
        return m_phase == Phase::Starting || m_phase == Phase::Advance || m_phase == Phase::Trial;
    }
    Phase phase() const { return m_phase; }
    std::string const& message() const { return m_message; }
    float progress() const;

    // --- game hooks ---
    bool isStepping() const { return m_stepping; }
    void beforeStep(GJBaseGameLayer* layer);
    void afterStep();
    void onDeath() { m_died = true; }

private:
    bool stepOne(PlayLayer* pl);
    void advance(PlayLayer* pl);
    void trialStep(PlayLayer* pl);
    void onTrialResult(PlayLayer* pl, bool ok);
    void endClick(PlayLayer* pl);
    bool buildTrial(Click const& c, int d, uint64_t& end);
    void takeCheckpoint(PlayLayer* pl);
    void restoreCheckpoint(PlayLayer* pl);
    void fail(PlayLayer* pl, std::string msg);
    void finish(PlayLayer* pl);
    void cleanup(PlayLayer* pl);
    void seek(std::vector<Ev> const* evs, uint64_t frame);

    // replay + click table
    bool m_hasReplay = false;
    Replay m_replay;
    std::filesystem::path m_source;
    std::vector<Ev> m_evs;
    std::vector<Click> m_clicks;

    // settings snapshot
    int m_maxShift = 12;
    int m_margin = 20;
    bool m_shiftRelease = true;
    float m_dt = 1.f / 240.f;

    // run state
    Phase m_phase = Phase::Idle;
    std::string m_message = "Idle";
    int m_wait = 0;
    size_t m_i = 0;
    uint64_t m_counter = 0;   // index of the physics step about to run
    uint64_t m_steps = 0;     // number of steps actually executed (used to detect progress)
    bool m_stepping = false;
    bool m_died = false;
    bool m_levelDone = false;
    bool m_stuck = false;

    // injector
    std::vector<Ev> const* m_injEvs = nullptr;
    size_t m_injCur = 0;

    // checkpoint
    CheckpointObject* m_cp = nullptr;
    uint64_t m_cpFrame = 0;
    bool m_held[2] = {false, false};
    size_t m_holdCur = 0;

    // current click trial state
    std::vector<Ev> m_trialEvs;
    uint64_t m_trialEnd = 0;
    uint64_t m_baseEnd = 0;
    bool m_trialRunning = false;
    int m_d = 0, m_dir = 0, m_back = 0, m_fwd = 0;
};

} // namespace fwc
