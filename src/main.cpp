#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Analyzer.hpp"
#include "FWCPopup.hpp"

using namespace geode::prelude;

// FWC button in the top right corner of the pause menu
class $modify(FWCPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("fwc-menu"_spr);
        menu->setPosition({0.f, 0.f});

        auto spr = ButtonSprite::create("FWC");
        spr->setScale(0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FWCPauseLayer::onFWC));
        btn->setID("fwc-button"_spr);
        btn->setPosition({win.width - 38.f, win.height - 24.f});
        menu->addChild(btn);

        this->addChild(menu, 10);
    }

    void onFWC(CCObject*) {
        if (auto p = FWCPopup::create()) p->show();
    }
};

// Feeds the macro's inputs into the simulation while an analysis is running.
class $modify(FWCBaseLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& a = fwc::Analyzer::get();
        bool mine = a.isStepping() && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this;
        if (mine) a.beforeStep(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        // only count a full physics step once, on its last tick
        if (mine && isLastTick) a.afterStep();
    }
};

// Deaths during analysis are just a "fail" result: no death effects, no level reset.
class $modify(FWCPlayLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        auto& a = fwc::Analyzer::get();
        if (a.isStepping()) {
            a.onDeath();
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }
};
