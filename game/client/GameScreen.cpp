#include "client/App.h"
#include "client/Theme.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace ww {

using eng::Color;
using eng::Rect;

namespace {

uint16_t figureKey(uint8_t id, uint8_t kind) { return static_cast<uint16_t>(id * 4 + kind); }

float wrap01(float v) {
    v = std::fmod(v, 1.0f);
    return v < 0.0f ? v + 1.0f : v;
}

std::string clockText(float seconds) {
    const int s = static_cast<int>(std::ceil(std::max(0.0f, seconds)));
    return std::format("{}:{:02}", s / 60, s % 60);
}

Color promptColor(Interaction kind) {
    switch (kind) {
    case Interaction::Attack: return theme::blood;
    case Interaction::Inspect: return theme::roleColor(Role::Seer);
    case Interaction::Protect: return theme::roleColor(Role::Protector);
    case Interaction::Heal:
    case Interaction::Poison: return theme::roleColor(Role::Witch);
    case Interaction::CupidPick: return theme::roleColor(Role::Cupid);
    case Interaction::HunterShot: return theme::roleColor(Role::Hunter);
    default: return theme::gold;
    }
}

} // namespace

bool App::imAlive() const {
    const PlayerView* me = m_net.view().me();
    return me && me->alive;
}

std::string App::headerTitle(const GameView& v) const {
    switch (v.phase) {
    case Phase::Day: return std::format("Tag {}", v.day);
    case Phase::Assembly: return "Versammlung";
    case Phase::Vote: return "Abstimmung";
    case Phase::Execution: return "Das Urteil";
    case Phase::Night: return std::format("Nacht {}", v.day);
    case Phase::Morning: return "Morgengrauen";
    case Phase::GameOver: return "Spielende";
    default: return "";
    }
}

// --- Einstieg ------------------------------------------------------------------

void App::enterMatch() {
    const GameView& v = m_net.view();
    ensureWorld(v.worldSeed);
    m_inMatch = true;
    m_figures.clear();
    m_entities.clear();
    m_player = Mover{};
    m_player.position = m_world.spawnPoint(0, 1);
    m_pauseOpen = false;
    m_deathTime = -100.0;
    m_wasAlive = true;
    m_chatWolves = false;
    m_bubbles.clear();
    m_chatScroll = 0.0f;
    m_myTransform = 0.0f;
    m_timeOfDay = timeOfDay(v.phase, v.timeLeft, v.phaseDuration);
    m_window.setMouseCaptured(true);
    m_autoTarget = {0, 8};
}

// --- Aktionen und Zielauswahl ----------------------------------------------------

void App::buildPrompts(const GameView& v) {
    m_prompts.clear();
    const Vec3 me = m_player.position;
    const Vec2 fwd{std::sin(m_camera.yaw), std::cos(m_camera.yaw)};
    auto name = [&](uint8_t id) {
        const PlayerView* p = v.player(id);
        return p ? p->name : std::string("?");
    };
    // Bestes Ziel in Blickrichtung innerhalb der Reichweite
    auto aim = [&](float range, bool corpses, auto filter) {
        uint8_t best = kNoPlayer;
        float bestScore = 1e9f;
        for (const EntityState& e : m_entities) {
            if (corpses ? e.kind != EntityCorpse : e.kind != EntityLiving) continue;
            if (!corpses && e.id == v.you) continue;
            if (!filter(e)) continue;
            const Vec2 d{e.x - me.x, e.z - me.z};
            const float dist = d.length();
            if (dist > range) continue;
            const float angle = dist < 0.05f ? 0.0f : std::acos(std::clamp((fwd.x * d.x + fwd.y * d.y) / dist, -1.0f, 1.0f));
            const float maxAngle = dist < 3.0f ? 0.95f : 0.42f;
            if (angle > maxAngle) continue;
            const float score = angle * 4.0f + dist * 0.05f;
            if (score < bestScore) {
                bestScore = score;
                best = e.id;
            }
        }
        return best;
    };
    auto any = [](const EntityState&) { return true; };
    auto add = [&](Interaction kind, const char* key, std::string label, uint8_t target, bool ready, uint8_t param = 0) {
        m_prompts.push_back(Prompt{kind, key, std::move(label), target, ready, param});
    };

    if (v.can(AbilityHunterShot)) {
        const uint8_t t = aim(kHunterRange, false, any);
        add(Interaction::HunterShot, "E", t != kNoPlayer ? "Letzter Schuss auf " + name(t) : "Ziele auf jemanden – dein letzter Schuss",
            t, t != kNoPlayer);
    }
    if (v.can(AbilityVote)) {
        const uint8_t t = aim(60.0f, false, any);
        if (t != kNoPlayer && v.vote == t) add(Interaction::Vote, "E", "Stimme gegen " + name(t) + " zurückziehen", kNoPlayer, true);
        else if (t != kNoPlayer) add(Interaction::Vote, "E", "Stimme gegen " + name(t), t, true);
        else add(Interaction::Vote, "E", "Schau jemanden an, um abzustimmen", kNoPlayer, false);
    }
    if (v.can(AbilityRingBell) && distanceXZ(me, m_world.bell()) < kBellRange)
        add(Interaction::RingBell, "E", "Glocke läuten – Versammlung einberufen", 0, true);
    if (v.can(AbilityTransform)) {
        const bool wolf = m_myTransform > 0.5f;
        add(Interaction::Transform, "R", wolf ? "Zurückverwandeln" : "In einen Werwolf verwandeln", 0, true, wolf ? 0 : 1);
    }
    if (v.can(AbilityAttack)) {
        const uint8_t t = aim(kAttackRange + 0.3f, false, [&](const EntityState& e) {
            const PlayerView* p = v.player(e.id);
            return !p || p->role != Role::Werewolf;
        });
        add(Interaction::Attack, "Klick", t != kNoPlayer ? "Reißen: " + name(t) : "Komm nah genug an ein Opfer heran", t, t != kNoPlayer);
    }
    if (v.can(AbilityInspect)) {
        const uint8_t t = aim(kInspectRange, false, any);
        add(Interaction::Inspect, "E", t != kNoPlayer ? "Vision: Wer ist " + name(t) + "?" : "Schau jemanden an (Vision)", t, t != kNoPlayer);
    }
    if (v.can(AbilityProtect)) {
        uint8_t t = aim(kProtectRange, false, any);
        if (t == kNoPlayer) t = v.you;
        const bool allowed = t != v.lastProtected;
        add(Interaction::Protect, "E", t == v.you ? "Dich selbst beschützen" : "Beschützen: " + name(t), t, allowed);
    }
    if (v.can(AbilityCupid)) {
        uint8_t t = aim(kCupidRange, false, [&](const EntityState& e) {
            return std::find(v.cupidPicks.begin(), v.cupidPicks.end(), e.id) == v.cupidPicks.end();
        });
        if (t == kNoPlayer && std::find(v.cupidPicks.begin(), v.cupidPicks.end(), v.you) == v.cupidPicks.end()) t = v.you;
        add(Interaction::CupidPick, "E", t == v.you ? "Amors Pfeil auf dich selbst" : "Amors Pfeil: " + name(t), t, t != kNoPlayer);
    }
    if (v.can(AbilityHeal)) {
        const uint8_t t = aim(kHealRange + 0.5f, true, any);
        if (t != kNoPlayer) add(Interaction::Heal, "E", "Heiltrank für " + name(t), t, true);
    }
    if (v.can(AbilityPoison)) {
        const uint8_t t = aim(kPoisonRange, false, any);
        add(Interaction::Poison, "F", t != kNoPlayer ? "Gift für " + name(t) : "Gift: geh nah an jemanden heran", t, t != kNoPlayer);
    }
    m_aimTarget = kNoPlayer;
    for (const Prompt& p : m_prompts) {
        if (p.target != kNoPlayer && p.kind != Interaction::Transform && p.kind != Interaction::RingBell) {
            m_aimTarget = p.target;
            break;
        }
    }
}

bool App::canMoveFreely() const {
    const GameView& v = m_net.view();
    return v.phase != Phase::GameOver;
}

void App::updateMatch() {
    const GameView& v = m_net.view();
    eng::Input& in = m_window.input();
    const float dt = m_dt;
    if (v.worldSeed != 0 && v.worldSeed != m_world.seed()) ensureWorld(v.worldSeed);

    if (auto c = m_net.takeCorrection()) {
        m_player.position = m_world.ground(c->x, c->z);
        m_player.velocity = {0, 0};
        m_player.yaw = c->yaw;
        m_camera.yaw = c->yaw;
    }

    const bool alive = imAlive();
    if (m_wasAlive && !alive) m_deathTime = m_time;
    m_wasAlive = alive;

    // Maus und Menüs
    const bool chatFocused = m_ui.isFocused("chat");
    const bool over = v.phase == Phase::GameOver;
    if (over) m_window.setMouseCaptured(false);
    if (m_chatWasFocused && !chatFocused && !m_pauseOpen && !over) m_window.setMouseCaptured(true);
    m_chatWasFocused = chatFocused;
    if (!chatFocused && !over && in.pressed(eng::Key::Escape)) {
        if (m_window.isMouseCaptured()) {
            m_window.setMouseCaptured(false);
            m_pauseOpen = true;
        } else {
            m_pauseOpen = false;
            m_window.setMouseCaptured(true);
        }
    }
    if (!chatFocused && !over && in.pressed(eng::Key::Enter) && !m_pauseOpen) {
        m_ui.focus("chat");
        m_chatFocusTime = m_time;
        m_window.setMouseCaptured(false);
    }
    if (!m_window.isMouseCaptured() && !m_pauseOpen && !over && !chatFocused && in.mousePressed()) {
        const bool onHud = std::any_of(m_hudBlockers.begin(), m_hudBlockers.end(), [&](const Rect& r) { return r.contains(in.mouse); });
        if (!onHud) {
            m_window.setMouseCaptured(true);
            in.consumeMouse();
        }
    }
    if (in.pressed(eng::Key::Tab) && v.yourRole == Role::Werewolf && alive) m_chatWolves = !m_chatWolves;
    const bool captured = m_window.isMouseCaptured();

    // Figuren aus den Snapshots
    m_entities = m_net.entities(m_time);
    for (auto& [key, f] : m_figures) f.present = false;
    for (const EntityState& e : m_entities) {
        if (e.id == v.you && e.kind == EntityLiving) {
            m_myTransform = e.transform;
            continue;
        }
        if (e.id == v.you && e.kind == EntityGhost) continue;
        Figure& f = m_figures[figureKey(e.id, e.kind)];
        const Vec3 pos = m_world.ground(e.x, e.z);
        if (f.mover.position.lengthSq() == 0.0f) {
            f.mover.position = pos;
            f.mover.yaw = e.yaw;
        }
        f.mover.animateTo(pos, e.yaw, dt);
        f.transform = e.transform;
        f.flags = e.flags;
        f.kind = e.kind;
        f.present = true;
    }
    std::erase_if(m_figures, [](const auto& kv) { return !kv.second.present; });
    if (!alive) m_myTransform = 0.0f;

    // Eigene Bewegung (vorhergesagt, Server prüft)
    m_camera.update(in, captured);
    Vec2 wish = (captured && !chatFocused) ? inputDirection(in, m_camera.yaw) : Vec2{0, 0};
    if (m_dev.autoPlay && !over) {
        const Vec2 pos{m_player.position.x, m_player.position.z};
        if ((m_autoTarget - pos).length() < 1.5f || m_devRng.chance(0.002f)) {
            const float a = m_devRng.uniform(0.0f, 2.0f * eng::kPi);
            m_autoTarget = m_world.resolveCollision({std::cos(a) * m_devRng.uniform(5.0f, 30.0f), std::sin(a) * m_devRng.uniform(5.0f, 30.0f)}, 0.6f);
        }
        if (v.phase == Phase::Day || v.phase == Phase::Night) wish = (m_autoTarget - pos).normalized();
        m_camera.yaw += wrapAngle(m_player.yaw - m_camera.yaw) * std::min(1.0f, dt * 1.5f);
    }
    const bool sprint = in.down(eng::Key::Shift);
    float speed = sprint ? kSprintSpeed : kWalkSpeed;
    if (!alive) speed = kGhostSpeed;
    else if (m_myTransform >= 0.99f) speed = kWolfSpeed;
    else if (m_myTransform > 0.01f) speed = 0.0f; // während der Verwandlung erstarrt
    if (!canMoveFreely()) speed = 0.0f;
    m_player.move(wish, speed, dt, m_world, alive);
    if (m_time - m_lastMoveSent >= proto::kMoveInterval) {
        m_net.sendMove(m_player.position.x, m_player.position.z, m_player.yaw, sprint && wish.length() > 0.1f ? EntitySprint : 0);
        m_lastMoveSent = m_time;
    }

    // Aktionen
    buildPrompts(v);
    if (captured && !chatFocused) {
        auto trigger = [&](const char* key) {
            for (const Prompt& p : m_prompts) {
                if (std::string_view(p.key) != key || !p.ready) continue;
                m_net.sendInteract(p.kind, p.kind == Interaction::Transform ? p.param : p.target);
                return;
            }
        };
        if (in.pressed(eng::Key::E)) trigger("E");
        if (in.pressed(eng::Key::F)) trigger("F");
        if (in.pressed(eng::Key::R)) trigger("R");
        if (in.mousePressed()) trigger("Klick");
    }

    // Tageszeit folgt weich der Spielphase
    const float target = timeOfDay(v.phase, m_net.timeLeft(m_time), v.phaseDuration);
    float diff = target - m_timeOfDay;
    if (diff > 0.5f) diff -= 1.0f;
    if (diff < -0.5f) diff += 1.0f;
    m_timeOfDay = wrap01(m_timeOfDay + diff * std::min(1.0f, dt * 1.2f));
}

// --- 3D ------------------------------------------------------------------------

void App::renderMatch3D() {
    const GameView& v = m_net.view();
    const float t = static_cast<float>(m_time - m_startTime);
    const bool alive = imAlive();

    m_lastCamera = m_camera.camera(m_player.position + Vec3{0, alive ? 0.0f : 0.4f, 0}, m_world);
    eng::Environment env = m_worldRenderer.environment(m_timeOfDay, m_player.position, t);
    if (!alive && v.phase != Phase::GameOver) {
        env.saturation *= 0.45f;
        env.gradeTint = mul(env.gradeTint, Vec3{0.85f, 0.95f, 1.2f});
    }
    m_renderer3d.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), m_lastCamera, env);
    m_worldRenderer.draw(m_renderer3d, env, t);
    const float night = env.emissive;

    auto drawFigure = [&](uint8_t id, const Mover& m, float transform, uint8_t flags, uint8_t kind) {
        CharacterPose pose;
        pose.position = m.position;
        pose.yaw = m.yaw;
        pose.walkPhase = m.walkPhase;
        pose.speed = m.speed;
        pose.wolf = transform;
        pose.attack = (flags & EntityAttack) ? 1.0f : 0.0f;
        pose.time = t + static_cast<float>(id) * 1.7f;
        pose.dead = kind == EntityCorpse;
        pose.ghost = kind == EntityGhost;
        pose.lantern = (flags & EntityLantern) != 0 && kind == EntityLiving && night > 0.3f;
        m_characters.draw(m_renderer3d, characterLook(id), pose);
        if (pose.lantern) m_renderer3d.light({m_characters.lanternPosition(pose), 6.5f, {1.0f, 0.62f, 0.28f}, 1.3f * night});
        if (transform > 0.02f && transform < 0.98f) {
            for (int i = 0; i < 14; ++i) {
                const float a = static_cast<float>(i) * 0.45f + t * 4.0f;
                const Vec3 p = m.position + Vec3{std::cos(a) * 0.8f, 0.3f + std::fmod(t * 2.5f + static_cast<float>(i) * 0.27f, 2.0f),
                                                 std::sin(a) * 0.8f};
                m_renderer3d.particle(p, 0.22f, {3.5f, 0.7f, 0.25f}, 1.0f - std::fabs(transform - 0.5f) * 2.0f);
            }
        }
        if (pose.ghost) m_renderer3d.particle(m.position + Vec3{0, 1.2f, 0}, 2.4f, {0.12f, 0.2f, 0.45f}, 0.3f);
    };

    // eigene Figur
    {
        uint8_t flags = 0;
        for (const EntityState& e : m_entities)
            if (e.id == v.you && e.kind == EntityLiving) flags = e.flags;
        if (alive) drawFigure(v.you, m_player, m_myTransform, flags | (night > 0.3f && m_myTransform < 0.01f ? EntityLantern : 0), EntityLiving);
        else drawFigure(v.you, m_player, 0.0f, 0, EntityGhost);
    }
    for (const auto& [key, f] : m_figures) drawFigure(static_cast<uint8_t>(key / 4), f.mover, f.transform, f.flags, f.kind);

    // Zielmarkierung am Boden
    if (m_aimTarget != kNoPlayer && !m_prompts.empty()) {
        Color c = theme::gold;
        Vec3 pos;
        bool found = false;
        for (const Prompt& p : m_prompts) {
            if (p.target == m_aimTarget) {
                c = promptColor(p.kind);
                break;
            }
        }
        for (const EntityState& e : m_entities) {
            if (e.id != m_aimTarget || e.kind == EntityGhost) continue;
            pos = m_world.ground(e.x, e.z);
            found = true;
            if (e.kind == EntityLiving) break;
        }
        if (m_aimTarget == v.you) {
            pos = m_player.position;
            found = true;
        }
        if (found) {
            for (int i = 0; i < 24; ++i) {
                const float a = static_cast<float>(i) / 24.0f * 2.0f * eng::kPi + t * 1.5f;
                m_renderer3d.particle(pos + Vec3{std::cos(a) * 0.75f, 0.08f, std::sin(a) * 0.75f}, 0.16f,
                                      Vec3{c.r, c.g, c.b} * 3.0f, 0.9f);
            }
        }
    }
    m_renderer3d.endFrame();
}

// --- HUD -----------------------------------------------------------------------

void App::drawNameTags(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const float scale = m_window.dpiScale();
    const bool night = m_timeOfDay > 0.78f || m_timeOfDay < 0.22f;
    std::map<uint8_t, int> votes;
    for (const PlayerView& p : v.players)
        if (p.vote != kNoPlayer) ++votes[p.vote];

    auto tag = [&](uint8_t id, Vec3 feet, float height, bool corpse, bool ghost) {
        const PlayerView* p = v.player(id);
        if (!p) return;
        Vec2 screen;
        const Vec3 head = feet + Vec3{0, height, 0};
        if (!m_renderer3d.worldToScreen(head, screen)) return;
        const float dist = (head - m_lastCamera.position).length();
        const float maxDist = corpse ? 14.0f : night ? 11.0f : 26.0f;
        if (dist > maxDist) return;
        const float alpha = eng::clamp01((maxDist - dist) / 5.0f);
        const Vec2 pt = screen / scale;

        std::string label = p->name;
        if (corpse) label = "† " + label;
        if (p->role != Role::Unknown && id != v.you) label = std::string(roleIcon(p->role)) + " " + label;
        const float w = m_fonts.smallBold.measure(label) + 16.0f;
        const Rect plate{pt.x - w * 0.5f, pt.y - 11.0f, w, 22.0f};
        Color fg = corpse ? theme::muted : ghost ? Color::hex(0xB8CCF2) : theme::playerColor(id);
        if (id == m_aimTarget) r.roundRectOutline(plate.shrink(-2.0f), 12.0f, 1.5f, theme::gold.fade(alpha));
        r.roundRect(plate, 11.0f, Color::hex(0x0A0D1A, 0.62f * alpha));
        r.textCentered(m_fonts.smallBold, plate, label, fg.fade(alpha));

        if (!corpse && votes.count(id)) {
            const Rect badge{plate.right() + 4.0f, plate.y, 24.0f, 22.0f};
            r.roundRect(badge, 11.0f, theme::blood.fade(alpha));
            r.textCentered(m_fonts.smallBold, badge, std::to_string(votes[id]), Color::hex(0xFFFFFF, alpha));
        }

        // Sprechblase
        auto it = m_bubbles.find(id);
        if (!corpse && it != m_bubbles.end()) {
            const double age = m_time - it->second.time;
            if (age < 6.0) {
                const float ba = alpha * eng::clamp01(static_cast<float>(6.0 - age) / 0.8f);
                std::vector<std::string> lines = m_fonts.small.wrap(it->second.text, 230.0f);
                if (lines.size() > 3) lines.resize(3);
                float bw = 0.0f;
                for (const std::string& l : lines) bw = std::max(bw, m_fonts.small.measure(l));
                const float lh = m_fonts.small.lineHeight();
                const Rect box{pt.x - bw * 0.5f - 10.0f, plate.y - 12.0f - lh * static_cast<float>(lines.size()) - 12.0f,
                               bw + 20.0f, lh * static_cast<float>(lines.size()) + 12.0f};
                Color bg = Color::hex(0xF5F1E8, 0.94f * ba), text = Color::hex(0x1B1406, ba);
                if (it->second.channel == ChatChannel::Wolves) {
                    bg = Color::hex(0x5A1A16, 0.94f * ba);
                    text = Color::hex(0xF8DCD8, ba);
                } else if (it->second.channel == ChatChannel::Dead) {
                    bg = Color::hex(0x34405E, 0.85f * ba);
                    text = Color::hex(0xDCE3F2, ba);
                }
                r.roundRect(box, 9.0f, bg);
                r.triangle({pt.x - 6.0f, box.bottom() - 1.0f}, {pt.x + 6.0f, box.bottom() - 1.0f}, {pt.x, box.bottom() + 7.0f}, bg);
                float y = box.y + 6.0f;
                for (const std::string& l : lines) {
                    r.text(m_fonts.small, {box.x + 10.0f, y}, l, text);
                    y += lh;
                }
            }
        }
    };

    for (const auto& [key, f] : m_figures) {
        const uint8_t id = static_cast<uint8_t>(key / 4);
        CharacterPose pose;
        pose.wolf = f.transform;
        pose.dead = f.kind == EntityCorpse;
        tag(id, f.mover.position, m_characters.headHeight(pose) + 0.35f, f.kind == EntityCorpse, f.kind == EntityGhost);
    }
}

void App::drawPhaseBox(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const float w = 360.0f;
    const Rect box{W * 0.5f - w * 0.5f, 14.0f, w, 66.0f};
    r.roundRect(box, 14.0f, Color::hex(0x0A0D1A, 0.62f));
    r.roundRectOutline(box, 14.0f, 1.0f, theme::panelBorder);
    r.textCentered(m_fonts.heading, {box.x, box.y + 4.0f, box.w, 40.0f}, headerTitle(v), theme::goldLight);
    const float timeLeft = m_net.timeLeft(m_time);
    if (v.phaseDuration > 0.0f && v.phase != Phase::GameOver) {
        const bool urgent = timeLeft < 10.0f;
        const Color tc = urgent ? theme::blood.fade(0.6f + 0.4f * std::sin(static_cast<float>(m_time) * 8.0f)) : theme::text;
        r.textRight(m_fonts.uiBold, {box.right() - 16.0f, box.y + 14.0f}, clockText(timeLeft), tc);
        const Rect bar{box.x + 18.0f, box.bottom() - 14.0f, box.w - 36.0f, 4.0f};
        r.roundRect(bar, 2.0f, Color::hex(0xFFFFFF, 0.1f));
        r.roundRect({bar.x, bar.y, bar.w * eng::clamp01(timeLeft / v.phaseDuration), bar.h}, 2.0f, urgent ? theme::blood : theme::gold);
    }
    int alive = 0;
    for (const PlayerView& p : v.players)
        if (p.alive) ++alive;
    r.text(m_fonts.small, {box.x + 16.0f, box.y + 16.0f}, std::format("{} leben", alive), theme::muted);
}

void App::drawRoleCard(Rect area, const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const Role role = v.yourRole;
    const Color rc = theme::roleColor(role);
    const bool alive = imAlive();

    std::vector<std::pair<std::string, Color>> lines;
    if (!alive) lines.push_back({"Du bist tot – als Geist siehst du alles.", theme::ghost});
    if (role == Role::Werewolf) {
        std::string pack;
        for (const PlayerView& p : v.players)
            if (p.role == Role::Werewolf && p.id != v.you) pack += (pack.empty() ? "" : ", ") + p.name;
        lines.push_back({pack.empty() ? "Du jagst allein." : "Rudel: " + pack, theme::blood});
        if (v.phase == Phase::Night) lines.push_back({v.packKillsLeft ? "Das Rudel hungert noch." : "Das Rudel ist satt.", theme::muted});
        lines.push_back({m_chatWolves ? "Chat: Rudel (Tab wechselt)" : "Chat: Nähe (Tab: Rudel)", theme::faint});
    }
    if (v.loverPartner != kNoPlayer) {
        const PlayerView* p = v.player(v.loverPartner);
        lines.push_back({std::string("♥ Verliebt in ") + (p ? p->name : "?"), theme::roleColor(Role::Cupid)});
    }
    if (role == Role::Witch) {
        lines.push_back({std::string("Heiltrank: ") + (v.healAvailable ? "bereit" : "verbraucht"), v.healAvailable ? rc : theme::faint});
        lines.push_back({std::string("Gifttrank: ") + (v.poisonAvailable ? "bereit" : "verbraucht"), v.poisonAvailable ? rc : theme::faint});
    }
    if (role == Role::Seer)
        for (const SeerResult& s : v.seerResults) {
            const PlayerView* p = v.player(s.player);
            lines.push_back({(p ? p->name : "?") + " – " + roleName(s.role), theme::roleColor(s.role)});
        }
    if (role == Role::Protector && v.protectedTarget != kNoPlayer) {
        const PlayerView* p = v.player(v.protectedTarget);
        lines.push_back({"Beschützt: " + (p ? p->name : std::string("?")), rc});
    }

    const float h = 74.0f + static_cast<float>(lines.size()) * 19.0f;
    const Rect card{area.x, area.bottom() - h, 300.0f, h};
    r.roundRect(card, 14.0f, Color::hex(0x0A0D1A, 0.66f));
    r.roundRectOutline(card, 14.0f, 1.0f, theme::panelBorder);
    const eng::Vec2 icon{card.x + 36.0f, card.y + 36.0f};
    r.circle(icon, 24.0f, Color::hex(0x0B0F1F));
    r.circleOutline(icon, 24.0f, 2.0f, rc);
    r.textCentered(m_fonts.iconLarge, {icon.x - 24.0f, icon.y - 24.0f, 48.0f, 48.0f}, roleIcon(role), rc);
    r.text(m_fonts.large, {card.x + 70.0f, card.y + 12.0f}, roleName(role), rc);
    const Team team = roleTeam(role);
    r.text(m_fonts.small, {card.x + 71.0f, card.y + 42.0f}, std::string("Lager: ") + teamName(team), theme::teamColor(team));
    float y = card.y + 68.0f;
    for (const auto& [text, color] : lines) {
        r.text(m_fonts.small, {card.x + 16.0f, y}, m_fonts.small.ellipsize(text, card.w - 28.0f), color);
        y += 19.0f;
    }
}

void App::drawPrompts(const GameView&) {
    eng::Renderer2D& r = m_renderer;
    if (m_prompts.empty()) return;
    float y = r.height() - 30.0f - static_cast<float>(m_prompts.size()) * 34.0f;
    for (const Prompt& p : m_prompts) {
        const float tw = m_fonts.ui.measure(p.label);
        const float kw = std::max(28.0f, m_fonts.smallBold.measure(p.key) + 14.0f);
        const float w = kw + tw + 30.0f;
        const Rect row{r.width() * 0.5f - w * 0.5f, y, w, 30.0f};
        const Color c = promptColor(p.kind);
        r.roundRect(row, 10.0f, Color::hex(0x0A0D1A, p.ready ? 0.72f : 0.45f));
        const Rect key{row.x + 6.0f, row.y + 4.0f, kw, 22.0f};
        r.roundRect(key, 6.0f, p.ready ? c : Color::hex(0x3A3F4E));
        r.textCentered(m_fonts.smallBold, key, p.key, p.ready ? Color::hex(0x10131C) : theme::muted);
        r.textCentered(m_fonts.ui, {key.right() + 10.0f, row.y, tw, row.h}, p.label, p.ready ? theme::text : theme::muted);
        y += 34.0f;
    }
}

void App::drawVotePanel(const GameView& v) {
    if (v.phase != Phase::Vote && v.phase != Phase::Execution) return;
    eng::Renderer2D& r = m_renderer;
    std::map<uint8_t, int> votes;
    for (const PlayerView& p : v.players)
        if (p.vote != kNoPlayer) ++votes[p.vote];
    std::vector<const PlayerView*> alive;
    for (const PlayerView& p : v.players)
        if (p.alive) alive.push_back(&p);
    const float rowH = 30.0f;
    const Rect panel{16.0f, 110.0f, 240.0f, 48.0f + rowH * static_cast<float>(alive.size())};
    m_hudBlockers.push_back(panel);
    r.roundRect(panel, 14.0f, Color::hex(0x0A0D1A, 0.7f));
    r.text(m_fonts.uiBold, {panel.x + 14.0f, panel.y + 10.0f}, "Stimmen", theme::text);
    float y = panel.y + 40.0f;
    const bool canVote = v.can(AbilityVote);
    for (const PlayerView* p : alive) {
        const Rect row{panel.x + 8.0f, y, panel.w - 16.0f, rowH - 4.0f};
        const bool mine = v.vote == p->id;
        if (mine) r.roundRect(row, 8.0f, theme::gold.withAlpha(0.18f));
        const bool clickable = canVote && p->id != v.you && !m_window.isMouseCaptured();
        if (m_ui.clickArea(std::format("vote/{}", p->id), row, clickable))
            m_net.sendInteract(Interaction::Vote, mine ? kNoPlayer : p->id);
        if (clickable && m_ui.hovered(row)) r.roundRectOutline(row, 8.0f, 1.0f, theme::gold.withAlpha(0.5f));
        r.circle({row.x + 14.0f, row.center().y}, 6.0f, theme::playerColor(p->id));
        r.textCentered(m_fonts.ui, {row.x + 28.0f, row.y, m_fonts.ui.measure(p->name), row.h}, p->name, p->id == v.you ? theme::goldLight : theme::text);
        if (votes.count(p->id)) {
            const std::string n = std::to_string(votes[p->id]);
            r.textCentered(m_fonts.uiBold, {row.right() - 30.0f, row.y, 24.0f, row.h}, n, theme::blood);
        }
        y += rowH;
    }
}

void App::drawMinimap(Rect area, const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const float size = 150.0f;
    const eng::Vec2 c{area.right() - size * 0.5f - 16.0f, 16.0f + size * 0.5f};
    const float R = size * 0.5f;
    const float scale = R / 70.0f; // 70 m Radius
    auto toMap = [&](float x, float z) {
        // Norden (−z) oben, Osten (+x) rechts
        return eng::Vec2{c.x + x * scale, c.y + z * scale};
    };
    r.circle(c, R + 3.0f, Color::hex(0x0A0D1A, 0.75f));
    r.circle(c, R, Color::hex(0x24331F, 0.85f));
    r.circle(toMap(0, 0), World::kSquareRadius * scale, Color::hex(0x7E786C, 0.9f));
    r.circle(toMap(0, 0), 1.6f, Color::hex(0xFF9A3A));
    r.rect({toMap(48, -15).x, toMap(48, -15).y, 26.0f * scale, 30.0f * scale}, Color::hex(0xB89A4A, 0.7f));
    for (const House& h : m_world.houses()) {
        const eng::Vec2 p = toMap(h.position.x, h.position.z);
        r.roundRect({p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f}, 1.5f, Color::hex(0xC9B48C));
    }
    const eng::Vec2 chapel = toMap(m_world.chapel().x, m_world.chapel().z);
    r.roundRect({chapel.x - 4.0f, chapel.y - 6.0f, 8.0f, 12.0f}, 1.5f, Color::hex(0xE2DCCD));
    const eng::Vec2 bell = toMap(m_world.bell().x, m_world.bell().z);
    r.circle(bell, 3.0f, theme::gold);
    r.circleOutline(c, R, 1.5f, theme::gold.withAlpha(0.6f));

    // eigene Position mit Blickrichtung
    eng::Vec2 me = toMap(m_player.position.x, m_player.position.z);
    const eng::Vec2 delta = me - c;
    if (delta.length() > R - 6.0f) me = c + delta.normalized() * (R - 6.0f);
    const eng::Vec2 dir{std::sin(m_camera.yaw), std::cos(m_camera.yaw)};
    const eng::Vec2 side{-dir.y, dir.x};
    r.triangle(me + dir * 8.0f, me - dir * 5.0f + side * 5.0f, me - dir * 5.0f - side * 5.0f,
               imAlive() ? theme::goldLight : theme::ghost);
    (void)v;
}

void App::drawBanner() {
    const double age = m_time - m_bannerTime;
    constexpr double kDuration = 3.4;
    if (age < 0.0 || age > kDuration) return;
    eng::Renderer2D& r = m_renderer;
    const float fadeIn = eng::smoothstep(0.0f, 0.4f, static_cast<float>(age));
    const float fadeOut = 1.0f - eng::smoothstep(static_cast<float>(kDuration) - 0.8f, static_cast<float>(kDuration), static_cast<float>(age));
    const float a = fadeIn * fadeOut;
    const float w = 520.0f;
    const Rect band{r.width() * 0.5f - w * 0.5f, r.height() * 0.3f - 50.0f + (1.0f - fadeIn) * 16.0f, w, 100.0f};
    r.roundRect(band, 18.0f, Color::hex(0x05070F, 0.6f * a));
    r.textCentered(m_fonts.title, {band.x, band.y + 4.0f, band.w, 64.0f}, m_bannerTitle, theme::goldLight.fade(a));
    r.textCentered(m_fonts.narration, {band.x, band.y + 70.0f, band.w, 22.0f}, m_bannerSubtitle, theme::text.fade(a));
}

void App::drawPauseMenu(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    r.rect({0, 0, r.width(), r.height()}, Color::hex(0x000000, 0.35f));
    const Rect panel{r.width() * 0.5f - 160.0f, r.height() * 0.5f - 120.0f, 320.0f, m_net.isHost() ? 240.0f : 194.0f};
    m_hudBlockers.push_back(panel);
    drawPanel(panel);
    r.textCentered(m_fonts.large, {panel.x, panel.y + 18.0f, panel.w, 30.0f}, "Pause", theme::text);
    Rect b{panel.x + 24.0f, panel.y + 64.0f, panel.w - 48.0f, 40.0f};
    if (m_ui.button("pause/resume", b, "Weiterspielen", eng::ui::ButtonKind::Primary)) {
        m_pauseOpen = false;
        m_window.setMouseCaptured(true);
    }
    b = b.offset(0, 48.0f);
    const bool leaveArmed = m_time - m_leaveArmedTime < 3.0;
    if (m_ui.button("pause/leave", b, leaveArmed ? "Wirklich verlassen?" : "Spiel verlassen",
                    leaveArmed ? eng::ui::ButtonKind::Danger : eng::ui::ButtonKind::Normal)) {
        if (leaveArmed) leaveSession();
        else m_leaveArmedTime = m_time;
    }
    if (m_net.isHost() && v.phase != Phase::GameOver) {
        b = b.offset(0, 48.0f);
        const bool abortArmed = m_time - m_abortArmedTime < 3.0;
        if (m_ui.button("pause/abort", b, abortArmed ? "Wirklich abbrechen?" : "Runde abbrechen",
                        abortArmed ? eng::ui::ButtonKind::Danger : eng::ui::ButtonKind::Ghost)) {
            if (abortArmed) m_net.sendReturnToLobby();
            else m_abortArmedTime = m_time;
        }
    }
}

void App::drawGameOver(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const size_t rows = (v.players.size() + 1) / 2;
    const float w = std::min(r.width() - 420.0f, 620.0f);
    const float h = 168.0f + static_cast<float>(rows) * 28.0f + 64.0f;
    const Rect panel{r.width() * 0.5f - w * 0.5f - 100.0f, r.height() * 0.5f - h * 0.5f, w, h};
    m_hudBlockers.push_back(panel);
    drawPanel(panel, 18.0f);
    const Color tc = theme::teamColor(v.winner);
    std::string title;
    switch (v.winner) {
    case Team::Village: title = "Das Dorf gewinnt!"; break;
    case Team::Wolves: title = "Die Werwölfe gewinnen!"; break;
    case Team::Lovers: title = "Die Verliebten gewinnen!"; break;
    default: title = "Niemand gewinnt …"; break;
    }
    r.textCentered(m_fonts.heading, {panel.x, panel.y + 22.0f, panel.w, 44.0f}, title, tc);
    const PlayerView* me = v.me();
    const bool won = me && me->winner;
    r.textCentered(m_fonts.ui, {panel.x, panel.y + 70.0f, panel.w, 24.0f}, won ? "★ Du hast gewonnen!" : "Du hast verloren.",
                   won ? theme::goldLight : theme::muted);
    const float colW = (panel.w - 56.0f) * 0.5f;
    for (size_t i = 0; i < v.players.size(); ++i) {
        const PlayerView& p = v.players[i];
        const float x = panel.x + 28.0f + (i < rows ? 0.0f : colW);
        const float y = panel.y + 112.0f + static_cast<float>(i % rows) * 28.0f;
        r.text(m_fonts.smallBold, {x, y}, p.winner ? "★" : (p.alive ? "" : "†"), p.winner ? theme::goldLight : theme::faint);
        r.text(m_fonts.ui, {x + 18.0f, y - 2.0f}, m_fonts.ui.ellipsize(p.name, colW * 0.5f), p.alive ? theme::text : theme::muted);
        r.text(m_fonts.small, {x + 22.0f + colW * 0.5f, y}, std::string(roleIcon(p.role)) + " " + roleName(p.role), theme::roleColor(p.role));
    }
    const Rect bottom{panel.x + 28.0f, panel.bottom() - 58.0f, panel.w - 56.0f, 42.0f};
    if (m_net.isHost()) {
        if (m_ui.button("over/lobby", {bottom.center().x - 120.0f, bottom.y, 240.0f, bottom.h}, "Zurück in die Lobby",
                        eng::ui::ButtonKind::Primary))
            m_net.sendReturnToLobby();
    } else if (m_ui.button("over/leave", {bottom.center().x - 90.0f, bottom.y, 180.0f, bottom.h}, "Spiel verlassen",
                           eng::ui::ButtonKind::Ghost)) {
        leaveSession();
    }
}

void App::drawMatchHud() {
    const GameView& v = m_net.view();
    eng::Renderer2D& r = m_renderer;
    m_hudBlockers.clear();
    const float W = r.width(), H = r.height();
    const bool over = v.phase == Phase::GameOver;

    drawNameTags(v);
    if (m_window.isMouseCaptured()) {
        r.circle({W * 0.5f, H * 0.5f}, 2.5f, Color::hex(0xFFFFFF, 0.8f));
        r.circleOutline({W * 0.5f, H * 0.5f}, 9.0f, 1.2f, Color::hex(0xFFFFFF, 0.25f));
    }
    drawPhaseBox(v);
    drawMinimap({0, 0, W, H}, v);
    drawBanner();
    if (!over) {
        drawRoleCard({16.0f, 0, 300.0f, H - 16.0f}, v);
        drawPrompts(v);
        drawVotePanel(v);
    }

    const Rect chat{W - 396.0f, H - 286.0f, 380.0f, 270.0f};
    m_hudBlockers.push_back(chat);
    drawChat(chat, true);

    if (m_time - m_deathTime < 5.0 && !over) {
        const float a = eng::clamp01(static_cast<float>(5.0 - (m_time - m_deathTime)) / 1.5f);
        r.textCentered(m_fonts.title, {0, H * 0.42f, W, 80.0f}, "Du bist gestorben", Color::hex(0xD64B3F, a));
        r.textCentered(m_fonts.narration, {0, H * 0.42f + 84.0f, W, 24.0f}, "Als Geist wanderst du ungesehen durchs Dorf.", theme::text.fade(a));
    }

    if (over) drawGameOver(v);
    else if (m_pauseOpen) drawPauseMenu(v);
    else if (!m_window.isMouseCaptured() && !m_ui.isFocused("chat")) {
        const Rect hint{W * 0.5f - 170.0f, H * 0.5f + 40.0f, 340.0f, 34.0f};
        r.roundRect(hint, 10.0f, Color::hex(0x0A0D1A, 0.7f));
        r.textCentered(m_fonts.small, hint, "Klicke ins Bild, um weiterzuspielen (Esc: Pause)", theme::text);
    }
}

} // namespace ww
