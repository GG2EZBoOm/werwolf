#include "client/App.h"
#include "client/Theme.h"

#include <cmath>
#include <format>
#include <map>

namespace ww {

using eng::Color;
using eng::Rect;
using eng::Vec2;
using eng::ui::ButtonKind;

namespace {

std::string formatSeconds(int seconds) {
    if (seconds >= 60) return std::format("{}:{:02}", seconds / 60, seconds % 60);
    return std::format("{} s", seconds);
}

std::string roleSummary(const std::vector<Role>& roles) {
    std::map<Role, int> counts;
    for (Role r : roles) ++counts[r];
    const Role order[] = {Role::Werewolf, Role::Seer,  Role::Witch,    Role::Hunter,
                          Role::Cupid,    Role::Protector, Role::Villager};
    std::string out;
    for (Role r : order) {
        auto it = counts.find(r);
        if (it == counts.end()) continue;
        if (!out.empty()) out += ", ";
        if (it->second > 1) out += std::to_string(it->second) + "× ";
        out += r == Role::Werewolf && it->second > 1 ? "Werwölfe" : roleName(r);
    }
    return out;
}

} // namespace

void App::drawLobby() {
    const GameView& v = m_net.view();
    const bool host = m_net.isHost();
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const float H = r.height();

    // Das 3D-Dorf läuft im Hintergrund; etwas abdunkeln, damit die Oberfläche lesbar bleibt
    r.rect({0, 0, W, H}, Color::hex(0x05070F, 0.35f));

    const float margin = 20.0f;
    const float chatW = std::clamp(W * 0.3f, 320.0f, 420.0f);
    const Rect chat{W - margin - chatW, margin, chatW, H - 2 * margin};
    drawChat(chat, false);
    Rect main{margin, margin, chat.x - 2 * margin, H - 2 * margin};

    // Kopfzeile
    Rect header = main.takeTop(78.0f);
    r.text(m_fonts.heading, {header.x + 4.0f, header.y}, "Dorfplatz", theme::goldLight);
    std::string sub;
    if (m_hosting) {
        std::string ips;
        for (size_t i = 0; i < m_localIps.size() && i < 3; ++i) ips += (i ? "  ·  " : "") + m_localIps[i];
        sub = ips.empty() ? std::format("Du hostest auf Port {}", m_net.port())
                          : std::format("Mitspieler verbinden sich mit  {}   (Port {})", ips, m_net.port());
    } else {
        sub = std::format("Verbunden mit {}:{}", m_net.host(), m_net.port());
    }
    r.text(m_fonts.small, {header.x + 6.0f, header.y + 46.0f}, sub, theme::muted);

    Rect footer = main.takeBottom(50.0f);
    main.takeBottom(14.0f);

    // --- Spielerliste ---------------------------------------------------------
    const float playersW = std::clamp(main.w * 0.42f, 250.0f, 360.0f);
    Rect players = main.takeLeft(playersW);
    main.takeLeft(16.0f);
    Rect settingsRect = main;

    drawPanel(players);
    Rect list = players.shrink(16.0f, 14.0f);
    r.text(m_fonts.uiBold, {list.x, list.y}, "Bewohner", theme::text);
    r.textRight(m_fonts.small, {list.right(), list.y + 2.0f},
                std::format("{} / {}", v.players.size(), kMaxPlayers), theme::muted);
    list.takeTop(30.0f);
    if (static_cast<int>(v.players.size()) < kMinPlayers) {
        const std::string hint = std::format("Mindestens {} Spieler nötig{}", kMinPlayers,
                                             host ? " – füge Bots hinzu!" : ".");
        r.text(m_fonts.small, {list.x, list.y}, hint, theme::gold);
        list.takeTop(22.0f);
    }

    Rect botRow = list.takeBottom(host ? 38.0f : 0.0f);
    list.takeBottom(host ? 10.0f : 0.0f);
    const float rowH = std::min(46.0f, list.h / std::max<float>(1.0f, static_cast<float>(v.players.size())));
    float y = list.y;
    for (const PlayerView& p : v.players) {
        const Rect row{list.x, y, list.w, rowH - 4.0f};
        const bool me = p.id == v.you;
        if (me) r.roundRect(row, 8.0f, Color::hex(0xFFFFFF, 0.05f));
        const float avatarR = std::min(16.0f, row.h * 0.42f);
        const Vec2 avatar{row.x + 8.0f + avatarR, row.center().y};
        r.circle(avatar, avatarR, theme::playerColor(p.id));
        r.textCentered(m_fonts.smallBold, {avatar.x - avatarR, avatar.y - avatarR, avatarR * 2, avatarR * 2},
                       theme::initial(p.name), Color::hex(0x1B1406));

        const float nameX = avatar.x + avatarR + 12.0f;
        const float tagSpace = host && !me ? 140.0f : 110.0f;
        const std::string shown = m_fonts.ui.ellipsize(p.name + (me ? " (Du)" : ""), row.right() - nameX - tagSpace);
        r.textCentered(m_fonts.ui, {nameX, row.y, m_fonts.ui.measure(shown), row.h}, shown, theme::text);

        float tagRight = row.right() - 6.0f;
        if (host && !me) {
            const Rect kick{tagRight - 28.0f, row.center().y - 14.0f, 28.0f, 28.0f};
            if (m_ui.button(std::format("lobby/kick/{}", p.id), kick, "✕", ButtonKind::Ghost)) m_net.sendKick(p.id);
            tagRight -= 36.0f;
        }
        std::string tag;
        Color tagColor = theme::muted;
        if (p.id == v.host) {
            tag = "♛ Spielleitung";
            tagColor = theme::gold;
        } else if (p.bot) {
            tag = "Bot";
        }
        if (!tag.empty()) {
            const float tw = m_fonts.small.measure(tag);
            r.textCentered(m_fonts.small, {tagRight - tw, row.y, tw, row.h}, tag, tagColor);
        }
        y += rowH;
    }
    if (host) {
        const bool full = static_cast<int>(v.players.size()) >= kMaxPlayers;
        if (m_ui.button("lobby/addbot", botRow, "+ Bot hinzufügen", ButtonKind::Normal, !full)) m_net.sendAddBot();
    }

    // --- Einstellungen ----------------------------------------------------------
    drawPanel(settingsRect);
    Rect s = settingsRect.shrink(18.0f, 14.0f);
    Settings edit = v.settings;
    bool changed = false;
    const int playerCount = static_cast<int>(v.players.size());

    r.text(m_fonts.uiBold, {s.x, s.y}, "Spielregeln", theme::text);
    if (!host) r.textRight(m_fonts.small, {s.right(), s.y + 2.0f}, "nur die Spielleitung kann ändern", theme::faint);
    s.takeTop(34.0f);

    const bool twoColumns = s.w >= 420.0f;
    Rect colA = s;
    Rect colB = s;
    if (twoColumns) {
        colA = s;
        colA.w = (s.w - 24.0f) * 0.5f;
        colB = colA.offset(colA.w + 24.0f, 0.0f);
    }

    // Rollen
    Rect a = colA;
    r.text(m_fonts.smallBold, {a.x, a.y}, "WERWÖLFE", theme::muted);
    a.takeTop(19.0f);
    const std::string wolvesText = edit.wolves == 0
                                       ? std::format("Auto ({})", autoWolfCount(std::max(playerCount, kMinPlayers)))
                                       : std::to_string(edit.wolves);
    const int dw = m_ui.stepper("lobby/wolves", a.takeTop(34.0f), wolvesText, host);
    if (dw != 0) {
        edit.wolves = static_cast<uint8_t>(std::clamp(static_cast<int>(edit.wolves) + dw, 0, 6));
        changed = true;
    }
    a.takeTop(14.0f);
    r.text(m_fonts.smallBold, {a.x, a.y}, "SONDERROLLEN", theme::muted);
    a.takeTop(22.0f);

    struct RoleToggle {
        Role role;
        bool* value;
    };
    const RoleToggle toggles[] = {{Role::Seer, &edit.seer},
                                  {Role::Witch, &edit.witch},
                                  {Role::Hunter, &edit.hunter},
                                  {Role::Cupid, &edit.cupid},
                                  {Role::Protector, &edit.protector}};
    const char* hoveredDescription = nullptr;
    for (const RoleToggle& t : toggles) {
        const Rect row = a.takeTop(30.0f);
        const std::string label = std::string(roleName(t.role));
        if (m_ui.checkbox(std::format("lobby/role/{}", static_cast<int>(t.role)), row, label, *t.value, host))
            changed = true;
        r.textCentered(m_fonts.icon, {row.right() - 24.0f, row.y, 24.0f, row.h}, roleIcon(t.role),
                       theme::roleColor(t.role));
        if (m_ui.hovered(row)) hoveredDescription = roleDescription(t.role);
    }

    // Ablauf
    Rect b = twoColumns ? colB : Rect{a.x, a.y + 12.0f, a.w, a.h - 12.0f};
    struct TimeStep {
        const char* label;
        const char* id;
        uint16_t* value;
        int step;
        int lo;
        int hi;
    };
    const TimeStep steps[] = {{"TAG (ERKUNDEN)", "lobby/day", &edit.daySeconds, 30, 30, 900},
                              {"VERSAMMLUNG", "lobby/discussion", &edit.discussionSeconds, 15, 20, 600},
                              {"ABSTIMMUNG", "lobby/vote", &edit.voteSeconds, 15, 15, 180},
                              {"NACHT (JAGD)", "lobby/night", &edit.nightSeconds, 15, 30, 600}};
    for (const TimeStep& st : steps) {
        r.text(m_fonts.smallBold, {b.x, b.y}, st.label, theme::muted);
        b.takeTop(18.0f);
        const int d = m_ui.stepper(st.id, b.takeTop(32.0f), formatSeconds(*st.value), host);
        if (d != 0) {
            *st.value = static_cast<uint16_t>(std::clamp(static_cast<int>(*st.value) + d * st.step, st.lo, st.hi));
            changed = true;
        }
        b.takeTop(7.0f);
    }
    if (m_ui.checkbox("lobby/reveal", b.takeTop(30.0f), "Rollen Toter aufdecken", edit.revealRoles, host)) changed = true;

    if (changed && host) m_net.sendSettings(edit);

    // Zusammenfassung der Rollenverteilung
    const float summaryY = std::max(a.y, b.y) + 16.0f;
    Rect summary{s.x, summaryY, s.w, settingsRect.bottom() - summaryY - 14.0f};
    std::string error;
    const std::vector<Role> roles = buildRoleList(v.settings, playerCount, &error);
    const std::string summaryText =
        hoveredDescription ? std::string(hoveredDescription)
        : roles.empty()    ? error
                           : std::format("Bei {} Spielern: {}.", playerCount, roleSummary(roles));
    const Color summaryColor = hoveredDescription ? theme::text : roles.empty() ? Color::hex(0xF0A35E) : theme::muted;
    r.pushClip(summary);
    float sy = summary.y;
    for (const std::string& line : m_fonts.small.wrap(summaryText, summary.w)) {
        r.text(m_fonts.small, {summary.x, sy}, line, summaryColor);
        sy += m_fonts.small.lineHeight();
    }
    r.popClip();

    // --- Fußzeile -------------------------------------------------------------
    const Rect leave = footer.takeLeft(170.0f);
    if (m_ui.button("lobby/leave", leave, m_hosting ? "Dorf auflösen" : "Lobby verlassen", ButtonKind::Ghost))
        leaveSession();
    const Rect start = footer.takeRight(220.0f);
    if (host) {
        if (m_ui.button("lobby/start", start, "Spiel starten", ButtonKind::Primary, !roles.empty())) m_net.sendStartGame();
    } else {
        const float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(m_time) * 2.0f);
        r.textCentered(m_fonts.ui, start, "Warte auf die Spielleitung …", theme::muted.fade(pulse));
    }
}

} // namespace ww
