#pragma once

#include "GDR2.hpp"

#include <string>
#include <vector>

class PlayLayer;

namespace fwc {

class Recorder {
public:
    static Recorder& get();

    bool enabled() const { return m_enabled; }
    void setEnabled(bool v);
    void loadSetting();

    void reset();                                   // new attempt
    void onInput(bool down, int button, bool isP1); // called from handleButton
    void onStepEnd();                               // one full physics step finished
    size_t inputCount() const { return m_inputs.size(); }

    bool save(PlayLayer* pl, std::string& outPath, std::string& err);

private:
    bool m_enabled = false;
    uint64_t m_frame = 0;
    std::vector<Ev> m_inputs;
};

} // namespace fwc
