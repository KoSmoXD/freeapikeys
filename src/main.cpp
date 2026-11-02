#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace geode::prelude;

// EXPERIMENTAL cube-only hazard-aware jump assistant.
// It queries actual damaging objects, compares approximate no-jump/jump paths,
// and dispatches one-frame jump inputs. The simulation is NOT a perfect copy
// of Geometry Dash physics; moving hazards, solids, portals and other modes
// remain unsupported.
class $modify(TrajectoryAutoPlayer, PlayLayer) {
    struct Fields {
        CCDrawNode* overlay = nullptr;
        bool held = false;
        int holdFrames = 0;
        float lastX = std::numeric_limits<float>::quiet_NaN();
        float speed = 0.f;  // world units / second, measured from real movement
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;
        m_fields->overlay = CCDrawNode::create();
        m_fields->overlay->setZOrder(9999);
        this->addChild(m_fields->overlay);
        return true;
    }

    void setAutoHold(bool down) {
        if (m_fields->held == down) return;
        m_fields->held = down;
        this->handleButton(down, 1, true);
    }

    void resetLevel() {
        if (m_fields->held) setAutoHold(false);
        m_fields->holdFrames = 0;
        m_fields->lastX = std::numeric_limits<float>::quiet_NaN();
        m_fields->speed = 0.f;
        PlayLayer::resetLevel();
    }

    void onQuit() {
        if (m_fields->held) setAutoHold(false);
        PlayLayer::onQuit();
    }

    static bool intersects(CCRect const& a, CCRect const& b) {
        return a.origin.x < b.origin.x + b.size.width &&
               a.origin.x + a.size.width > b.origin.x &&
               a.origin.y < b.origin.y + b.size.height &&
               a.origin.y + a.size.height > b.origin.y;
    }

    bool isCubeMode(PlayerObject* player) {
        return !m_isPlatformer && !player->m_isDead &&
            !player->m_isShip && !player->m_isBird &&
            !player->m_isBall && !player->m_isDart &&
            !player->m_isRobot && !player->m_isSpider &&
            !player->m_isSwing && !player->m_isGoingLeft;
    }

    // Predictor in world coordinates. Speed is measured from actual gameplay.
    // A simple ballistics approximation is used ONLY for evaluating jumps.
    std::vector<CCPoint> predict(PlayerObject* player, bool click, float speed, float horizon) {
        std::vector<CCPoint> out;
        CCPoint pos = player->getPosition();
        auto const startY = pos.y;
        auto const dt = 1.f / 60.f;
        float direction = player->m_isUpsideDown ? -1.f : 1.f;
        float vy = static_cast<float>(player->m_yVelocity);
        if (player->m_isOnGround) vy = click ? 11.8f * direction : 0.f;
        float gravity = -0.88f * direction;
        int count = std::clamp(static_cast<int>(horizon / dt), 1, 120);
        out.reserve(count);
        for (int i = 0; i < count; ++i) {
            pos.x += speed * dt;
            pos.y += vy;
            vy += gravity;
            // Ground is approximated as a plane through the current player.
            if (player->m_isOnGround && (pos.y - startY) * direction < 0.f) {
                pos.y = startY;
                vy = 0.f;
            }
            out.push_back(pos);
        }
        return out;
    }

    std::vector<CCRect> nearbyHazards(PlayerObject* player, float speed, float horizon) {
        std::vector<CCRect> result;
        auto p = player->getPosition();
        float reach = std::clamp(speed * horizon + 45.f, 60.f, 550.f);
        CCRect area(p.x - 20.f, p.y - 145.f, reach + 35.f, 290.f);
        // Uses the game's hazard classification instead of guessing object IDs.
        auto* objects = this->damagingObjectsInRect(area, true);
        if (!objects) return result;
        CCObject* raw = nullptr;
        CCARRAY_FOREACH(objects, raw) {
            auto* obj = static_cast<GameObject*>(raw);
            if (!obj || obj->m_isDisabled || obj->m_isDisabled2) continue;
            CCRect r = obj->getObjectRect();
            if (r.size.width <= 0.f || r.size.height <= 0.f) continue;
            if (r.origin.x + r.size.width < p.x - 30.f) continue;
            if (r.origin.x > p.x + reach) continue;
            result.push_back(r);
        }
        return result;
    }

    bool collides(CCPoint point, std::vector<CCRect> const& hazards, float halfSize) {
        CCRect box(point.x - halfSize, point.y - halfSize, halfSize * 2.f, halfSize * 2.f);
        for (auto const& rect : hazards)
            if (intersects(box, rect)) return true;
        return false;
    }

    void drawPath(std::vector<CCPoint> const& path, ccColor4F color) {
        if (!m_fields->overlay) return;
        for (size_t i = 1; i < path.size(); ++i)
            m_fields->overlay->drawSegment(path[i - 1], path[i], .8f, color);
    }

    void update(float dt) {
        PlayLayer::update(dt);

        auto* player = m_player1;
        if (!player) return;
        auto const pos = player->getPosition();

        // Keep a press held during at least one complete game update.
        bool wasHolding = m_fields->holdFrames > 0;
        if (wasHolding) {
            --m_fields->holdFrames;
            if (m_fields->holdFrames == 0) setAutoHold(false);
        }

        if (std::isfinite(m_fields->lastX) && dt > .0001f && dt < .1f) {
            float measured = (pos.x - m_fields->lastX) / dt;
            if (measured > 30.f && measured < 1200.f)
                m_fields->speed = measured;
        }
        m_fields->lastX = pos.x;

        if (m_fields->overlay) m_fields->overlay->clear();
        if (!Mod::get()->getSettingValue<bool>("enabled") || !isCubeMode(player)) {
            if (m_fields->held) setAutoHold(false);
            m_fields->holdFrames = 0;
            return;
        }

        float speed = m_fields->speed;
        if (speed < 30.f) return;  // Don't guess speed until movement is measured.

        auto horizon = static_cast<float>(Mod::get()->getSettingValue<double>("lookahead"));
        horizon = std::clamp(horizon, .15f, 1.5f);
        auto hazards = nearbyHazards(player, speed, horizon);
        auto neutral = predict(player, false, speed, horizon);
        auto jumped  = predict(player, true,  speed, horizon);

        if (Mod::get()->getSettingValue<bool>("debug-draw")) {
            drawPath(neutral, {1.f, .32f, .32f, .8f});
            drawPath(jumped, {.20f, 1.f, .42f, .8f});
        }

        if (wasHolding || !player->m_isOnGround || hazards.empty()) return;

        // Look for an imminent impact when NOT jumping.
        // At the moment of expected impact, check that jumping would
        // actually put the cube outside the estimated hazard bounds.
        float halfSize = player->m_vehicleSize < .9f ? 5.5f : 10.0f;
        int imminentFrames = std::clamp(
            static_cast<int>(std::ceil(.17f * 60.f)), 5, 16
        );
        int steps = std::min<int>(imminentFrames, neutral.size());
        for (int i = 0; i < steps; ++i) {
            if (!collides(neutral[i], hazards, halfSize)) continue;
            // This comparison is the actual trajectory-based click decision.
            if (i < static_cast<int>(jumped.size()) &&
                !collides(jumped[i], hazards, halfSize)) {
                setAutoHold(true);
                m_fields->holdFrames = 1;
            }
            break;
        }
    }
};
