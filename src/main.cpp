#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace geode::prelude;

// v0.3.1: Experimental forward cube auto-jumper with one enable switch.
// Draws both hypothetical trajectories, detects upcoming GameObjectType::Hazard
// and AnimatedHazard, and presses Jump automatically at a calculated approach
// distance. It is not an exact collision simulator or a level-solving bot.
class $modify(TrajectoryAutoPlayer, PlayLayer) {
    struct Fields {
        CCDrawNode* overlay = nullptr;
        CCLabelBMFont* status = nullptr;
        bool held = false;
        int pressFrames = 0;
        int jumps = 0;
        int scanTick = 0;
        int statusTick = 0;
        float previousX = std::numeric_limits<float>::quiet_NaN();
        float measuredSpeed = 0.f;
        float testElapsed = 0.f;
        bool testTapSent = false;
        std::vector<CCRect> nearby;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        m_fields->overlay = CCDrawNode::create();
        if (m_objectLayer) {
            // The predicted points are WORLD coordinates, not screen coordinates.
            // Put the draw node in the same transformed layer as the objects.
            m_objectLayer->addChild(m_fields->overlay, 9999);
        } else {
            this->addChild(m_fields->overlay, 9999);
        }

        m_fields->status = CCLabelBMFont::create("AUTO ON", "bigFont.fnt");
        if (m_fields->status) {
            m_fields->status->setScale(.32f);
            m_fields->status->setAnchorPoint({0.f, 1.f});
            auto size = CCDirector::sharedDirector()->getWinSize();
            m_fields->status->setPosition({8.f, size.height - 8.f});
            this->addChild(m_fields->status, 100000);
        }
        return true;
    }

    void holdJump(bool down) {
        if (m_fields->held == down) return;
        m_fields->held = down;
        // Button 1 is Jump for player one on Android.
        this->handleButton(down, 1, true);
    }

    void resetLevel() {
        if (m_fields->held) holdJump(false);
        m_fields->pressFrames = 0;
        m_fields->previousX = std::numeric_limits<float>::quiet_NaN();
        m_fields->measuredSpeed = 0.f;
        m_fields->nearby.clear();
        m_fields->scanTick = 0;
        m_fields->testElapsed = 0.f;
        m_fields->testTapSent = false;
        PlayLayer::resetLevel();
    }

    void onQuit() {
        if (m_fields->held) holdJump(false);
        PlayLayer::onQuit();
    }

    bool isSupported(PlayerObject* p) const {
        return !m_isPlatformer && !p->m_isDead &&
               !p->m_isShip && !p->m_isBird && !p->m_isBall &&
               !p->m_isDart && !p->m_isRobot && !p->m_isSpider &&
               !p->m_isSwing && !p->m_isGoingLeft;
    }

    static bool overlaps(CCRect const& a, CCRect const& b) {
        return a.origin.x < b.origin.x + b.size.width &&
               b.origin.x < a.origin.x + a.size.width &&
               a.origin.y < b.origin.y + b.size.height &&
               b.origin.y < a.origin.y + a.size.height;
    }

    std::vector<CCPoint> predict(PlayerObject* player, bool jump, float speed, float horizon) {
        std::vector<CCPoint> path;
        auto p = player->getPosition();
        auto ground = p.y;
        float direction = player->m_isUpsideDown ? -1.f : 1.f;
        // These are approximate per-60-Hz-tick velocities, NOT accurate game physics.
        float vy = static_cast<float>(player->m_yVelocity);
        if (player->m_isOnGround) vy = jump ? 11.18f * direction : 0.f;
        float gravity = -.88f * direction;
        auto steps = std::clamp(static_cast<int>(horizon * 60.f), 1, 90);
        path.reserve(steps);
        for (int i = 0; i < steps; ++i) {
            p.x += speed / 60.f;
            p.y += vy;
            vy += gravity;
            if (player->m_isOnGround && ((p.y - ground) * direction < 0.f)) {
                p.y = ground;
                vy = 0.f;
            }
            path.push_back(p);
        }
        return path;
    }

    std::vector<CCRect> gatherHazards(CCPoint center, float reach) {
        std::vector<CCRect> boxes;
        // Iterate actual loaded objects, since damagingObjectsInRect can be empty
        // when the object is not yet in the engine's collision candidate set.
        if (m_objects) {
            for (auto* obj : CCArrayExt<GameObject*>(m_objects)) {
                if (!obj || obj->m_isDisabled || obj->m_isDisabled2) continue;
                if (obj->m_objectType != GameObjectType::Hazard &&
                    obj->m_objectType != GameObjectType::AnimatedHazard)
                    continue;
                auto r = obj->getObjectRect();
                if (r.size.width <= .1f || r.size.height <= .1f) continue;
                if (r.origin.x + r.size.width < center.x - 35.f ||
                    r.origin.x > center.x + reach) continue;
                if (r.origin.y > center.y + 70.f ||
                    r.origin.y + r.size.height < center.y - 70.f) continue;
                boxes.push_back(r);
            }
        }

        // The collision pipeline also maintains a nearby-hazard list.
        // Include it for levels which don't populate m_objects predictably.
        for (auto* obj : m_hazardCollisionObjects) {
            if (!obj || obj->m_isDisabled || obj->m_isDisabled2) continue;
            auto r = obj->getObjectRect();
            if (r.size.width <= .1f || r.size.height <= .1f) continue;
            if (r.origin.x + r.size.width < center.x - 35.f ||
                r.origin.x > center.x + reach) continue;
            if (r.origin.y > center.y + 70.f ||
                r.origin.y + r.size.height < center.y - 70.f) continue;
            boxes.push_back(r);
        }

        // Fallback to the engine's collision-area query if its loaded objects
        // list did not contain nearby hazards (different level configurations).
        if (boxes.empty()) {
            auto* arr = damagingObjectsInRect(
                CCRect(center.x - 35.f, center.y - 105.f, reach + 70.f, 210.f), true
            );
            if (arr) {
                for (auto* obj : CCArrayExt<GameObject*>(arr)) {
                    if (!obj || obj->m_isDisabled || obj->m_isDisabled2) continue;
                    auto r = obj->getObjectRect();
                    if (r.size.width > .1f && r.size.height > .1f &&
                        r.origin.x + r.size.width >= center.x - 35.f &&
                        r.origin.x < center.x + reach)
                        boxes.push_back(r);
                }
            }
        }
        return boxes;
    }

    bool wouldHit(std::vector<CCPoint> const& path, float halfSize) {
        for (auto const& point : path) {
            CCRect box(point.x - halfSize, point.y - halfSize,
                       2.f * halfSize, 2.f * halfSize);
            for (auto const& danger : m_fields->nearby) {
                if (overlaps(box, danger)) return true;
            }
        }
        return false;
    }

    void drawPath(std::vector<CCPoint> const& path, ccColor4F color) {
        if (!m_fields->overlay) return;
        for (size_t i = 1; i < path.size(); ++i)
            m_fields->overlay->drawSegment(path[i - 1], path[i], 1.3f, color);
    }

    void updateStatus(std::string message, bool enabled) {
        if (!m_fields->status) return;
        // Avoid rebuilding an atlas label every video frame.
        if (++m_fields->statusTick % 6 != 0) return;
        m_fields->status->setString(message.c_str());
        m_fields->status->setColor(enabled ? ccc3(120, 255, 135)
                                          : ccc3(255, 170, 95));
    }

    void update(float dt) {
        PlayLayer::update(dt);

        auto* p = m_player1;
        if (!p) return;
        if (m_fields->overlay) m_fields->overlay->clear();

        if (m_fields->pressFrames > 0) {
            if (--m_fields->pressFrames == 0) holdJump(false);
        }

        // Geode's mod-level Enabled toggle controls whether this mod runs.
        // There is intentionally no second independent "enabled" setting.
        // The old duplicate toggle could be OFF even with the mod enabled.
        if (!isSupported(p)) {
            if (m_fields->held) holdJump(false);
            m_fields->pressFrames = 0;
            updateStatus("AUTO: CUBE ONLY", false);
            return;
        }

        auto current = p->getPosition();

        if (std::isfinite(m_fields->previousX) && dt > .00001f && dt < .15f) {
            float deltaSpeed = (current.x - m_fields->previousX) / dt;
            if (deltaSpeed > 50.f && deltaSpeed < 1600.f) {
                if (m_fields->measuredSpeed == 0.f)
                    m_fields->measuredSpeed = deltaSpeed;
                else
                    m_fields->measuredSpeed = .75f * m_fields->measuredSpeed + .25f * deltaSpeed;
            }
        }
        m_fields->previousX = current.x;
        // GD's playerSpeed is in approximate world units per simulation tick.
        float estimatedSpeed = std::clamp(
            static_cast<float>(std::abs(p->m_playerSpeed)) * 60.f,
            200.f, 750.f
        );
        float speed = m_fields->measuredSpeed > 50.f
                    ? m_fields->measuredSpeed : estimatedSpeed;

        float horizon = std::clamp(
            static_cast<float>(Mod::get()->getSettingValue<double>("lookahead")),
            .15f, 1.5f
        );
        float reach = std::clamp(speed * horizon + 60.f, 100.f, 600.f);

        if ((++m_fields->scanTick % 4 == 1) || m_fields->nearby.empty() && m_fields->scanTick == 1)
            m_fields->nearby = gatherHazards(current, reach);

        auto neutral = predict(p, false, speed, horizon);
        auto jumped = predict(p, true, speed, horizon);

        if (Mod::get()->getSettingValue<bool>("debug-draw")) {
            drawPath(neutral, {1.f, .23f, .18f, .95f});
            drawPath(jumped, {.10f, 1.f, .20f, .95f});
        }

        float halfSize = p->m_vehicleSize < .9f ? 6.f : 12.f;
        // Optional diagnostic tap: confirms that the input path works even
        // if no hazards are found. Off by default, use a practice level.
        if (!m_fields->testTapSent && Mod::get()->getSettingValue<bool>("test-click") &&
            p->m_isOnGround && m_fields->pressFrames == 0) {
            m_fields->testElapsed += std::clamp(dt, 0.f, .1f);
            if (m_fields->testElapsed >= 1.0f) {
                holdJump(true);
                m_fields->pressFrames = 2;
                m_fields->testTapSent = true;
                ++m_fields->jumps;
            }
        }

        // Find the nearest hazard at ground height, ahead of the cube.
        float closest = std::numeric_limits<float>::infinity();
        for (auto const& r : m_fields->nearby) {
            if (r.origin.y > current.y + 28.f ||
                r.origin.y + r.size.height < current.y - 25.f)
                continue;
            float distance = r.origin.x - (current.x + halfSize);
            if (distance >= -8.f && distance < closest)
                closest = distance;
        }

        // Choose a jump initiation distance proportional to movement speed.
        // Deliberately early enough for a spike in basic cube gameplay.
        float triggerDistance = std::clamp(speed * .19f + 16.f, 55.f, 110.f);
        bool nearThreat = std::isfinite(closest) &&
                          closest <= triggerDistance &&
                          closest >= -8.f;

        // Compute whether neutral trajectory intersects known hazards. The
        // proximity fallback remains active because the approximate predictor
        // cannot reproduce every game's collision geometry.
        bool neutralThreat = wouldHit(neutral, halfSize);
        if (nearThreat && p->m_isOnGround && m_fields->pressFrames == 0) {
            holdJump(true);
            m_fields->pressFrames = 2;
            ++m_fields->jumps;
        }

        std::string status = "AUTO ON | H:" + std::to_string(m_fields->nearby.size())
            + " | J:" + std::to_string(m_fields->jumps);
        if (m_fields->testTapSent && Mod::get()->getSettingValue<bool>("test-click"))
            status += " TEST SENT";
        else if (nearThreat && p->m_isOnGround) status += " JUMP!";
        else if (neutralThreat) status += " THREAT";
        updateStatus(status, true);
    }
};
