#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Analyzer.hpp"
#include "FWCPopup.hpp"
#include "Recorder.hpp"

using namespace geode::prelude;

class FWCHandler : public CCObject {
public:
    static FWCHandler* get() {
        static FWCHandler* h = [] {
            auto x = new FWCHandler();
            x->retain();
            return x;
        }();
        return h;
    }
    void onOpen(CCObject*) {
        if (auto p = FWCPopup::create()) p->show();
    }
};

static void addFWCButton(CCNode* pauseLayer) {
    if (!pauseLayer || pauseLayer->getChildByID("fwc-menu"_spr)) return;

    auto win = CCDirector::get()->getWinSize();
    auto menu = CCMenu::create();
    menu->setID("fwc-menu"_spr);
    menu->setPosition({0.f, 0.f});

    auto spr = ButtonSprite::create("FWC");
    spr->setScale(0.6f);
    auto btn = CCMenuItemSpriteExtra::create(spr, FWCHandler::get(), menu_selector(FWCHandler::onOpen));
    btn->setID("fwc-button"_spr);
    btn->setPosition({win.width - 38.f, win.height - 24.f});
    menu->addChild(btn);

    pauseLayer->addChild(menu, 100);
}

$on_mod(Loaded) {
    fwc::Recorder::get().loadSetting();
}

class $modify(FWCPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        addFWCButton(this);
    }
};

class $modify(FWCBaseLayer, GJBaseGameLayer) {
    // real player inputs -> recorder
    void handleButton(bool down, int button, bool isPlayer1) {
        auto& a = fwc::Analyzer::get();
        if (!a.running() && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            fwc::Recorder::get().onInput(down, button, isPlayer1);
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& a = fwc::Analyzer::get();
        bool isMine = static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this;
        bool mine = a.isStepping() && isMine;
        bool full = !isHalfTick; // exactly one full tick per physics step (240/s)
        if (mine) {
            a.noteCall();
            if (full) a.beforeStep(this);
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (mine) {
            if (full) a.afterStep();
        } else if (full && isMine && !a.running()) {
            fwc::Recorder::get().onStepEnd();
        }
    }
};

class $modify(FWCPlayLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        auto& a = fwc::Analyzer::get();
        if (a.isStepping()) {
            a.onDeath();
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void pauseGame(bool b) {
        PlayLayer::pauseGame(b);
        if (auto scene = CCDirector::get()->getRunningScene()) {
            if (auto pause = scene->getChildByType<PauseLayer>(0)) {
                addFWCButton(pause);
            }
        }
    }

    // every new attempt starts a fresh recording
    void resetLevel() {
        PlayLayer::resetLevel();
        if (!fwc::Analyzer::get().running()) fwc::Recorder::get().reset();
    }

    // auto-save a completed run when recording is on
    void levelComplete() {
        auto& r = fwc::Recorder::get();
        if (r.enabled() && !fwc::Analyzer::get().running()) {
            std::string path, err;
            if (r.save(this, path, err)) {
                Notification::create("Macro saved", NotificationIcon::Success)->show();
            } else {
                Notification::create("Macro not saved: " + err, NotificationIcon::Error)->show();
            }
        }
        PlayLayer::levelComplete();
    }
};
