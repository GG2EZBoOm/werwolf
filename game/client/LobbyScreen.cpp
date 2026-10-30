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
    std::vector<Role> order = {Role::Werewolf};
    for (int i = 0; i < static_cast<int>(Role::Count); ++i)
        if (isOptionalRole(static_cast<Role>(i))) order.push_back(static_cast<Role>(i));
    order.push_back(Role::Villager);
    std::string out;
    for (Role r : order) {
        auto it = counts.find(r);
        if (it == counts.end()) continue;
        if (!out.empty()) out += ", ";
        if (it->second > 1) out += std::to_string(it->second) + "× ";
        out += r == Role::Werewolf && it->second > 1   ? L("Werwölfe", "Werewolves")
               : r == Role::Villager && it->second > 1 ? L("Dorfbewohner", "Villagers")
                                                       : roleName(r);
    }
    return out;
}

} // namespace

float App::drawAddresses(Rect area) {
    using namespace palette;
    eng::Renderer2D& r = m_renderer;
    const HostNetwork::Status s = m_hostNet.status();
    const uint16_t port = m_net.port();
    eng::Font& f = m_fonts.paperItalic;
    const float lh = f.lineHeight() + 2.0f;
    float y = area.y;

    r.text(m_fonts.script, {area.x, y}, L("So findet man her", "How to get here"), inkFaded);
    y += m_fonts.script.lineHeight();

    // Eine Zeile pro Adresse; ein Klick kopiert sie für Mitspieler
    auto line = [&](const char* label, const std::string& address, Color color) {
        const std::string endpoint = eng::net::formatEndpoint(address, port);
        const Rect row{area.x, y, area.w, lh};
        const bool hover = m_ui.hovered(row);
        if (m_ui.clickArea(std::string("lobby/copy/") + endpoint, row)) {
            m_window.setClipboardText(endpoint);
            m_copiedText = endpoint;
            m_copiedAt = m_time;
            playSound(Sound::Click, 0.5f);
        }
        if (hover) m_ui.setCursor(eng::Cursor::Hand);
        const float labelW = 74.0f;
        r.text(f, {row.x, row.y}, label, inkFaded);
        r.text(m_fonts.paper, {row.x + labelW, row.y}, m_fonts.paper.ellipsize(endpoint, row.w - labelW), hover ? inkRed : color);
        y += lh;
    };
    if (!s.lan.empty()) line(L("Heimnetz", "Home"), s.lan.front(), inkDark);
    if (!s.publicIPv4.empty() && !s.cgnat()) line("IPv4", s.publicIPv4, inkDark);
    if (!s.ipv6.empty()) line("IPv6", s.ipv6.front(), inkDark);
    if (!s.vpn.empty()) line("VPN", s.vpn.front(), inkSoft);

    // Zustand der Freigabe im Router
    std::string state;
    Color stateInk = inkSoft;
    if (s.router == HostNetwork::Router::Searching) {
        state = L("Suche den Router für die Portfreigabe …", "Looking for the router to open the port …");
    } else if (s.cgnat()) {
        state = s.ipv6.empty() ? L("Dein Anschluss hat keine eigene IPv4-Adresse (DS-Lite). Über das Internet geht es nur per VPN.",
                                   "Your connection has no public IPv4 address (DS-Lite). Over the internet only a VPN will work.")
                               : L("Kein eigenes IPv4 (DS-Lite) – Freunde verbinden sich über IPv6.",
                                   "No public IPv4 (DS-Lite) – friends connect via IPv6.");
        stateInk = inkRed;
    } else if (s.router == HostNetwork::Router::Opened) {
        state = s.ipv6Opened ? L("Router-Freigabe für IPv4 und IPv6 aktiv.", "Router port opened for IPv4 and IPv6.")
                             : L("Router-Freigabe aktiv.", "Router port opened.");
    } else if (s.router == HostNetwork::Router::Disabled) {
        state = Lf("Für Freunde im Internet: TCP-Port {} im Router freigeben.", "For friends on the internet: forward TCP port {} in your router.", port);
    } else {
        state = Lf("Der Router hat keine automatische Freigabe erlaubt. Gib TCP-Port {} von Hand frei (IPv6: in der Router-Firewall erlauben).",
                   "The router did not allow an automatic opening. Forward TCP port {} by hand (IPv6: allow it in the router firewall).", port);
        stateInk = inkRed;
    }
    if (m_time - m_copiedAt < 2.5) {
        state = Lf("Kopiert: {}", "Copied: {}", m_copiedText);
        stateInk = inkRed;
    }
    for (const std::string& l : m_fonts.handSmall.wrap(state, area.w)) {
        r.text(m_fonts.handSmall, {area.x, y}, l, stateInk);
        y += m_fonts.handSmall.lineHeight();
    }
    if (m_discord.connected()) {
        for (const std::string& l : m_fonts.handSmall.wrap(L("Discord: Freunde mit ＋ im Chat einladen.", "Discord: invite friends with ＋ in a chat."), area.w)) {
            r.text(m_fonts.handSmall, {area.x, y}, l, inkSoft);
            y += m_fonts.handSmall.lineHeight();
        }
    }
    return y - area.y;
}

void App::drawJoinRequests(const Rect& area) {
    using namespace palette;
    eng::Renderer2D& r = m_renderer;
    float y = area.y;
    for (size_t i = 0; i < m_joinRequests.size() && i < 3;) {
        const Discord::JoinRequest req = m_joinRequests[i];
        const Rect slip{area.x, y, area.w, 74.0f};
        m_kit.beginTilt(slip, i % 2 ? 0.012f : -0.01f);
        m_kit.paper(slip, 9500u + static_cast<uint32_t>(i));
        m_kit.nail({slip.center().x, slip.y + 10.0f});
        r.text(m_fonts.hand, {slip.x + 14.0f, slip.y + 12.0f},
               m_fonts.hand.ellipsize(Lf("{} möchte über Discord beitreten", "{} wants to join via Discord", req.username), slip.w - 28.0f), inkDark);
        const float bw = (slip.w - 42.0f) * 0.5f;
        bool handled = false;
        if (m_kit.inkButton("lobby/discord/yes/" + req.userId, {slip.x + 14.0f, slip.y + 38.0f, bw, 28.0f}, L("Einlassen", "Let in"), true)) {
            m_discord.respond(req.userId, true);
            handled = true;
        }
        if (m_kit.inkButton("lobby/discord/no/" + req.userId, {slip.x + 28.0f + bw, slip.y + 38.0f, bw, 28.0f}, L("Ablehnen", "Decline"))) {
            m_discord.respond(req.userId, false);
            handled = true;
        }
        m_kit.endTilt();
        if (handled) m_joinRequests.erase(m_joinRequests.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
        y += 82.0f;
    }
}

void App::drawLobby() {
    const GameView& v = m_net.view();
    const bool host = m_net.isHost();
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const float H = r.height();
    using namespace palette;

    // Das nächtliche Dorf im Hintergrund, Kerzenlicht über den Blättern
    r.rect({0, 0, W, H}, Color::hex(0x05070F, 0.4f));
    m_kit.vignette(1.0f);
    m_kit.embers({0, H * 0.4f, W, H * 0.6f}, 16, 0.5f);

    const float margin = 24.0f;
    const float chatW = std::clamp(W * 0.27f, 300.0f, 400.0f);
    const Rect chat{W - margin - chatW, margin + 10.0f, chatW, H - 2 * margin - 70.0f};
    Rect main{margin, margin, chat.x - 2 * margin, H - 2 * margin};
    const Rect footer = main.takeBottom(56.0f);
    main.takeBottom(10.0f);
    const float playersW = std::clamp(main.w * 0.34f, 240.0f, 320.0f);
    const Rect register_ = main.takeLeft(playersW);
    main.takeLeft(18.0f);
    const Rect laws = main;

    m_kit.light({W * 0.38f, 20.0f}, 1.1f);
    m_kit.candleLight({W * 0.42f, 10.0f}, 700.0f, 0.9f);

    // --- Dorfregister: wer ist da? ---------------------------------------------------------
    m_kit.beginTilt(register_, -0.012f);
    m_kit.paper(register_, 9100u);
    m_kit.nail({register_.center().x, register_.y + 14.0f});
    Rect list = register_.shrink(28.0f, 30.0f);
    r.text(m_fonts.scriptLarge, {list.x, list.y}, L("Dorfregister", "Village register"), inkRed);
    list.takeTop(m_fonts.scriptLarge.lineHeight());
    const std::string sub = m_hosting ? Lf("Dein Dorf · Tor {}", "Your village · gate {}", m_net.port())
                                      : Lf("Zu Gast in {}", "Visiting {}", m_net.address());
    r.text(m_fonts.paperItalic, {list.x, list.y}, m_fonts.paperItalic.ellipsize(sub, list.w), inkSoft);
    list.takeTop(24.0f);
    m_kit.inkLine({list.x, list.y}, {list.right(), list.y}, 1.0f, inkFaded.withAlpha(0.6f), 1.0f, 3);
    list.takeTop(10.0f);

    const Rect botRow = host ? list.takeBottom(42.0f) : Rect{};
    if (host) list.takeBottom(6.0f);
    // Wie Freunde ins Dorf finden (nur für den Host, der den Server betreibt)
    if (m_hosting) {
        const float wanted = std::min(list.h * 0.45f, 190.0f);
        Rect addr = list.takeBottom(wanted);
        list.takeBottom(6.0f);
        m_kit.inkLine({addr.x, addr.y - 3.0f}, {addr.right(), addr.y - 3.0f}, 1.0f, inkFaded.withAlpha(0.5f), 1.0f, 7);
        drawAddresses(addr.shrink(0.0f, 4.0f));
    }
    if (static_cast<int>(v.players.size()) < kMinPlayers) {
        r.text(m_fonts.paperItalic, {list.x, list.bottom() - 18.0f},
               host ? Lf("Mindestens {} Bewohner nötig – lade Fremde ein!", "At least {} villagers needed – invite strangers!", kMinPlayers)
                    : Lf("Mindestens {} Bewohner nötig.", "At least {} villagers needed.", kMinPlayers),
               inkRed);
        list.takeBottom(24.0f);
    }
    // ab vielen Bewohnern zweispaltig, damit die Namen lesbar bleiben
    const int count = static_cast<int>(v.players.size());
    const int columns = list.h / static_cast<float>(std::max(1, count)) < 26.0f ? 2 : 1;
    const int perColumn = (count + columns - 1) / columns;
    const float columnGap = 18.0f;
    const float columnW = (list.w - columnGap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const float rowH = std::min(40.0f, list.h / std::max<float>(1.0f, static_cast<float>(perColumn)));
    int index = 0;
    for (const PlayerView& p : v.players) {
        const int column = index / std::max(1, perColumn), line = index % std::max(1, perColumn);
        const Rect row{list.x + static_cast<float>(column) * (columnW + columnGap), list.y + static_cast<float>(line) * rowH, columnW, rowH};
        const bool me = p.id == v.you;
        const float appear = eng::smoothstep(0.0f, 1.0f, m_kit.animFrom(std::format("lobby/row/{}/{}", p.id, p.name), 0.0f, 1.0f, 3.0f));
        // Name, als hätte man sich eben eingetragen
        r.text(m_fonts.paper, {row.x, row.center().y - m_fonts.paper.lineHeight() * 0.5f}, std::format("{}.", ++index), inkFaded);
        const Color nameInk = eng::lerp(theme::playerColor(p.id), inkDark, 0.72f);
        const std::string name = m_fonts.hand.ellipsize(p.name, row.w - 110.0f);
        m_kit.writeText(m_fonts.hand, {row.x + 28.0f, row.center().y - m_fonts.hand.lineHeight() * 0.5f}, name, nameInk, appear);
        if (me) m_kit.swash({row.x + 28.0f, row.center().y + 9.0f}, m_fonts.hand.measure(name), inkRed.withAlpha(0.6f), appear, 5);
        std::string tag = p.id == v.host ? L("Vorsteher", "Headman") : (p.bot ? L("Fremder", "Stranger") : "");
        float tagRight = row.right();
        if (host && !me) {
            const Rect kick{tagRight - 24.0f, row.center().y - 12.0f, 24.0f, 24.0f};
            const bool hov = m_ui.hovered(kick);
            if (m_ui.clickArea(std::format("lobby/kick/{}", p.id), kick)) m_net.sendKick(p.id);
            if (hov) m_ui.setCursor(eng::Cursor::Hand);
            r.textCentered(m_fonts.scriptBold, kick, "✗", hov ? inkRed : inkFaded.withAlpha(0.7f));
            tagRight -= 30.0f;
        }
        if (!tag.empty())
            r.textRight(m_fonts.paperItalic, {tagRight, row.center().y - m_fonts.paperItalic.lineHeight() * 0.5f}, tag,
                        p.id == v.host ? inkRed : inkFaded);
    }
    if (host) {
        const bool full = static_cast<int>(v.players.size()) >= kMaxPlayers;
        if (m_kit.inkButton("lobby/addbot", botRow, L("Fremde einladen", "Invite strangers"), false, !full)) m_net.sendAddBot();
    }
    m_kit.endTilt();

    // --- Gesetze der Nacht -------------------------------------------------------------------
    m_kit.beginTilt(laws, 0.006f);
    m_kit.paper(laws, 9200u);
    m_kit.nail({laws.x + 20.0f, laws.y + 16.0f});
    m_kit.nail({laws.right() - 20.0f, laws.y + 16.0f});
    Rect s = laws.shrink(34.0f, 30.0f);
    Settings edit = v.settings;
    bool changed = false;
    const int playerCount = static_cast<int>(v.players.size());
    r.text(m_fonts.scriptLarge, {s.x, s.y}, L("Gesetze der Nacht", "Laws of the night"), inkRed);
    if (!host) r.textRight(m_fonts.paperItalic, {s.right(), s.y + 14.0f}, L("nur der Vorsteher schreibt hier", "only the headman writes here"), inkFaded);
    s.takeTop(m_fonts.scriptLarge.lineHeight() + 2.0f);

    std::string hoveredText;
    bool hoveredSomething = false;
    {
        Rect row = s.takeTop(32.0f);
        r.text(m_fonts.script, {row.x, row.center().y - m_fonts.script.lineHeight() * 0.5f}, L("Werwölfe", "Werewolves"), inkSoft);
        std::vector<std::string> options = {Lf("so viele, wie es braucht ({})", "as many as needed ({})", autoWolfCount(std::max(playerCount, kMinPlayers)))};
        for (int i = 1; i <= 10; ++i)
            options.push_back(i == 1 ? std::string(L("1 Werwolf", "1 werewolf")) : Lf("{} Werwölfe", "{} werewolves", i));
        int wolves = edit.wolves;
        const Rect ctrl{row.x + 110.0f, row.y, std::min(320.0f, row.w - 110.0f), row.h};
        if (host ? m_kit.inkChoice("lobby/wolves", ctrl, options, wolves) : (r.text(m_fonts.paper, {ctrl.x + 30.0f, ctrl.y + 6.0f}, options[static_cast<size_t>(wolves)], inkDark), false)) {
            edit.wolves = static_cast<uint8_t>(wolves);
            changed = true;
        }
        s.takeTop(4.0f);
        Rect checks = s.takeTop(28.0f);
        bool reveal = edit.revealRoles, captain = edit.captain;
        if (m_kit.inkCheck("lobby/reveal", checks.takeLeft(checks.w * 0.5f), L("Rollen der Toten aufdecken", "Reveal the roles of the dead"), reveal, host)) {
            edit.revealRoles = reveal;
            changed = true;
        }
        if (m_kit.inkCheck("lobby/captain", checks, L("Hauptmannwahl", "Captain election"), captain, host)) {
            edit.captain = captain;
            changed = true;
        }
        if (m_ui.hovered(checks)) {
            hoveredText = L("Hauptmannwahl: Am ersten Abend wählt das Dorf einen Hauptmann. Seine Stimme zählt doppelt, und stirbt er, "
                            "bestimmt er seinen Nachfolger.",
                            "Captain election: on the first evening the village elects a captain. Their vote counts double, and if "
                            "they die, they name their successor.");
            hoveredSomething = true;
        }
        s.takeTop(8.0f);
    }

    // Rollen: jede ein kleiner Eintrag, gewählte tragen ein rotes Siegel
    const int cols = std::clamp(static_cast<int>(s.w / 118.0f), 2, 6);
    const float chipH = 24.0f;
    const float chipW = s.w / static_cast<float>(cols);
    auto roleGrid = [&](const char* title, auto belongs) {
        std::vector<Role> group;
        for (int i = 0; i < static_cast<int>(Role::Count); ++i) {
            const Role role = static_cast<Role>(i);
            if (isOptionalRole(role) && belongs(role)) group.push_back(role);
        }
        r.text(m_fonts.script, {s.x, s.y}, title, inkFaded);
        s.takeTop(m_fonts.script.lineHeight() + 2.0f);
        for (size_t i = 0; i < group.size(); ++i) {
            const Role role = group[i];
            const int col = static_cast<int>(i) % cols;
            if (col == 0 && i > 0) s.takeTop(chipH);
            const Rect chip{s.x + static_cast<float>(col) * chipW, s.y, chipW - 4.0f, chipH};
            const bool on = edit.has(role);
            if (m_ui.clickArea(std::format("lobby/role/{}", static_cast<int>(role)), chip, host)) {
                edit.set(role, !on);
                changed = true;
                m_kit.burst(m_window.input().mouse, on ? inkFaded : wax, 10);
            }
            const bool hover = m_ui.hovered(chip);
            if (hover && host) m_ui.setCursor(eng::Cursor::Hand);
            const float h = m_kit.anim(std::format("lobby/role/{}", static_cast<int>(role)), on ? 1.0f : 0.0f, 10.0f);
            if (h > 0.02f) m_kit.seal({chip.x + 9.0f, chip.center().y}, 7.0f * h, wax, "", 1.0f, static_cast<uint32_t>(role));
            else r.circleOutline({chip.x + 9.0f, chip.center().y}, 5.0f, 1.0f, inkFaded.withAlpha(0.7f));
            const std::string name = m_fonts.paperItalic.ellipsize(roleName(role), chip.w - 26.0f);
            const Color c = eng::lerp(inkFaded, inkDark, 0.3f + 0.7f * h);
            r.text(m_fonts.paperItalic, {chip.x + 21.0f, chip.center().y - m_fonts.paperItalic.lineHeight() * 0.5f}, name,
                   hover ? inkRed : c);
            if (hover) {
                hoveredText = std::string(roleIcon(role)) + "  " + roleName(role) + ": " + roleDescription(role);
                hoveredSomething = true;
            }
        }
        s.takeTop(chipH + 8.0f);
    };
    roleGrid(L("Das Dorf", "The village"), [](Role role) { return roleFaction(role) == Faction::Village; });
    roleGrid(L("Wölfe, Vampire und Einzelgänger", "Wolves, vampires and loners"), [](Role role) { return roleFaction(role) != Faction::Village; });

    // Ablauf der Tage
    struct TimeStep {
        const char* label;
        const char* id;
        uint16_t* value;
        std::vector<int> presets;
    };
    const TimeStep steps[] = {{L("Tag", "Day"), "lobby/day", &edit.daySeconds, {60, 90, 120, 150, 180, 240, 300, 420, 600, 900}},
                              {L("Versammlung", "Gathering"), "lobby/discussion", &edit.discussionSeconds, {30, 45, 60, 90, 120, 180, 300, 600}},
                              {L("Abstimmung", "Vote"), "lobby/vote", &edit.voteSeconds, {15, 20, 30, 40, 60, 90, 120, 180}},
                              {L("Nacht", "Night"), "lobby/night", &edit.nightSeconds, {45, 60, 80, 100, 120, 150, 180, 240, 300, 600}}};
    const float timeW = (s.w - 30.0f) / 4.0f;
    for (size_t i = 0; i < 4; ++i) {
        const TimeStep& st = steps[i];
        const float x = s.x + static_cast<float>(i) * (timeW + 10.0f);
        r.text(m_fonts.paperItalic, {x, s.y + 4.0f}, st.label, inkFaded);
        // Auswahl aus festen Stufen; der aktuelle Wert zeigt auf die nächstliegende
        std::vector<std::string> values;
        int current = 0;
        for (size_t k = 0; k < st.presets.size(); ++k) {
            values.push_back(formatSeconds(st.presets[k]));
            if (std::abs(st.presets[k] - static_cast<int>(*st.value)) < std::abs(st.presets[static_cast<size_t>(current)] - static_cast<int>(*st.value)))
                current = static_cast<int>(k);
        }
        const Rect ctrl{x, s.y + m_fonts.script.lineHeight(), timeW, 28.0f};
        if (host) {
            if (m_kit.inkChoice(st.id, ctrl, values, current)) {
                *st.value = static_cast<uint16_t>(st.presets[static_cast<size_t>(current)]);
                changed = true;
            }
        } else {
            r.text(m_fonts.paper, {ctrl.x + 26.0f, ctrl.y + 5.0f}, formatSeconds(*st.value), inkDark);
        }
    }
    s.takeTop(m_fonts.script.lineHeight() + 36.0f);
    if (changed && host) m_net.sendSettings(edit);

    // Wer wird gebraucht? Oder was die Maus gerade beschreibt
    Text error;
    const std::vector<Role> roles = buildRoleList(v.settings, playerCount, &error);
    const std::string summaryText = hoveredSomething ? hoveredText
                                    : roles.empty()  ? error.str()
                                                     : Lf("Bei {} Bewohnern: {}.", "With {} villagers: {}.", playerCount, roleSummary(roles));
    float sy = s.y;
    for (const std::string& line : m_fonts.paperItalic.wrap(summaryText, s.w)) {
        if (sy > s.bottom() - 4.0f) break;
        r.text(m_fonts.paperItalic, {s.x, sy}, line, roles.empty() && !hoveredSomething ? inkRed : inkSoft);
        sy += m_fonts.paperItalic.lineHeight();
    }
    m_kit.endTilt();

    // --- Schiefertafel mit dem Gespräch des Dorfes -----------------------------------------------
    drawChat(chat, false);
    // Discord-Anfragen liegen obenauf
    if (!m_joinRequests.empty()) drawJoinRequests({chat.x + 16.0f, chat.y + 16.0f, chat.w - 32.0f, chat.h * 0.5f});

    // --- Unten: gehen oder die Nacht beginnen lassen (auf eigenen Zetteln) ----------------------
    const Rect leaveSlip{footer.x, footer.y + 4.0f, 210.0f, 50.0f};
    m_kit.beginTilt(leaveSlip, 0.02f);
    m_kit.paper(leaveSlip, 9300u);
    if (m_kit.inkButton("lobby/leave", leaveSlip.shrink(14.0f, 7.0f), m_hosting ? L("Dorf auflösen", "Disband village") : L("Gehen", "Leave"))) leaveSession();
    m_kit.endTilt();
    const Rect start{chat.x, footer.y + 2.0f, chat.w, 56.0f};
    if (host) {
        m_kit.beginTilt(start, -0.015f);
        m_kit.paper(start, 9400u);
        m_kit.endTilt();
    }
    if (host) {
        const float pulse = roles.empty() ? 0.0f : 0.5f + 0.5f * std::sin(static_cast<float>(m_time) * 2.2f);
        if (!roles.empty()) m_kit.candleLight({start.x + 30.0f, start.center().y}, 120.0f, 0.6f + 0.4f * pulse, 5.0f);
        // Das große Siegel: die Nacht beginnt
        if (m_kit.inkButton("lobby/start", {start.x + 12.0f, start.y + 6.0f, start.w - 24.0f, 44.0f}, L("Die Nacht beginnen lassen", "Let the night begin"), true,
                            !roles.empty()))
            m_net.sendStartGame();
    } else {
        const float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(m_time) * 2.0f);
        r.text(m_fonts.hand, {start.x + 10.0f, start.y + 14.0f}, L("Warte auf den Dorfvorsteher …", "Waiting for the headman …"), chalk.withAlpha(pulse));
    }
}

} // namespace ww
