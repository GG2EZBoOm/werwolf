#include "client/App.h"
#include "client/Theme.h"

#include <cmath>
#include <format>

namespace ww {

using eng::Color;
using eng::Rect;

namespace {

Vec2 randomVillageSpot(const World& world, eng::Rng& rng) {
    for (int i = 0; i < 40; ++i) {
        const float a = rng.uniform(0.0f, 2.0f * eng::kPi);
        const float r = rng.uniform(6.0f, 40.0f);
        const Vec2 p{std::cos(a) * r, std::sin(a) * r};
        if (!world.blocked(p, 0.6f)) return p;
    }
    return {0, 8};
}

} // namespace

void App::enterSandbox() {
    ensureWorld(1337u);
    if (!m_characters.isReady()) m_characters.init();
    const Vec3 spawn = m_world.spawnPoint(0, 8);
    m_player = Mover{};
    m_player.position = spawn;
    m_player.yaw = std::atan2(-spawn.x, -spawn.z);
    m_camera.yaw = m_player.yaw;
    m_npcs.clear();
    for (int i = 1; i < 9; ++i) {
        SandboxNpc npc;
        npc.id = static_cast<uint8_t>(i);
        npc.mover.position = m_world.spawnPoint(i, 9);
        npc.target = randomVillageSpot(m_world, m_devRng);
        npc.idle = m_devRng.uniform(0.0f, 2.0f);
        m_npcs.push_back(npc);
    }
    m_screen = Screen::Sandbox;
    m_window.setMouseCaptured(true);
}

void App::updateSandbox() {
    eng::Input& in = m_window.input();
    const float dt = m_dt;

    const Rect menuButton{m_window.width() * 0.5f - 90.0f, m_window.height() * 0.5f + 36.0f, 180.0f, 38.0f};
    if (in.pressed(eng::Key::Escape)) m_window.setMouseCaptured(false);
    else if (in.mousePressed() && !m_window.isMouseCaptured() && !menuButton.contains(in.mouse))
        m_window.setMouseCaptured(true);

    // Tageszeit: fest, automatisch oder mit T vorspulen
    if (m_dev.timeOfDay >= 0.0f) m_previewTimeOfDay = m_dev.timeOfDay;
    else m_previewTimeOfDay = std::fmod(m_previewTimeOfDay + dt * 0.004f, 1.0f);
    if (in.down(eng::Key::T)) {
        m_previewTimeOfDay = std::fmod(m_previewTimeOfDay + dt * 0.08f, 1.0f);
        if (m_dev.timeOfDay >= 0.0f) m_dev.timeOfDay = m_previewTimeOfDay;
    }

    m_camera.update(in, m_window.isMouseCaptured());
    if (in.pressed(eng::Key::R)) m_playerWolfTarget = !m_playerWolfTarget;
    m_playerWolf = eng::approach(m_playerWolf, m_playerWolfTarget ? 1.0f : 0.0f, dt * 1.4f);
    if (in.mousePressed() && m_window.isMouseCaptured() && m_playerWolf > 0.9f && m_playerAttack <= 0.0f) m_playerAttack = 1.0f;
    m_playerAttack = std::max(0.0f, m_playerAttack - dt * 3.0f);

    Vec2 wish = m_window.isMouseCaptured() ? inputDirection(in, m_camera.yaw) : Vec2{0, 0};
    if (m_dev.autoPlay) {
        // Autopilot für Screenshots: im Kreis um den Platz laufen
        const Vec2 p{m_player.position.x, m_player.position.z};
        const Vec2 tangent = Vec2{-p.y, p.x}.normalized();
        const Vec2 inward = (p.length() > 9.0f ? p * -1.0f : p).normalized();
        wish = (tangent + inward * 0.3f).normalized();
        m_camera.yaw = eng::lerp(m_camera.yaw, m_player.yaw, dt * 1.5f);
    }
    const bool sprint = in.down(eng::Key::Shift);
    const float maxSpeed = m_playerWolf > 0.5f ? kWolfSpeed : (sprint ? kSprintSpeed : kWalkSpeed);
    m_player.move(wish, maxSpeed, dt, m_world);

    for (SandboxNpc& npc : m_npcs) {
        const Vec2 pos{npc.mover.position.x, npc.mover.position.z};
        if (npc.idle > 0.0f) {
            npc.idle -= dt;
            npc.mover.move({0, 0}, kWalkSpeed, dt, m_world);
            continue;
        }
        const Vec2 toTarget = npc.target - pos;
        if (toTarget.length() < 1.0f) {
            npc.idle = m_devRng.uniform(1.5f, 6.0f);
            npc.target = randomVillageSpot(m_world, m_devRng);
            continue;
        }
        npc.mover.move(toTarget.normalized(), kWalkSpeed * 0.75f, dt, m_world);
        npc.stuck += dt;
        if (npc.stuck > 2.0f) {
            if ((pos - npc.lastPos).length() < 0.6f) npc.target = randomVillageSpot(m_world, m_devRng);
            npc.lastPos = pos;
            npc.stuck = 0.0f;
        }
    }
}

void App::drawCharacters3D(float night) {
    const float t = static_cast<float>(m_time - m_startTime);
    const bool lanterns = night > 0.45f;
    auto drawOne = [&](uint8_t id, const Mover& m, float wolf, float attack) {
        CharacterPose pose;
        pose.position = m.position;
        pose.yaw = m.yaw;
        pose.walkPhase = m.walkPhase;
        pose.speed = m.speed;
        pose.wolf = wolf;
        pose.attack = attack;
        pose.time = t + static_cast<float>(id) * 1.7f;
        pose.lantern = lanterns && wolf < 0.5f;
        m_characters.draw(m_renderer3d, characterLook(id), pose);
        if (pose.lantern) m_renderer3d.light({m_characters.lanternPosition(pose), 6.5f, {1.0f, 0.62f, 0.28f}, 1.3f * night});
        if (wolf > 0.05f && wolf < 0.95f) {
            // Verwandlungsfunken
            for (int i = 0; i < 12; ++i) {
                const float a = static_cast<float>(i) * 0.52f + t * 3.0f;
                const Vec3 p = m.position + Vec3{std::cos(a) * 0.7f, 0.4f + std::fmod(t * 2.0f + static_cast<float>(i) * 0.3f, 1.8f), std::sin(a) * 0.7f};
                m_renderer3d.particle(p, 0.2f, {3.0f, 0.8f, 0.3f}, 1.0f - std::fabs(wolf - 0.5f) * 2.0f);
            }
        }
    };
    drawOne(0, m_player, m_playerWolf, m_playerAttack);
    for (const SandboxNpc& npc : m_npcs) drawOne(npc.id, npc.mover, 0.0f, 0.0f);
}

void App::renderSandbox3D() {
    const float t = static_cast<float>(m_time - m_startTime);
    m_lastCamera = m_camera.camera(m_player.position, m_world);
    const eng::Environment env = m_worldRenderer.environment(m_previewTimeOfDay, m_player.position, t);
    m_renderer3d.beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight(), m_lastCamera, env);
    m_worldRenderer.draw(m_renderer3d, env, t);
    drawCharacters3D(env.emissive);
    m_renderer3d.endFrame();
}

void App::drawSandboxOverlay() {
    eng::Renderer2D& r = m_renderer;
    const float scale = m_window.dpiScale();

    // Namensschilder über den Köpfen
    auto nameTag = [&](uint8_t id, const Mover& m, float wolf) {
        CharacterPose pose;
        pose.position = m.position;
        pose.wolf = wolf;
        Vec2 screen;
        const Vec3 head = m.position + Vec3{0, m_characters.headHeight(pose) + 0.35f, 0};
        if (!m_renderer3d.worldToScreen(head, screen)) return;
        const float dist = (head - m_lastCamera.position).length();
        if (dist > 30.0f) return;
        const float alpha = eng::clamp01((30.0f - dist) / 8.0f);
        const std::string name = id == 0 ? std::string("Du") : std::format("Dorfbewohner {}", id);
        const Vec2 p = screen / scale;
        const float w = m_fonts.smallBold.measure(name) + 16.0f;
        const Rect plate{p.x - w * 0.5f, p.y - 11.0f, w, 22.0f};
        r.roundRect(plate, 11.0f, Color::hex(0x0A0D1A, 0.6f * alpha));
        r.textCentered(m_fonts.smallBold, plate, name, theme::playerColor(id).fade(alpha));
    };
    for (const SandboxNpc& npc : m_npcs) nameTag(npc.id, npc.mover, 0.0f);

    const int hours = static_cast<int>(m_previewTimeOfDay * 24.0f);
    const int minutes = static_cast<int>(std::fmod(m_previewTimeOfDay * 24.0f * 60.0f, 60.0f));
    r.text(m_fonts.small, {14.0f, 12.0f},
           std::format("{:02}:{:02} Uhr · {:.0f} FPS · WASD laufen, Shift rennen, Maus umsehen, R verwandeln, T Zeit vorspulen, "
                       "Esc Maus freigeben",
                       hours, minutes, 1.0f / std::max(m_dt, 0.001f)),
           Color::hex(0xFFFFFF, 0.75f));
    if (!m_window.isMouseCaptured()) {
        const Rect hint{r.width() * 0.5f - 160.0f, r.height() * 0.5f - 24.0f, 320.0f, 48.0f};
        r.roundRect(hint, 12.0f, Color::hex(0x0A0D1A, 0.75f));
        r.textCentered(m_fonts.ui, hint, "Klicken, um weiterzuspielen", theme::text);
        if (m_ui.button("sandbox/menu", {r.width() * 0.5f - 90.0f, hint.bottom() + 12.0f, 180.0f, 38.0f}, "Hauptmenü"))
            m_screen = Screen::Menu;
    }
}

} // namespace ww
