#pragma once

#include <Geode/Geode.hpp>
#include <filesystem>
#include <vector>

using namespace geode::prelude;

class FWCPopup : public geode::Popup {
public:
    static FWCPopup* create();
    ~FWCPopup() override;

protected:
    bool init();

    void rebuildList();
    void refreshInfo();
    void refreshStats(bool full = true);
    void scanMacros();
    void onTick(float dt);

    void onSelect(CCObject* sender);
    void onPage(CCObject* sender);
    void onFolder(CCObject*);
    void onAnalyze(CCObject*);
    void onClicks(CCObject*);
    void onExport(CCObject*);
    void onRec(CCObject*);
    void onSave(CCObject*);

    CCNode* m_content = nullptr;
    CCMenu* m_menu = nullptr;
    CCMenu* m_listMenu = nullptr;
    CCDrawNode* m_bars = nullptr;
    CCDrawNode* m_timeline = nullptr;
    CCLabelBMFont* m_info = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCLabelBMFont* m_names[6] = {};
    CCLabelBMFont* m_values[6] = {};
    ButtonSprite* m_analyzeSpr = nullptr;
    ButtonSprite* m_recSpr = nullptr;

    std::vector<std::filesystem::path> m_files;
    int m_page = 0;
    bool m_wasRunning = false;
    int m_ticks = 0;
};

// Paged list of every click with its measured window.
class FWCListPopup : public geode::Popup {
public:
    static FWCListPopup* create();

protected:
    bool init();
    void showPage();
    void onPrev(CCObject*);
    void onNext(CCObject*);

    CCNode* m_content = nullptr;
    CCNode* m_rows = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    int m_page = 0;
};

namespace fwc_ui {
    ccColor4F windowColor(int window);
}
