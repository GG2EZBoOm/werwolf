#include "client/App.h"
#include "client/Theme.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace ww {

using eng::Color;
using eng::Rect;
using eng::Vec2;
using eng::ui::ButtonKind;

namespace {

bool contains(const std::vector<uint8_t>& v, uint8_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }

std::string nameOf(const GameView& v, uint8_t id) {
    const PlayerView* p = v.player(id);
    return p ? p->name : std::string("?");
}

std::string clock(float seconds) {
    const int s = static_cast<int>(std::ceil(std::max(0.0f, seconds)));
    return std::format("{}:{:02}", s / 60, s % 60);
}

} // namespace

// --- Regeln aus Sicht des Clients ----------------------------------------------

bool App::isTargetable(const GameView& v, uint8_t id) const {
    const PlayerView* p = v.player(id);
    if (!p || !p->alive) return false;
    if (v.confirmed && v.action != ActionKind::DayVote) return false;
    switch (v.action) {
    case ActionKind::WolfVote: return p->role != Role::Werewolf;
    case ActionKind::Inspect:
        if (id == v.you) return false;
        return std::none_of(v.seerResults.begin(), v.seerResults.end(),
                            [&](const SeerResult& r) { return r.player == id; });
    case ActionKind::Protect: return id != v.lastProtected;
    case ActionKind::CupidPick: return true;
    case ActionKind::WitchPotions: return id != v.you && v.poisonAvailable;
    case ActionKind::HunterShot:
    case ActionKind::DayVote: return id != v.you;
    default: return false;
    }
}

void App::clickPlayer(const GameView& v, uint8_t id) {
    if (!isTargetable(v, id)) return;
    switch (v.action) {
    case ActionKind::CupidPick: m_net.sendSelect(id); break; // Server schaltet um
    case ActionKind::WitchPotions: m_net.sendSelect(v.poisonTarget == id ? kNoPlayer : id); break;
    default: m_net.sendSelect(contains(v.selection, id) ? kNoPlayer : id); break;
    }
}

std::string App::headerTitle(const GameView& v) const {
    switch (v.phase) {
    case Phase::Night:
    case Phase::Witch: return std::format("Nacht {}", v.day);
    case Phase::Morning: return "Morgengrauen";
    case Phase::Hunter: return "Der letzte Schuss";
    case Phase::Day: return std::format("Tag {}", v.day);
    case Phase::Vote: return "Abstimmung";
    case Phase::Execution: return "Das Urteil";
    case Phase::GameOver: return "Spielende";
    default: return "";
    }
}

std::string App::actionHint(const GameView& v) const {
    const PlayerView* me = v.me();
    switch (v.action) {
    case ActionKind::WolfVote:
        return v.confirmed ? "Warte auf das restliche Rudel …"
                           : "Wählt gemeinsam ein Opfer – klicke auf einen Dorfbewohner und bestätige.";
    case ActionKind::Inspect:
        return v.confirmed ? "Deine Vision ist empfangen." : "Wähle einen Spieler, dessen wahre Rolle du sehen willst.";
    case ActionKind::Protect:
        return v.confirmed ? "Du wachst über deinen Schützling." : "Wen beschützt du heute Nacht vor den Werwölfen?";
    case ActionKind::CupidPick:
        return v.confirmed ? "Amors Pfeile sind abgeschossen."
                           : std::format("Wähle zwei Spieler, die sich verlieben sollen ({}/2).", v.selection.size());
    case ActionKind::WitchPotions: {
        std::string s = v.witchVictim != kNoPlayer ? "Die Werwölfe haben " + nameOf(v, v.witchVictim) + " gewählt."
                                                   : "Heute Nacht gibt es kein Opfer.";
        if (v.poisonAvailable)
            s += v.poisonTarget != kNoPlayer ? " Gift für " + nameOf(v, v.poisonTarget) + "."
                                             : " Klicke jemanden an, um Gift zu verabreichen.";
        return s;
    }
    case ActionKind::HunterShot: return "Du stirbst – wen reißt du mit in den Tod?";
    case ActionKind::SkipDiscussion: return "Diskutiert im Chat: Wer könnte ein Werwolf sein?";
    case ActionKind::DayVote:
        return v.selection.empty() ? "Wähle, wer hingerichtet werden soll."
                                   : "Deine Stimme: " + nameOf(v, v.selection[0]) + ". Du kannst sie noch ändern.";
    default: break;
    }
    if (me && !me->alive && v.phase != Phase::GameOver)
        return "Du bist gestorben und siehst als Geist zu. Im Geisterchat kannst du mit anderen Toten reden.";
    switch (v.phase) {
    case Phase::Night: return "Du schläfst tief und fest … hoffentlich wachst du morgen wieder auf.";
    case Phase::Witch: return "Die Hexe ist am Werk …";
    case Phase::Morning: return "Das Dorf erwacht und zählt seine Bewohner.";
    case Phase::Hunter: return "Der Jäger wählt sein letztes Ziel …";
    case Phase::Execution: return "Das Urteil wird vollstreckt.";
    default: return "";
    }
}

// --- Spieltisch ----------------------------------------------------------------

void App::drawGame() {
    const GameView& v = m_net.view();
    eng::Renderer2D& r = m_renderer;
    const float W = r.width();
    const float H = r.height();

    const float margin = 16.0f;
    const float chatW = std::clamp(W * 0.27f, 300.0f, 400.0f);
    const float leftW = std::clamp(W * 0.2f, 230.0f, 290.0f);
    const Rect chat{W - margin - chatW, margin, chatW, H - 2 * margin};
    const Rect left{margin, margin, leftW, H - 2 * margin};
    Rect center{left.right() + margin, margin, chat.x - left.right() - 2 * margin, H - 2 * margin};

    m_scenery.drawSky(r, m_night, m_time);
    m_scenery.drawHorizon(r, m_night, m_time, center.y + center.h * 0.36f);
    m_scenery.drawVignette(r, 0.45f + 0.35f * m_night);

    // Wer nachts schläft, sieht das Dorf nur verschwommen
    const PlayerView* me = v.me();
    const bool sleeping = (v.phase == Phase::Night || v.phase == Phase::Witch) && me && me->alive &&
                          v.action == ActionKind::None;

    const Rect header = center.takeTop(86.0f);
    const Rect actionBar = center.takeBottom(72.0f);
    drawVillage(center, v);
    if (sleeping) {
        r.rect({0, 0, W, H}, Color::hex(0x02030A, 0.45f));
        for (int i = 0; i < 3; ++i) {
            const float t = std::fmod(static_cast<float>(m_time) * 0.4f + static_cast<float>(i) / 3.0f, 1.0f);
            const Vec2 p = center.center() + Vec2{40.0f + t * 30.0f + static_cast<float>(i) * 8.0f, -60.0f - t * 60.0f};
            r.text(m_fonts.large, p, "z", theme::muted.fade((1.0f - t) * 0.8f));
        }
    }
    drawHeader(header, v);
    drawActionBar(actionBar, v);
    drawRoleCard(left, v);
    drawChat(chat);
    if (v.phase == Phase::GameOver) drawGameOver(center, v);
    else drawBanner(center);
}

void App::drawHeader(const Rect& area, const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const Rect box{area.center().x - std::min(area.w, 520.0f) * 0.5f, area.y, std::min(area.w, 520.0f), area.h - 8};
    drawPanel(box, 16.0f);

    const float timeLeft = m_net.timeLeft(m_time);
    const std::string title = headerTitle(v);
    r.textCentered(m_fonts.heading, {box.x, box.y + 8.0f, box.w, 40.0f}, title, theme::goldLight);

    int alive = 0;
    for (const PlayerView& p : v.players)
        if (p.alive) ++alive;
    r.text(m_fonts.small, {box.x + 18.0f, box.y + 20.0f}, std::format("{} / {} leben", alive, v.players.size()),
           theme::muted);

    if (v.phase != Phase::GameOver && v.phaseDuration > 0.0f) {
        const bool urgent = timeLeft < 10.0f;
        const float pulse = urgent ? 0.6f + 0.4f * std::sin(static_cast<float>(m_time) * 8.0f) : 1.0f;
        const Color timeColor = urgent ? theme::blood.fade(pulse) : theme::text;
        r.textRight(m_fonts.large, {box.right() - 18.0f, box.y + 14.0f}, clock(timeLeft), timeColor);

        const Rect bar{box.x + 24.0f, box.bottom() - 18.0f, box.w - 48.0f, 4.0f};
        r.roundRect(bar, 2.0f, Color::hex(0xFFFFFF, 0.08f));
        const float frac = eng::clamp01(timeLeft / v.phaseDuration);
        r.roundRect({bar.x, bar.y, bar.w * frac, bar.h}, 2.0f, urgent ? theme::blood : theme::gold);
    }
}

void App::drawRoleCard(Rect area, const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const PlayerView* me = v.me();
    const Role role = v.yourRole;
    const Color roleCol = theme::roleColor(role);

    // Rollenkarte
    const float innerW = area.w - 36.0f;
    const std::vector<std::string> desc = m_fonts.small.wrap(roleDescription(role), innerW);
    const float cardH = 176.0f + static_cast<float>(desc.size()) * m_fonts.small.lineHeight();
    Rect card = area.takeTop(cardH);
    drawPanel(card);
    r.roundRect({card.x, card.y, card.w, 5.0f}, 2.5f, roleCol.withAlpha(0.8f));
    Rect c = card.shrink(18.0f, 16.0f);
    r.text(m_fonts.smallBold, {c.x, c.y}, "DEINE ROLLE", theme::muted);

    const Vec2 iconPos{c.center().x, c.y + 60.0f};
    r.glow(iconPos, 70.0f, roleCol.withAlpha(0.18f));
    r.circle(iconPos, 34.0f, Color::hex(0x0B0F1F));
    r.circleOutline(iconPos, 34.0f, 2.0f, roleCol);
    r.textCentered(m_fonts.iconLarge, {iconPos.x - 34.0f, iconPos.y - 34.0f, 68.0f, 68.0f}, roleIcon(role), roleCol);
    r.textCentered(m_fonts.large, {c.x, c.y + 100.0f, c.w, 28.0f}, roleName(role), roleCol);

    const bool mixedLover = v.loverPartner != kNoPlayer && [&] {
        const PlayerView* partner = v.player(v.loverPartner);
        return partner && partner->role != Role::Unknown &&
               (partner->role == Role::Werewolf) != (role == Role::Werewolf);
    }();
    const Team team = mixedLover ? Team::Lovers : roleTeam(role);
    r.textCentered(m_fonts.small, {c.x, c.y + 128.0f, c.w, 18.0f}, std::string("Lager: ") + teamName(team),
                   theme::teamColor(team));
    float y = c.y + 152.0f;
    for (const std::string& line : desc) {
        r.text(m_fonts.small, {c.x, y}, line, theme::muted);
        y += m_fonts.small.lineHeight();
    }
    if (me && !me->alive) {
        r.roundRect(card, 14.0f, Color::hex(0x000000, 0.35f));
        const Rect stamp{c.x + 10.0f, iconPos.y - 20.0f, c.w - 20.0f, 40.0f};
        r.roundRect(stamp, 8.0f, Color::hex(0x5A1A16, 0.9f));
        r.textCentered(m_fonts.uiBold, stamp, "† Du bist tot", Color::hex(0xF3C9C3));
    }

    // Wissen
    struct Line {
        std::string text;
        Color color;
    };
    std::vector<Line> lines;
    if (role == Role::Werewolf) {
        std::string pack;
        for (const PlayerView& p : v.players)
            if (p.role == Role::Werewolf && p.id != v.you) pack += (pack.empty() ? "" : ", ") + p.name;
        lines.push_back({pack.empty() ? "Du jagst allein." : "Rudel: " + pack, theme::roleColor(Role::Werewolf)});
    }
    if (v.loverPartner != kNoPlayer)
        lines.push_back({"♥ Verliebt in " + nameOf(v, v.loverPartner), theme::roleColor(Role::Cupid)});
    if (role == Role::Witch) {
        lines.push_back({std::string("Heiltrank: ") + (v.healAvailable ? "bereit" : "verbraucht"),
                         v.healAvailable ? theme::roleColor(Role::Witch) : theme::faint});
        lines.push_back({std::string("Gifttrank: ") + (v.poisonAvailable ? "bereit" : "verbraucht"),
                         v.poisonAvailable ? theme::roleColor(Role::Witch) : theme::faint});
    }
    if (role == Role::Protector && v.lastProtected != kNoPlayer)
        lines.push_back({"Zuletzt beschützt: " + nameOf(v, v.lastProtected), theme::muted});
    if (role == Role::Seer) {
        if (v.seerResults.empty()) lines.push_back({"Noch keine Visionen.", theme::faint});
        for (const SeerResult& s : v.seerResults)
            lines.push_back({nameOf(v, s.player) + " – " + roleName(s.role), theme::roleColor(s.role)});
    }

    const float buttonsH = 44.0f + (m_net.isHost() ? 44.0f : 0.0f);
    area.takeTop(12.0f);
    Rect bottom = area.takeBottom(buttonsH);
    if (!lines.empty()) {
        const float h = std::min(area.h, 44.0f + static_cast<float>(lines.size()) * 22.0f);
        Rect know = area.takeTop(h);
        drawPanel(know);
        Rect k = know.shrink(18.0f, 14.0f);
        r.text(m_fonts.smallBold, {k.x, k.y}, "DEIN WISSEN", theme::muted);
        float ly = k.y + 24.0f;
        r.pushClip(know);
        for (const Line& l : lines) {
            r.text(m_fonts.small, {k.x, ly}, m_fonts.small.ellipsize(l.text, k.w), l.color);
            ly += 22.0f;
        }
        r.popClip();
    }

    // Verlassen / Abbrechen mit Sicherheitsabfrage
    const bool leaveArmed = m_time - m_leaveArmedTime < 3.0;
    if (m_ui.button("game/leave", bottom.takeBottom(38.0f), leaveArmed ? "Wirklich verlassen?" : "Spiel verlassen",
                    leaveArmed ? ButtonKind::Danger : ButtonKind::Ghost)) {
        if (leaveArmed || v.phase == Phase::GameOver) leaveSession();
        else m_leaveArmedTime = m_time;
    }
    if (m_net.isHost() && v.phase != Phase::GameOver) {
        bottom.takeBottom(6.0f);
        const bool abortArmed = m_time - m_abortArmedTime < 3.0;
        if (m_ui.button("game/abort", bottom.takeBottom(38.0f), abortArmed ? "Runde wirklich abbrechen?" : "Runde abbrechen",
                        abortArmed ? ButtonKind::Danger : ButtonKind::Ghost)) {
            if (abortArmed) m_net.sendReturnToLobby();
            else m_abortArmedTime = m_time;
        }
    }
}

void App::drawVillage(const Rect& area, const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const size_t n = v.players.size();
    if (n == 0) return;

    const float tokenR = std::clamp(std::min(area.w, area.h) / 15.0f, 20.0f, 34.0f);
    const Vec2 c{area.center().x, area.center().y + 14.0f};
    const float rx = std::max(80.0f, std::min(area.w * 0.5f - tokenR - 46.0f, 430.0f));
    const float ry = std::max(60.0f, std::min(area.h * 0.5f - tokenR - 40.0f, 270.0f));

    m_scenery.drawCampfire(r, c, tokenR / 28.0f, m_time, 0.75f + 0.25f * m_night);

    std::vector<Token> tokens;
    for (size_t i = 0; i < n; ++i) {
        const float a = -eng::kPi * 0.5f + 2.0f * eng::kPi * static_cast<float>(i) / static_cast<float>(n);
        tokens.push_back({v.players[i].id, c + Vec2{std::cos(a) * rx, std::sin(a) * ry}, tokenR});
    }
    auto tokenOf = [&](uint8_t id) -> const Token* {
        for (const Token& t : tokens)
            if (t.id == id) return &t;
        return nullptr;
    };

    // Stimmen als Pfeile
    std::map<uint8_t, int> tally;
    const bool wolfVotes = v.phase == Phase::Night;
    for (const PlayerView& p : v.players) {
        if (p.vote == kNoPlayer) continue;
        const Token* from = tokenOf(p.id);
        const Token* to = tokenOf(p.vote);
        if (!from || !to) continue;
        ++tally[p.vote];
        const Vec2 dir = (to->pos - from->pos).normalized();
        const Vec2 a = from->pos + dir * (tokenR + 4.0f);
        const Vec2 b = to->pos - dir * (tokenR + 9.0f);
        const Color col = wolfVotes ? theme::blood.withAlpha(0.75f) : theme::playerColor(p.id).withAlpha(0.6f);
        r.line(a, b, 2.5f, col);
        const Vec2 nrm{-dir.y, dir.x};
        r.triangle(b + dir * 8.0f, b - dir * 2.0f + nrm * 6.0f, b - dir * 2.0f - nrm * 6.0f, col);
    }

    // Spielfiguren
    for (const Token& t : tokens) {
        const PlayerView* p = v.player(t.id);
        if (!p) continue;
        const bool targetable = v.phase != Phase::GameOver && isTargetable(v, t.id);
        const Rect hit{t.pos.x - tokenR - 4.0f, t.pos.y - tokenR - 4.0f, (tokenR + 4.0f) * 2.0f, (tokenR + 4.0f) * 2.0f};
        const bool hovered = targetable && m_ui.hovered(hit);
        if (m_ui.clickArea(std::format("token/{}", t.id), hit, targetable)) clickPlayer(v, t.id);
        float& h = m_hover[t.id];
        h = eng::approach(h, hovered ? 1.0f : 0.0f, m_dt * 8.0f);
        drawToken(*p, t, v, targetable, hovered);
    }

    // Anzahl der Stimmen
    for (const auto& [id, count] : tally) {
        const Token* t = tokenOf(id);
        if (!t) continue;
        const Vec2 badge = t->pos + Vec2{tokenR * 0.75f, -tokenR * 0.8f};
        r.circle(badge, 11.0f, wolfVotes ? theme::blood : Color::hex(0xB8433A));
        r.circleOutline(badge, 11.0f, 1.5f, Color::hex(0xFFFFFF, 0.6f));
        r.textCentered(m_fonts.smallBold, {badge.x - 11, badge.y - 11, 22, 22}, std::to_string(count),
                       Color::hex(0xFFFFFF));
    }

    drawBubbles(tokens, v);
}

void App::drawToken(const PlayerView& p, const Token& t, const GameView& v, bool targetable, bool hovered) {
    eng::Renderer2D& r = m_renderer;
    const float hover = m_hover[p.id];
    const float R = t.radius * (1.0f + 0.07f * hover);
    const Vec2 pos = t.pos;
    const float time = static_cast<float>(m_time);
    const float pulse = 0.5f + 0.5f * std::sin(time * 4.0f);

    const bool selected =
        contains(v.selection, p.id) || (v.action == ActionKind::WitchPotions && v.poisonTarget == p.id);
    const Color base = p.alive ? theme::playerColor(p.id) : Color::hex(0x4A5063);

    r.circle(pos + Vec2{0, 5}, R, Color::hex(0x000000, 0.35f));
    if (v.action == ActionKind::WitchPotions && v.witchVictim == p.id) {
        r.glow(pos, R * 2.2f, theme::blood.withAlpha(0.35f + 0.2f * pulse));
        r.circleOutline(pos, R + 7.0f, 2.5f, theme::blood);
    }
    if (selected) {
        const Color ring = v.action == ActionKind::WitchPotions ? theme::roleColor(Role::Witch) : theme::gold;
        r.glow(pos, R * 2.0f, ring.withAlpha(0.25f + 0.15f * pulse));
        r.circleOutline(pos, R + 6.0f, 3.0f, ring);
    } else if (targetable) {
        r.circleOutline(pos, R + 5.0f, hovered ? 2.5f : 1.5f,
                        Color::hex(0xFFFFFF, hovered ? 0.75f : 0.18f + 0.12f * pulse));
    }
    if (p.id == v.you) r.circleOutline(pos, R + 2.0f, 2.0f, Color::hex(0xFFFFFF, 0.85f));

    r.circle(pos, R, base.scaled(0.55f));
    r.circle(pos, R - 3.0f, base);
    r.textCentered(R > 26.0f ? m_fonts.large : m_fonts.uiBold, {pos.x - R, pos.y - R, R * 2, R * 2},
                   theme::initial(p.name), Color::hex(0x1B1406, p.alive ? 0.85f : 0.5f));
    if (!p.alive) {
        r.circle(pos, R, Color::hex(0x0A0D18, 0.45f));
        r.textCentered(m_fonts.large, {pos.x - R, pos.y - R, R * 2, R * 2}, "†", Color::hex(0xD8DCE6, 0.9f));
    }

    if (p.role != Role::Unknown) {
        const Vec2 b = pos + Vec2{R * 0.74f, R * 0.74f};
        const Color rc = theme::roleColor(p.role);
        r.circle(b, 12.0f, Color::hex(0x0B0F1F));
        r.circleOutline(b, 12.0f, 1.5f, rc);
        r.textCentered(m_fonts.icon, {b.x - 12, b.y - 12, 24, 24}, roleIcon(p.role), rc);
    }
    if (p.lover) {
        const Vec2 b = pos + Vec2{-R * 0.78f, -R * 0.72f};
        r.circle(b, 10.0f, Color::hex(0x0B0F1F));
        r.textCentered(m_fonts.icon, {b.x - 10, b.y - 10, 20, 20}, "♥", theme::roleColor(Role::Cupid));
    }
    if (v.phase == Phase::Day && p.skip) {
        const Vec2 b = pos + Vec2{-R * 0.78f, R * 0.72f};
        r.circle(b, 10.0f, Color::hex(0x0B0F1F));
        r.textCentered(m_fonts.smallBold, {b.x - 10, b.y - 10, 20, 20}, "»", theme::gold);
    }

    // Name unter der Figur
    std::string label = p.name + (p.id == v.you ? " (Du)" : "");
    label = m_fonts.smallBold.ellipsize(label, 150.0f);
    const float lw = m_fonts.smallBold.measure(label);
    const Rect plate{pos.x - lw * 0.5f - 8.0f, pos.y + R + 6.0f, lw + 16.0f, 20.0f};
    r.roundRect(plate, 10.0f, Color::hex(0x0A0D1A, 0.7f));
    const Color nameColor = !p.connected ? theme::faint : p.alive ? theme::text : theme::muted;
    r.textCentered(m_fonts.smallBold, plate, label, nameColor);
    if (!p.connected) {
        r.textCentered(m_fonts.small, {plate.x, plate.bottom() + 1.0f, plate.w, 16.0f}, "offline",
                       Color::hex(0xF0A35E, 0.8f));
    }
}

void App::drawBubbles(const std::vector<Token>& tokens, const GameView&) {
    eng::Renderer2D& r = m_renderer;
    constexpr double kDuration = 5.0;
    for (const Token& t : tokens) {
        auto it = m_bubbles.find(t.id);
        if (it == m_bubbles.end()) continue;
        const double age = m_time - it->second.time;
        if (age > kDuration) continue;
        const float alpha = eng::clamp01(static_cast<float>(kDuration - age) / 0.8f) *
                            eng::clamp01(static_cast<float>(age) / 0.15f);

        std::vector<std::string> lines = m_fonts.small.wrap(it->second.text, 200.0f);
        if (lines.size() > 2) {
            lines.resize(2);
            lines[1] = m_fonts.small.ellipsize(lines[1] + " …", 200.0f);
        }
        float w = 0.0f;
        for (const std::string& l : lines) w = std::max(w, m_fonts.small.measure(l));
        const float lh = m_fonts.small.lineHeight();
        const float h = static_cast<float>(lines.size()) * lh + 12.0f;
        const Rect box{std::max(4.0f, t.pos.x - w * 0.5f - 10.0f), t.pos.y - t.radius - 18.0f - h, w + 20.0f, h};

        Color bg = Color::hex(0xF5F1E8, 0.95f * alpha);
        Color fg = Color::hex(0x1B1406, alpha);
        if (it->second.channel == ChatChannel::Wolves) {
            bg = Color::hex(0x5A1A16, 0.95f * alpha);
            fg = Color::hex(0xF8DCD8, alpha);
        } else if (it->second.channel == ChatChannel::Dead) {
            bg = Color::hex(0x34405E, 0.85f * alpha);
            fg = Color::hex(0xDCE3F2, alpha);
        }
        r.roundRect(box, 9.0f, bg);
        const Vec2 tip{t.pos.x, box.bottom() + 8.0f};
        r.triangle({tip.x - 7.0f, box.bottom() - 1.0f}, {tip.x + 7.0f, box.bottom() - 1.0f}, tip, bg);
        float y = box.y + 6.0f;
        for (const std::string& l : lines) {
            r.text(m_fonts.small, {box.x + 10.0f, y}, l, fg);
            y += lh;
        }
    }
}

void App::drawActionBar(const Rect& area, const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const std::string hint = actionHint(v);
    if (hint.empty() && v.action == ActionKind::None) return;

    const float w = std::min(area.w, 680.0f);
    const Rect bar{area.center().x - w * 0.5f, area.y + 8.0f, w, area.h - 8.0f};
    drawPanel(bar, 16.0f);
    Rect inner = bar.shrink(16.0f, 10.0f);

    // Knöpfe rechts
    auto button = [&](const char* id, const std::string& label, ButtonKind kind, bool enabled, float width) {
        const Rect br = inner.takeRight(width);
        inner.takeRight(8.0f);
        return m_ui.button(id, br.shrink(0.0f, 2.0f), label, kind, enabled);
    };
    auto confirmButton = [&](const char* label, bool ready) {
        if (v.confirmed) button("act/confirm", "✓ Bestätigt", ButtonKind::Normal, false, 150.0f);
        else if (button("act/confirm", label, ButtonKind::Primary, ready, 150.0f)) m_net.sendConfirm();
    };

    switch (v.action) {
    case ActionKind::WolfVote: confirmButton("Opfer wählen", !v.selection.empty()); break;
    case ActionKind::Inspect: confirmButton("Vision", !v.selection.empty()); break;
    case ActionKind::Protect: confirmButton("Beschützen", !v.selection.empty()); break;
    case ActionKind::HunterShot: confirmButton("Schießen", !v.selection.empty()); break;
    case ActionKind::CupidPick: confirmButton("Verkuppeln", v.selection.size() == 2); break;
    case ActionKind::WitchPotions: {
        if (button("act/done", "Fertig", ButtonKind::Primary, !v.confirmed, 110.0f)) m_net.sendConfirm();
        const bool canHeal = v.healAvailable && v.witchVictim != kNoPlayer && !v.confirmed;
        const std::string label = v.healUsed ? "♥ Wird geheilt" : "Heiltrank";
        if (button("act/heal", label, v.healUsed ? ButtonKind::Primary : ButtonKind::Normal, canHeal, 140.0f))
            m_net.sendWitchHeal(!v.healUsed);
        break;
    }
    case ActionKind::SkipDiscussion: {
        int alive = 0, skips = 0;
        for (const PlayerView& p : v.players) {
            if (!p.alive) continue;
            ++alive;
            if (p.skip) ++skips;
        }
        const PlayerView* me = v.me();
        const bool mine = me && me->skip;
        const std::string label = std::format("{} ({}/{})", mine ? "Warte …" : "Abstimmen", skips, alive / 2 + 1);
        if (button("act/skip", label, mine ? ButtonKind::Normal : ButtonKind::Primary, true, 160.0f))
            m_net.sendSkip(!mine);
        break;
    }
    case ActionKind::DayVote:
        if (!v.selection.empty() && button("act/unvote", "Zurückziehen", ButtonKind::Ghost, true, 130.0f))
            m_net.sendSelect(kNoPlayer);
        break;
    default:
        break;
    }

    // Hinweistext links
    const std::vector<std::string> lines = m_fonts.ui.wrap(hint, inner.w);
    const float lh = m_fonts.ui.lineHeight();
    const size_t count = std::min<size_t>(lines.size(), 2);
    float y = inner.center().y - lh * static_cast<float>(count) * 0.5f;
    for (size_t i = 0; i < count; ++i) {
        r.text(m_fonts.ui, {inner.x, y}, lines[i], v.action != ActionKind::None ? theme::text : theme::muted);
        y += lh;
    }
}

void App::drawBanner(const Rect& area) {
    const double age = m_time - m_bannerTime;
    constexpr double kDuration = 3.2;
    if (age < 0.0 || age > kDuration) return;
    eng::Renderer2D& r = m_renderer;
    const float fadeIn = eng::smoothstep(0.0f, 0.35f, static_cast<float>(age));
    const float fadeOut = 1.0f - eng::smoothstep(static_cast<float>(kDuration) - 0.7f, static_cast<float>(kDuration),
                                                  static_cast<float>(age));
    const float a = fadeIn * fadeOut;
    const float rise = (1.0f - fadeIn) * 14.0f;

    // schmal genug, um die Figuren links und rechts vom Feuer nicht zu verdecken
    const float w = std::clamp(area.w * 0.56f, 280.0f, 440.0f);
    const Rect band{area.center().x - w * 0.5f, area.center().y - 56.0f + rise, w, 104.0f};
    r.roundRect(band, 18.0f, Color::hex(0x05070F, 0.72f * a));
    r.roundRectOutline(band, 18.0f, 1.0f, theme::gold.withAlpha(0.25f * a));
    r.textCentered(m_fonts.heading, {band.x, band.y + 14.0f, band.w, 44.0f}, m_bannerTitle, theme::goldLight.fade(a));
    r.textCentered(m_fonts.narration, {band.x, band.y + 62.0f, band.w, 24.0f}, m_bannerSubtitle, theme::text.fade(a));
}

void App::drawGameOver(const Rect& area, const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    r.roundRect(area, 18.0f, Color::hex(0x02030A, 0.55f));

    const size_t rows = (v.players.size() + 1) / 2;
    const float w = std::min(area.w - 32.0f, 600.0f);
    const float h = std::min(area.h - 24.0f, 168.0f + static_cast<float>(rows) * 28.0f + 64.0f);
    const Rect panel{area.center().x - w * 0.5f, area.center().y - h * 0.5f, w, h};
    drawPanel(panel, 18.0f);

    const Color tc = theme::teamColor(v.winner);
    std::string title;
    switch (v.winner) {
    case Team::Village: title = "Das Dorf gewinnt!"; break;
    case Team::Wolves: title = "Die Werwölfe gewinnen!"; break;
    case Team::Lovers: title = "Die Verliebten gewinnen!"; break;
    default: title = "Niemand gewinnt …"; break;
    }
    r.glow({panel.center().x, panel.y + 50.0f}, 220.0f, tc.withAlpha(0.15f));
    r.textCentered(m_fonts.heading, {panel.x, panel.y + 22.0f, panel.w, 44.0f}, title, tc);
    const PlayerView* me = v.me();
    const bool won = me && me->winner;
    r.textCentered(m_fonts.ui, {panel.x, panel.y + 70.0f, panel.w, 24.0f},
                   won ? "★ Du hast gewonnen!" : "Du hast verloren.", won ? theme::goldLight : theme::muted);

    Rect list{panel.x + 28.0f, panel.y + 112.0f, panel.w - 56.0f, panel.h - 112.0f - 70.0f};
    r.pushClip(list);
    const float colW = list.w * 0.5f;
    for (size_t i = 0; i < v.players.size(); ++i) {
        const PlayerView& p = v.players[i];
        const float x = list.x + (i < rows ? 0.0f : colW);
        const float y = list.y + static_cast<float>(i % rows) * 28.0f;
        const Color nameColor = p.alive ? theme::text : theme::muted;
        r.text(m_fonts.smallBold, {x, y}, p.winner ? "★" : (p.alive ? "" : "†"), p.winner ? theme::goldLight : theme::faint);
        const std::string name = m_fonts.ui.ellipsize(p.name, colW * 0.5f);
        r.text(m_fonts.ui, {x + 18.0f, y - 2.0f}, name, nameColor);
        r.text(m_fonts.small, {x + 22.0f + colW * 0.5f, y}, std::string(roleIcon(p.role)) + " " + roleName(p.role),
               theme::roleColor(p.role));
    }
    r.popClip();

    const Rect bottom{panel.x + 28.0f, panel.bottom() - 58.0f, panel.w - 56.0f, 42.0f};
    if (m_net.isHost()) {
        if (m_ui.button("over/lobby", {bottom.center().x - 120.0f, bottom.y, 240.0f, bottom.h}, "Zurück in die Lobby",
                        ButtonKind::Primary))
            m_net.sendReturnToLobby();
    } else {
        r.textCentered(m_fonts.small, bottom, "Die Spielleitung bringt euch gleich zurück in die Lobby …", theme::muted);
    }
}

} // namespace ww
