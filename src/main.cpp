#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

// EXPERIMENTAL: approximate ballistic preview, not an auto-player.
struct PredictedPoint { CCPoint pos; };

class $modify(TrajectoryAutoPlayer, PlayLayer) {
    struct Fields {
        bool held = false;
        CCDrawNode* overlay = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->overlay = CCDrawNode::create();
        m_fields->overlay->setZOrder(9999);
        this->addChild(m_fields->overlay);
        return true;
    }

    std::vector<PredictedPoint> predict(bool jump, float horizon) {
        std::vector<PredictedPoint> result;
        auto* p = m_player1;
        if (!p) return result;
        CCPoint pos = p->getPosition();
        float vx = std::max(0.f, static_cast<float>(p->m_playerSpeed));
        float vy = static_cast<float>(p->m_yVelocity);
        if (jump && p->m_isOnGround) vy = 11.0f;
        float gravity = p->m_isUpsideDown ? 0.75f : -0.75f;
        const float dt = 1.f / 60.f;
        for (int i = 0; i < static_cast<int>(horizon / dt); ++i) {
            pos.x += vx;
            pos.y += vy;
            vy += gravity;
            result.push_back({pos});
        }
        return result;
    }

    // Collision detection NOT implemented: must not treat paths as safe.
    void setAutoHold(bool down) {
        if (m_fields->held == down) return;
        m_fields->held = down;
        if (down) this->handleButton(true, 1, true);
        else this->handleButton(false, 1, true);
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (!m_player1 || !m_fields->overlay) return;
        m_fields->overlay->clear();
        if (!Mod::get()->getSettingValue<bool>("enabled")) {
            if (m_fields->held) setAutoHold(false);
            return;
        }
        float horizon = Mod::get()->getSettingValue<double>("lookahead");
        auto neutral = predict(false, horizon);
        auto pressed = predict(true, horizon);
        if (Mod::get()->getSettingValue<bool>("debug-draw")) {
            auto draw = [this](auto const& points, ccColor4F color) {
                for (size_t i = 1; i < points.size(); ++i)
                    m_fields->overlay->drawSegment(points[i-1].pos, points[i].pos, 0.7f, color);
            };
            draw(neutral, {1.f, .35f, .25f, .8f});
            draw(pressed, {.2f, 1.f, .5f, .8f});
        }
        if (m_fields->held) setAutoHold(false);
    }

    void onQuit() {
        if (m_fields->held) setAutoHold(false);
        PlayLayer::onQuit();
    }
};
