#include "FWCPopup.hpp"
#include "Analyzer.hpp"
#include "Recorder.hpp"

#include <algorithm>
#include <system_error>

namespace fwc_ui {
    ccColor4F windowColor(int w) {
        if (w < 0) return {0.25f, 0.25f, 0.25f, 1.f};   // not analysed
        if (w == 0) return {0.55f, 0.55f, 0.55f, 1.f};  // unverified
        if (w == 1) return {0.92f, 0.25f, 0.20f, 1.f};  // 1 frame
        if (w == 2) return {1.00f, 0.55f, 0.00f, 1.f};  // 2 frames
        if (w == 3) return {1.00f, 0.84f, 0.00f, 1.f};  // 3 frames
        if (w == 4) return {0.47f, 0.86f, 0.24f, 1.f};  // 4 frames
        return {0.24f, 0.78f, 0.90f, 1.f};              // 5+ frames
    }
}

namespace {
    ccColor3B toRGB(ccColor4F c) {
        return {static_cast<GLubyte>(c.r * 255.f), static_cast<GLubyte>(c.g * 255.f), static_cast<GLubyte>(c.b * 255.f)};
    }

    void fillRect(CCDrawNode* n, float x0, float y0, float x1, float y1, ccColor4F c) {
        CCPoint v[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        n->drawPolygon(v, 4, c, 0.f, c);
    }

    CCLabelBMFont* makeLabel(CCNode* parent, char const* text, char const* font, float scale, CCPoint pos,
                             CCPoint anchor = {0.5f, 0.5f}) {
        auto l = CCLabelBMFont::create(text, font);
        l->setScale(scale);
        l->setAnchorPoint(anchor);
        l->setPosition(pos);
        parent->addChild(l);
        return l;
    }

    CCNode* makeContent(geode::Popup* popup, CCSize size) {
        auto content = CCNode::create();
        content->setContentSize(size);
        content->setAnchorPoint({0.5f, 0.5f});
        content->ignoreAnchorPointForPosition(false);
        popup->m_mainLayer->addChildAtPosition(content, Anchor::Center);
        return content;
    }

    CCMenu* makeMenu(CCNode* parent, CCSize size, int z = 5) {
        auto m = CCMenu::create();
        m->setContentSize(size);
        m->setPosition({0.f, 0.f});
        parent->addChild(m, z);
        return m;
    }

    void alertMsg(std::string const& msg) {
        FLAlertLayer::create("Frame Window Counter", msg.c_str(), "OK")->show();
    }
}

// =============================================================================================
// Main popup
// =============================================================================================

FWCPopup* FWCPopup::create() {
    auto ret = new FWCPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

FWCPopup::~FWCPopup() {
    fwc::Analyzer::get().cancel();
}

bool FWCPopup::init() {
    if (!Popup::init(400.f, 290.f)) return false;
    this->setTitle("Frame Window Counter");

    CCSize size{400.f, 290.f};
    m_content = makeContent(this, size);
    m_menu = makeMenu(m_content, size, 5);
    m_listMenu = makeMenu(m_content, size, 5);

    // left column ------------------------------------------------------------------------------
    makeLabel(m_content, "Macros (.gdr2)", "goldFont.fnt", 0.5f, {102.f, 244.f});
    m_pageLabel = makeLabel(m_content, "", "chatFont.fnt", 0.55f, {102.f, 100.f});

    for (int dir : {-1, 1}) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        spr->setScale(0.55f);
        if (dir > 0) spr->setFlipX(true);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FWCPopup::onPage));
        btn->setTag(dir);
        btn->setPosition({dir < 0 ? 52.f : 152.f, 100.f});
        m_menu->addChild(btn);
    }

    // right column -----------------------------------------------------------------------------
    m_info = makeLabel(m_content, "", "chatFont.fnt", 0.55f, {205.f, 252.f}, {0.f, 1.f});

    char const* names[6] = {"1 frame", "2 frames", "3 frames", "4 frames", "5+ frames", "Unverified"};
    for (int r = 0; r < 6; ++r) {
        float y = 186.f - r * 18.f;
        m_names[r] = makeLabel(m_content, names[r], "bigFont.fnt", 0.4f, {208.f, y}, {0.f, 0.5f});
        m_values[r] = makeLabel(m_content, "-", "bigFont.fnt", 0.4f, {298.f, y}, {1.f, 0.5f});
        int w = r < 5 ? r + 1 : 0;
        m_names[r]->setColor(toRGB(fwc_ui::windowColor(w)));
    }
    m_bars = CCDrawNode::create();
    m_content->addChild(m_bars, 1);

    // buttons ----------------------------------------------------------------------------------
    auto addBtn = [&](char const* text, int w, int slot, SEL_MenuHandler cb) {
        auto spr = ButtonSprite::create(text, w, true, "bigFont.fnt", "GJ_button_01.png", 26.f, 0.45f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        btn->setPosition({38.f + slot * 64.f, 72.f});
        m_menu->addChild(btn);
        return spr;
    };
    addBtn("Folder", 58, 0, menu_selector(FWCPopup::onFolder));
    m_analyzeSpr = addBtn("Analyze", 58, 1, menu_selector(FWCPopup::onAnalyze));
    addBtn("Clicks", 58, 2, menu_selector(FWCPopup::onClicks));
    addBtn("Export", 58, 3, menu_selector(FWCPopup::onExport));
    m_recSpr = addBtn(fwc::Recorder::get().enabled() ? "Rec ON" : "Rec OFF", 58, 4, menu_selector(FWCPopup::onRec));
    addBtn("Save", 58, 5, menu_selector(FWCPopup::onSave));

    // timeline ---------------------------------------------------------------------------------
    makeLabel(m_content, "Click timeline", "goldFont.fnt", 0.35f, {200.f, 54.f});
    m_timeline = CCDrawNode::create();
    m_timeline->setPosition({14.f, 26.f});
    m_content->addChild(m_timeline, 1);

    m_status = makeLabel(m_content, "", "chatFont.fnt", 0.5f, {200.f, 14.f});

    scanMacros();
    rebuildList();
    refreshInfo();
    refreshStats();

    this->schedule(schedule_selector(FWCPopup::onTick));
    return true;
}

void FWCPopup::scanMacros() {
    m_files.clear();
    std::vector<std::filesystem::path> dirs;
    std::error_code ec;
    auto own = Mod::get()->getSaveDir() / "macros";
    std::filesystem::create_directories(own, ec);
    dirs.push_back(own);
    auto extra = Mod::get()->getSettingValue<std::string>("extra-macro-folder");
    if (!extra.empty()) dirs.push_back(std::filesystem::path(extra));

    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
    for (auto const& dir : dirs) {
        ec.clear();
        if (!std::filesystem::exists(dir, ec)) continue;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            if (!it->is_regular_file(ec2)) continue;
            if (it->path().extension() != ".gdr2") continue;
            auto t = std::filesystem::last_write_time(it->path(), ec2);
            found.emplace_back(t, it->path());
        }
    }
    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    for (auto& f : found) m_files.push_back(f.second);
}

void FWCPopup::rebuildList() {
    m_listMenu->removeAllChildren();
    constexpr int perPage = 5;
    int count = static_cast<int>(m_files.size());
    int pages = std::max(1, (count + perPage - 1) / perPage);
    m_page = std::clamp(m_page, 0, pages - 1);

    if (count == 0) {
        m_pageLabel->setString("No .gdr2 found - press Folder");
        return;
    }
    m_pageLabel->setString(fmt::format("{} / {}", m_page + 1, pages).c_str());

    auto& a = fwc::Analyzer::get();
    for (int i = 0; i < perPage; ++i) {
        int idx = m_page * perPage + i;
        if (idx >= count) break;
        auto name = geode::utils::string::pathToString(m_files[idx].stem());
        if (name.size() > 24) name = name.substr(0, 22) + "..";
        bool selected = a.hasReplay() && a.source() == m_files[idx];
        auto spr = ButtonSprite::create(name.c_str(), 170, true, "bigFont.fnt",
                                        selected ? "GJ_button_02.png" : "GJ_button_04.png", 24.f, 0.5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FWCPopup::onSelect));
        btn->setTag(idx);
        btn->setPosition({102.f, 214.f - i * 24.f});
        m_listMenu->addChild(btn);
    }
}

void FWCPopup::refreshInfo() {
    auto& a = fwc::Analyzer::get();
    if (!a.hasReplay()) {
        m_info->setString("No macro loaded.\nPick a .gdr2 on the left.");
        return;
    }
    auto const& r = a.replay();
    auto name = geode::utils::string::pathToString(a.source().stem());
    if (name.size() > 28) name = name.substr(0, 26) + "..";
    std::string txt = fmt::format("{}\n240 TPS  |  {} clicks\nLevel: {}", name, a.clicks().size(),
                                  r.levelName.empty() ? "?" : r.levelName);
    m_info->setString(txt.c_str());
}

void FWCPopup::refreshStats(bool full) {
    auto& a = fwc::Analyzer::get();
    auto s = a.summary();
    int vals[6] = {s.counts[0], s.counts[1], s.counts[2], s.counts[3], s.counts[4], s.unverified};
    int mx = 1;
    for (int v : vals) mx = std::max(mx, v);

    m_bars->clear();
    for (int r = 0; r < 6; ++r) {
        m_values[r]->setString(s.analysed > 0 ? std::to_string(vals[r]).c_str() : "-");
        if (vals[r] > 0) {
            float y = 186.f - r * 18.f;
            float w = 80.f * static_cast<float>(vals[r]) / static_cast<float>(mx);
            fillRect(m_bars, 306.f, y - 5.f, 306.f + w, y + 5.f, fwc_ui::windowColor(r < 5 ? r + 1 : 0));
        }
    }

    if (full) {
        m_timeline->clear();
        fillRect(m_timeline, 0.f, 0.f, 372.f, 20.f, {0.f, 0.f, 0.f, 0.45f});
        if (a.hasReplay() && !a.clicks().empty()) {
            uint64_t total = std::max<uint64_t>(a.replay().duration, a.clicks().back().press) + 1;
            for (auto const& c : a.clicks()) {
                float x = 372.f * static_cast<float>(c.press) / static_cast<float>(total);
                fillRect(m_timeline, x, 1.f, std::min(372.f, x + 1.6f), 19.f, fwc_ui::windowColor(c.window));
            }
        }
    }

    if (a.running()) {
        m_status->setString(fmt::format("Analysing... {}%  (click {} of {})", static_cast<int>(a.progress() * 100.f),
                                        std::min<size_t>(a.clicks().size(), static_cast<size_t>(a.progress() * a.clicks().size()) + 1),
                                        a.clicks().size()).c_str());
    } else if (s.analysed > 0) {
        m_status->setString(fmt::format("{}  ({} of {} clicks measured)", a.message(), s.analysed, s.total).c_str());
    } else {
        m_status->setString(a.message().c_str());
    }
}

void FWCPopup::onTick(float) {
    auto& a = fwc::Analyzer::get();
    if (a.running()) {
        double budget = static_cast<double>(Mod::get()->getSettingValue<int64_t>("step-budget-ms"));
        a.tick(budget);
        m_wasRunning = true;
        refreshStats(++m_ticks % 30 == 0);
    } else if (m_wasRunning) {
        m_wasRunning = false;
        m_analyzeSpr->setString("Analyze");
        refreshStats(true);
    }
}

void FWCPopup::onSelect(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_files.size())) return;
    auto& a = fwc::Analyzer::get();
    if (a.running()) {
        Notification::create("Analysis is running - cancel it first", NotificationIcon::Warning)->show();
        return;
    }

    fwc::Replay replay;
    std::string err;
    if (!fwc::loadGDR2(m_files[idx], replay, err)) {
        alertMsg(err);
        return;
    }
    std::string err2;
    bool ok = a.setReplay(std::move(replay), m_files[idx], err2);
    rebuildList();
    refreshInfo();
    refreshStats(true);
    if (!ok) alertMsg(err2);
}

void FWCPopup::onPage(CCObject* sender) {
    m_page += static_cast<CCNode*>(sender)->getTag();
    rebuildList();
}

void FWCPopup::onFolder(CCObject*) {
    std::error_code ec;
    auto dir = Mod::get()->getSaveDir() / "macros";
    std::filesystem::create_directories(dir, ec);
    geode::utils::file::openFolder(dir);
    // pick up files the user dropped in while the folder was open next time they press a button
    scanMacros();
    rebuildList();
}

void FWCPopup::onAnalyze(CCObject*) {
    auto& a = fwc::Analyzer::get();
    if (a.running()) {
        a.cancel();
        m_analyzeSpr->setString("Analyze");
        refreshStats(true);
        return;
    }
    scanMacros();
    std::string err;
    if (!a.start(err)) {
        alertMsg(err);
        return;
    }
    m_wasRunning = true;
    m_analyzeSpr->setString("Cancel");
}

void FWCPopup::onClicks(CCObject*) {
    auto& a = fwc::Analyzer::get();
    if (!a.hasReplay() || a.clicks().empty()) {
        alertMsg("Load a macro first.");
        return;
    }
    FWCListPopup::create()->show();
}

void FWCPopup::onExport(CCObject*) {
    auto& a = fwc::Analyzer::get();
    if (a.summary().analysed == 0) {
        alertMsg("Run the analysis first.");
        return;
    }
    std::string path, err;
    if (a.exportResults(path, err)) {
        alertMsg("Exported CSV + JSON to:\n" + path);
    } else {
        alertMsg("Export failed: " + err);
    }
}

void FWCPopup::onRec(CCObject*) {
    auto& r = fwc::Recorder::get();
    r.setEnabled(!r.enabled());
    m_recSpr->setString(r.enabled() ? "Rec ON" : "Rec OFF");
    if (r.enabled()) alertMsg("Recording is ON. It starts from your next attempt (restart the level). Finished runs are saved automatically; use Save to store an unfinished attempt.");
}

void FWCPopup::onSave(CCObject*) {
    std::string path, err;
    if (fwc::Recorder::get().save(PlayLayer::get(), path, err)) {
        scanMacros();
        rebuildList();
        alertMsg("Saved macro:\n" + path);
    } else {
        alertMsg(err);
    }
}

// =============================================================================================
// Click list popup
// =============================================================================================

FWCListPopup* FWCListPopup::create() {
    auto ret = new FWCListPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool FWCListPopup::init() {
    if (!Popup::init(380.f, 270.f)) return false;
    this->setTitle("Click windows");

    CCSize size{380.f, 270.f};
    m_content = makeContent(this, size);
    auto menu = makeMenu(m_content, size, 5);

    float cols[6] = {16.f, 50.f, 110.f, 170.f, 214.f, 246.f};
    char const* heads[6] = {"#", "Press", "Release", "Hold", "Plr", "Window"};
    for (int i = 0; i < 6; ++i) {
        makeLabel(m_content, heads[i], "goldFont.fnt", 0.38f, {cols[i], 224.f}, {0.f, 0.5f});
    }

    m_rows = CCNode::create();
    m_content->addChild(m_rows, 2);
    m_pageLabel = makeLabel(m_content, "", "chatFont.fnt", 0.6f, {190.f, 20.f});

    for (int dir : {-1, 1}) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        spr->setScale(0.6f);
        if (dir > 0) spr->setFlipX(true);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, dir < 0 ? menu_selector(FWCListPopup::onPrev) : menu_selector(FWCListPopup::onNext));
        btn->setPosition({dir < 0 ? 120.f : 260.f, 20.f});
        menu->addChild(btn);
    }

    showPage();
    return true;
}

void FWCListPopup::showPage() {
    constexpr int perPage = 10;
    auto& a = fwc::Analyzer::get();
    auto const& clicks = a.clicks();
    int count = static_cast<int>(clicks.size());
    int pages = std::max(1, (count + perPage - 1) / perPage);
    m_page = std::clamp(m_page, 0, pages - 1);

    m_rows->removeAllChildren();
    float cols[6] = {16.f, 50.f, 110.f, 170.f, 214.f, 246.f};

    for (int r = 0; r < perPage; ++r) {
        int idx = m_page * perPage + r;
        if (idx >= count) break;
        auto const& c = clicks[idx];
        float y = 204.f - r * 17.f;

        std::string winTxt;
        if (c.window < 0) winTxt = "-";
        else if (c.window == 0) winTxt = "unverified";
        else winTxt = fmt::format("{}f (-{}/+{})", c.window, c.back, c.fwd);

        std::string texts[6] = {
            std::to_string(idx + 1),
            std::to_string(c.press),
            c.hasRelease ? std::to_string(c.release) : std::string("-"),
            c.hasRelease ? std::to_string(c.release - c.press) + "f" : std::string("-"),
            c.p2 ? "P2" : "P1",
            winTxt,
        };
        for (int i = 0; i < 6; ++i) {
            auto l = makeLabel(m_rows, texts[i].c_str(), "chatFont.fnt", 0.6f, {cols[i], y}, {0.f, 0.5f});
            if (i == 5) l->setColor(toRGB(fwc_ui::windowColor(c.window)));
        }
    }
    m_pageLabel->setString(fmt::format("{} / {}", m_page + 1, pages).c_str());
}

void FWCListPopup::onPrev(CCObject*) { m_page--; showPage(); }
void FWCListPopup::onNext(CCObject*) { m_page++; showPage(); }
