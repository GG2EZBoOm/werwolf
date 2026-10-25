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
    default: return e.name + ": ";
    }
}

} // namespace

void App::drawChat(const Rect& area) {
    eng::Renderer2D& r = m_renderer;
    eng::Input& in = m_window.input();
    const GameView& v = m_net.view();
    drawPanel(area);
    Rect inner = area.shrink(14.0f, 12.0f);

    // Wer darf gerade wo sprechen?
    const PlayerView* me = v.me();
    const std::optional<ChatChannel> channel = speakChannel(v.phase, me ? me->alive : true, v.yourRole);

    Rect header = inner.takeTop(28.0f);
    r.text(m_fonts.uiBold, {header.x, header.y + 2.0f}, v.inGame() ? "Dorfgeflüster" : "Chat", theme::text);
    std::string channelLabel = "Nachts herrscht Stille";
    Color channelColor = theme::faint;
    if (channel) {
        switch (*channel) {
        case ChatChannel::Public:
            channelLabel = "An alle";
            channelColor = theme::muted;
            break;
        case ChatChannel::Wolves:
            channelLabel = "☾ Rudel-Chat";
            channelColor = theme::blood;
            break;
        case ChatChannel::Dead:
            channelLabel = "† Geisterchat";
            channelColor = theme::ghost;
            break;
        }
    }
    r.textRight(m_fonts.small, {header.right(), header.y + 5.0f}, channelLabel, channelColor);
    inner.takeTop(6.0f);
    r.rect({inner.x, inner.y, inner.w, 1.0f}, theme::panelBorder);
    inner.takeTop(8.0f);

    const Rect input = inner.takeBottom(42.0f);
    inner.takeBottom(10.0f);
    const Rect logArea = inner;

    // Verlauf von unten nach oben zeichnen
    std::deque<LogEntry>& log = m_net.log();
    const float width = logArea.w;
    float total = 0.0f;
    float y = logArea.bottom() + m_chatScroll;
    r.pushClip(logArea);
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        LogEntry& e = *it;
        const bool narration = !e.isChat && e.kind == NoticeKind::Narration;
        const bool victory = !e.isChat && e.kind == NoticeKind::Victory;
        eng::Font& font = narration ? m_fonts.narration : victory ? m_fonts.uiBold : m_fonts.ui;
        if (e.wrapWidth != width) {
            e.lines = font.wrap(e.isChat ? chatPrefix(e) + e.text : e.text, width);
            e.wrapWidth = width;
        }
        const float lh = font.lineHeight();
        const float h = static_cast<float>(e.lines.size()) * lh + (e.isChat ? 4.0f : 8.0f);
        total += h;
        y -= h;
        if (y > logArea.bottom() || y + h < logArea.y) continue;

        float ly = y + (e.isChat ? 2.0f : 4.0f);
        if (e.isChat) {
            Color nameColor = theme::playerColor(e.from);
            Color textColor = theme::text;
            if (e.channel == ChatChannel::Wolves) {
                nameColor = theme::blood;
                textColor = Color::hex(0xF0C8C3);
            } else if (e.channel == ChatChannel::Dead) {
                nameColor = theme::ghost;
                textColor = Color::hex(0xB8C3D8);
            }
            const std::string prefix = chatPrefix(e);
            for (size_t i = 0; i < e.lines.size(); ++i) {
                const std::string& line = e.lines[i];
                if (i == 0 && line.compare(0, prefix.size(), prefix) == 0) {
                    r.text(font, {logArea.x, ly}, prefix, nameColor);
                    r.text(font, {logArea.x + font.measure(prefix), ly}, std::string_view(line).substr(prefix.size()),
                           textColor);
                } else {
                    r.text(font, {logArea.x, ly}, line, i == 0 ? nameColor : textColor);
                }
                ly += lh;
            }
        } else {
            const Color c = theme::noticeColor(e.kind);
            if (e.kind == NoticeKind::Death || e.kind == NoticeKind::Private || e.kind == NoticeKind::Victory)
                r.roundRect({logArea.x - 6.0f, y + 1.0f, 3.0f, h - 2.0f}, 1.5f, c.withAlpha(0.8f));
            for (const std::string& line : e.lines) {
                r.text(font, {logArea.x, ly}, line, c);
                ly += lh;
            }
        }
    }
    r.popClip();

    // Scrollen mit dem Mausrad
    if (m_ui.hovered(logArea) && in.wheel != 0.0f) m_chatScroll += in.wheel * 48.0f;
    m_chatScroll = std::clamp(m_chatScroll, 0.0f, std::max(0.0f, total - logArea.h));
    if (m_chatScroll > 4.0f) {
        const Rect jump{logArea.center().x - 80.0f, logArea.bottom() - 30.0f, 160.0f, 26.0f};
        if (m_ui.button("chat/jump", jump, "↓ Neueste", eng::ui::ButtonKind::Normal)) m_chatScroll = 0.0f;
    }
    if (log.empty()) {
        r.textCentered(m_fonts.small, logArea, "Noch ist es still im Dorf …", theme::faint);
    }

    // Eingabezeile
    std::string placeholder = "Nachts schläft das Dorf …";
    if (channel) {
        switch (*channel) {
        case ChatChannel::Public: placeholder = "Nachricht an alle …"; break;
        case ChatChannel::Wolves: placeholder = "Flüstere deinem Rudel zu …"; break;
        case ChatChannel::Dead: placeholder = "Nur die Toten hören dich …"; break;
        }
    }
    const bool enabled = channel.has_value();
    if (m_ui.textInput("chat", input, m_chatInput, placeholder, proto::kMaxChatLength, enabled)) {
        if (!m_chatInput.empty()) {
            m_net.sendChat(m_chatInput);
            m_chatInput.clear();
            m_chatScroll = 0.0f;
        }
    }
    // Enter springt ins Chatfeld
    if (enabled && !m_ui.anyFocused() && in.pressed(eng::Key::Enter)) m_ui.focus("chat");
}

} // namespace ww
