#include "client/App.h"
#include "client/Theme.h"

#include <algorithm>

namespace ww {

using eng::Color;
using eng::Rect;

namespace {

std::string chatPrefix(const LogEntry& e) {
    switch (e.channel) {
    case ChatChannel::Wolves: return "[Rudel] " + e.name + ": ";
    case ChatChannel::Dead: return "[Geist] " + e.name + ": ";
    case ChatChannel::Family: return "[Geschwister] " + e.name + ": ";
    default: return e.name + ": ";
    }
}

} // namespace

void App::drawChat(const Rect& area, bool compact) {
    eng::Renderer2D& r = m_renderer;
    eng::Input& in = m_window.input();
    const GameView& v = m_net.view();
    const PlayerView* me = v.me();
    const bool alive = me ? me->alive : true;
    const bool focused = m_ui.isFocused("chat");

    // Schiefertafel mit Kreide – im Spiel etwas durchscheinend
    m_kit.slate(area, compact ? (focused ? 0.95f : 0.82f) : 1.0f);
    Rect inner = area.shrink(compact ? 14.0f : 22.0f, compact ? 12.0f : 20.0f);

    // Kanal bestimmen (gleiche Regeln wie auf dem Server)
    ChatChannel channel = ChatChannel::Global;
    if (v.inGame()) {
        if (v.phase == Phase::GameOver) channel = ChatChannel::Global;
        else if (!alive) channel = ChatChannel::Dead;
        else if (m_chatWolves && isWolfRole(v.yourRole)) channel = ChatChannel::Wolves;
        else if (m_chatWolves && (v.acting == Role::Medium || v.yourRole == Role::Medium) && v.phase == Phase::Night)
            channel = ChatChannel::Dead;
        else if (m_chatWolves && (v.yourRole == Role::Sister || v.yourRole == Role::Brother)) channel = ChatChannel::Family;
        else if (isGatheringPhase(v.phase)) channel = ChatChannel::Global;
        else channel = ChatChannel::Nearby;
    }

    if (!compact) {
        Rect header = inner.takeTop(34.0f);
        r.text(m_fonts.handLarge, {header.x, header.y - 4.0f}, "Dorfgeflüster", palette::chalk);
        inner.takeTop(4.0f);
        m_kit.inkLine({inner.x, inner.y}, {inner.right(), inner.y + 1.0f}, 1.2f, palette::chalk.withAlpha(0.35f), 1.0f, 9);
        inner.takeTop(10.0f);
    }

    const Rect input = inner.takeBottom(compact ? 36.0f : 42.0f);
    inner.takeBottom(8.0f);
    const Rect logArea = inner;

    std::deque<LogEntry>& log = m_net.log();
    const float width = logArea.w;
    float total = 0.0f;
    float y = logArea.bottom() + m_chatScroll;
    r.pushClip(logArea);
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        LogEntry& e = *it;
        const bool narration = !e.isChat && e.kind == NoticeKind::Narration;
        const bool victory = !e.isChat && e.kind == NoticeKind::Victory;
        eng::Font& font = narration ? m_fonts.narration : victory ? m_fonts.uiBold : (compact ? m_fonts.handSmall : m_fonts.hand);
        if (e.wrapWidth != width) {
            e.lines = font.wrap(e.isChat ? chatPrefix(e) + e.text : e.text, width);
            e.wrapWidth = width;
        }
        const float lh = font.lineHeight();
        const float h = static_cast<float>(e.lines.size()) * lh + (e.isChat ? 3.0f : 6.0f);
        total += h;
        y -= h;
        if (y > logArea.bottom() || y + h < logArea.y) continue;

        // Im Spiel verblassen ältere Einträge, solange der Chat nicht aktiv ist
        float alpha = 1.0f;
        if (compact && !focused && m_chatScroll < 1.0f) alpha = eng::clamp01(1.4f - static_cast<float>(m_time - e.time) / 30.0f);
        if (alpha <= 0.02f) continue;

        float ly = y + (e.isChat ? 1.0f : 3.0f);
        if (e.isChat) {
            Color nameColor = theme::playerColor(e.from);
            Color textColor = theme::text;
            if (e.channel == ChatChannel::Wolves) {
                nameColor = theme::blood;
                textColor = Color::hex(0xF0C8C3);
            } else if (e.channel == ChatChannel::Dead) {
                nameColor = theme::ghost;
                textColor = Color::hex(0xB8C3D8);
            } else if (e.channel == ChatChannel::Family) {
                nameColor = theme::roleColor(Role::Sister);
                textColor = Color::hex(0xF2DCE9);
            }
            const std::string prefix = chatPrefix(e);
            for (size_t i = 0; i < e.lines.size(); ++i) {
                const std::string& line = e.lines[i];
                if (i == 0 && line.compare(0, prefix.size(), prefix) == 0) {
                    r.text(font, {logArea.x, ly}, prefix, nameColor.fade(alpha));
                    r.text(font, {logArea.x + font.measure(prefix), ly}, std::string_view(line).substr(prefix.size()), textColor.fade(alpha));
                } else {
                    r.text(font, {logArea.x, ly}, line, (i == 0 ? nameColor : textColor).fade(alpha));
                }
                ly += lh;
            }
        } else {
            const Color c = theme::noticeColor(e.kind).fade(alpha);
            if (e.kind == NoticeKind::Death || e.kind == NoticeKind::Private || e.kind == NoticeKind::Victory)
                r.roundRect({logArea.x - 6.0f, y + 1.0f, 3.0f, h - 2.0f}, 1.5f, c.withAlpha(0.8f * alpha));
            for (const std::string& line : e.lines) {
                r.text(font, {logArea.x, ly}, line, c);
                ly += lh;
            }
        }
    }
    r.popClip();

    if (m_ui.hovered(logArea) && in.wheel != 0.0f) m_chatScroll += in.wheel * 40.0f;
    m_chatScroll = std::clamp(m_chatScroll, 0.0f, std::max(0.0f, total - logArea.h));
    if (m_chatScroll > 4.0f) {
        const Rect jump{logArea.center().x - 70.0f, logArea.bottom() - 28.0f, 140.0f, 24.0f};
        if (m_ui.button("chat/jump", jump, "↓ Neueste", eng::ui::ButtonKind::Normal)) m_chatScroll = 0.0f;
    }

    std::string placeholder;
    switch (channel) {
    case ChatChannel::Global: placeholder = v.inGame() ? "An alle …" : "Nachricht an alle …"; break;
    case ChatChannel::Nearby: placeholder = "Sprechen (Hörweite 24 m) …"; break;
    case ChatChannel::Wolves: placeholder = "Flüstere deinem Rudel zu …"; break;
    case ChatChannel::Dead: placeholder = "Nur die Toten hören dich …"; break;
    case ChatChannel::Family: placeholder = "Nur deine Geschwister hören dich …"; break;
    }
    if (compact && !focused) placeholder = "Enter: " + placeholder;
    const bool submitted = m_ui.textInput("chat", input, m_chatInput, placeholder, proto::kMaxChatLength, true);
    if (submitted && m_time != m_chatFocusTime) {
        if (!m_chatInput.empty()) {
            m_net.sendChat(m_chatInput, channel == ChatChannel::Wolves || channel == ChatChannel::Family ||
                                            (alive && channel == ChatChannel::Dead));
            m_chatInput.clear();
            m_chatScroll = 0.0f;
        }
        if (compact) m_ui.clearFocus(); // zurück ins Spiel
    }
    if (!compact && !m_ui.anyFocused() && in.pressed(eng::Key::Enter)) m_ui.focus("chat");
}

} // namespace ww
