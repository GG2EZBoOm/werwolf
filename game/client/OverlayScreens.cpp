#include "client/App.h"
#include "client/Theme.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

namespace ww {

using eng::Color;
using eng::Rect;
using eng::Vec2;
using namespace palette;

std::vector<std::pair<std::string, Color>> App::roleStatusLines(const GameView& v) {
    const Role role = v.yourRole;
    const Color rc = theme::roleColor(role);
    const bool alive = imAlive();

    std::vector<std::pair<std::string, Color>> lines;
    if (!alive) lines.push_back({"Du bist tot – als Geist siehst du alles.", theme::ghost});
    if (isWolfRole(role)) {
        std::string pack;
        for (const PlayerView& p : v.players)
            if (isWolfRole(p.role) && p.id != v.you && p.alive) pack += (pack.empty() ? "" : ", ") + p.name;
        lines.push_back({pack.empty() ? "Du jagst allein." : "Rudel: " + pack, theme::blood});
        if (v.phase == Phase::Night) lines.push_back({v.packKillsLeft ? "Das Rudel hungert noch." : "Das Rudel ist satt.", theme::muted});
        lines.push_back({m_chatWolves ? "Chat: Rudel (Tab wechselt)" : "Chat: Nähe (Tab: Rudel)", theme::faint});
    }
    if (v.loverPartner != kNoPlayer) {
        const PlayerView* p = v.player(v.loverPartner);
        lines.push_back({std::string("♥ Verliebt in ") + (p ? p->name : "?"), theme::roleColor(Role::Cupid)});
    }
    if (const PlayerView* me = v.me(); me && me->captain) lines.push_back({"♛ Du bist der Hauptmann – doppelte Stimme.", palette::brassLight});
    if (role == Role::Witch) {
        lines.push_back({std::string("Heiltrank: ") + (v.healAvailable ? "bereit" : "verbraucht"), v.healAvailable ? rc : theme::faint});
        lines.push_back({std::string("Gifttrank: ") + (v.poisonAvailable ? "bereit" : "verbraucht"), v.poisonAvailable ? rc : theme::faint});
    }
    for (const SeerResult& s : v.seerResults) {
        const PlayerView* p = v.player(s.player);
        lines.push_back({(p ? p->name : "?") + " – " + roleName(s.role), theme::roleColor(s.role)});
    }
    if (v.protectedTarget != kNoPlayer) {
        const PlayerView* p = v.player(v.protectedTarget);
        lines.push_back({"Beschützt: " + (p ? p->name : std::string("?")), theme::roleColor(Role::Protector)});
    }
    if (alive) {
        switch (role) {
        case Role::Elder:
            lines.push_back({v.shieldLeft ? "Du überstehst noch einen Wolfsangriff." : "Dein Schutz ist verbraucht.", v.shieldLeft ? rc : theme::faint});
            break;
        case Role::LittleGirl: lines.push_back({"Nachts liest du den Rudel-Chat mit.", theme::muted}); break;
        case Role::Medium:
            lines.push_back({v.phase != Phase::Night ? "Nachts sprichst du mit den Toten."
                             : m_chatWolves          ? "Chat: Geister (Tab wechselt)"
                                                     : "Chat: Nähe (Tab: Geister)",
                             theme::faint});
            break;
        case Role::Tracker:
            if (const PlayerView* p = v.player(v.tracked)) lines.push_back({"Fährte: " + p->name, rc});
            break;
        case Role::Blacksmith:
            if (v.can(AbilityReinforce)) lines.push_back({"Heute kannst du eine Tür beschlagen (E).", rc});
            break;
        case Role::Priest:
            if (v.can(AbilityHolyWater)) lines.push_back({"Weihwasser: bereit", rc});
            break;
        case Role::Mayor: lines.push_back({"Deine Stimme zählt doppelt.", rc}); break;
        case Role::Arsonist: lines.push_back({std::format("Getränkte Häuser: {}", std::popcount(v.doused)), rc}); break;
        case Role::Jester: lines.push_back({"Ziel: Lass dich hinrichten!", rc}); break;
        case Role::Angel:
            lines.push_back({v.day <= 1 ? "Ziel: Stirb in der ersten Runde!" : "Du lebst noch – das Paradies muss warten.", rc});
            break;
        case Role::Sister:
        case Role::Brother: {
            std::string family;
            for (const PlayerView& p : v.players)
                if (p.id != v.you && p.role == role) family += (family.empty() ? "" : ", ") + p.name;
            lines.push_back({(role == Role::Sister ? "Schwester: " : "Brüder: ") + family, rc});
            lines.push_back({m_chatWolves ? "Chat: Geschwister (Tab wechselt)" : "Chat: Nähe (Tab: Geschwister)", theme::faint});
            break;
        }
        case Role::Actor:
            if (v.acting != Role::Unknown) lines.push_back({std::string("Heute Nacht: ") + roleName(v.acting), theme::roleColor(v.acting)});
            break;
        case Role::WildChild:
            if (const PlayerView* p = v.player(v.model)) lines.push_back({"Vorbild: " + p->name, rc});
            break;
        case Role::Piper: {
            int enchanted = 0, alivePlayers = 0;
            for (const PlayerView& p : v.players) {
                if (!p.alive || p.id == v.you) continue;
                ++alivePlayers;
                if (p.enchanted) ++enchanted;
            }
            lines.push_back({std::format("Verzaubert: {} von {}", enchanted, alivePlayers), rc});
            break;
        }
        case Role::Sectarian: lines.push_back({"Gegner: alle aus der anderen Dorfhälfte", rc}); break;
        default: break;
        }
        if (v.acting == Role::Medium)
            lines.push_back({m_chatWolves ? "Chat: Geister (Tab wechselt)" : "Chat: Nähe (Tab: Geister)", theme::faint});
    }
    return lines;
}

void App::drawRoleCardFace(const Rect& card, Role role, float alpha) {
    eng::Renderer2D& r = m_renderer;
    const Color rc = theme::roleColor(role);
    m_kit.paper(card, 11000u + static_cast<uint32_t>(role), alpha);
    // Doppelter Tuscherahmen mit roten Ecken
    const Rect outer = card.shrink(13.0f), inner = card.shrink(19.0f);
    const Color frameInk = inkSoft.withAlpha(0.9f * alpha);
    m_kit.inkLine({outer.x, outer.y}, {outer.right(), outer.y}, 1.6f, frameInk, 1.0f, 1);
    m_kit.inkLine({outer.right(), outer.y}, {outer.right(), outer.bottom()}, 1.6f, frameInk, 1.0f, 2);
    m_kit.inkLine({outer.right(), outer.bottom()}, {outer.x, outer.bottom()}, 1.6f, frameInk, 1.0f, 3);
    m_kit.inkLine({outer.x, outer.bottom()}, {outer.x, outer.y}, 1.6f, frameInk, 1.0f, 4);
    r.roundRectOutline(inner, 0.0f, 0.8f, inkFaded.withAlpha(0.7f * alpha));
    for (Vec2 p : {Vec2{outer.x, outer.y}, Vec2{outer.right(), outer.y}, Vec2{outer.x, outer.bottom()}, Vec2{outer.right(), outer.bottom()}})
        m_kit.diamond(p, 4.0f, inkRed.withAlpha(alpha));

    // Bild: das Zeichen der Rolle, in Tusche, mit einem Hauch Farbe
    const Vec2 c{card.center().x, card.y + card.h * 0.38f};
    const float rad = card.w * 0.27f;
    r.circle(c, rad, eng::lerp(rc, Color::hex(0xE9DDBE), 0.82f).withAlpha(0.6f * alpha));
    r.circleOutline(c, rad, 1.4f, inkSoft.withAlpha(alpha));
    r.circleOutline(c, rad + 5.0f, 0.8f, inkFaded.withAlpha(0.7f * alpha));
    for (int i = 0; i < 12; ++i) {
        const float a = static_cast<float>(i) / 12.0f * 2.0f * eng::kPi;
        const Vec2 d{std::cos(a), std::sin(a)};
        r.line(c + d * (rad + 8.0f), c + d * (rad + (i % 3 == 0 ? 16.0f : 12.0f)), 1.0f, inkFaded.withAlpha(0.8f * alpha));
    }
    r.textCentered(m_fonts.iconHuge, {c.x - rad, c.y - rad + 2.0f, rad * 2, rad * 2}, roleIcon(role),
                   eng::lerp(rc, inkDark, 0.62f).withAlpha(alpha));

    // Name in Handschrift, darunter das Lager
    const std::string name = roleName(role);
    eng::Font& font = m_fonts.scriptLarge.measure(name) < card.w - 44.0f ? m_fonts.scriptLarge : m_fonts.scriptBold;
    const float nw = font.measure(name);
    const float ny = card.y + card.h * 0.68f;
    r.text(font, {card.center().x - nw * 0.5f, ny}, name, inkDark.withAlpha(alpha));
    m_kit.swash({card.center().x - nw * 0.5f, ny + font.lineHeight() * 0.9f}, nw, inkRed.withAlpha(0.85f * alpha), 1.0f, 13);
    const Team team = roleTeam(role);
    const std::string side = std::string("– ") + teamName(team) + " –";
    r.text(m_fonts.paperItalic, {card.center().x - m_fonts.paperItalic.measure(side) * 0.5f, ny + font.lineHeight() + 10.0f}, side,
           inkFaded.withAlpha(alpha));
}

void App::drawRoleReveal(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width(), H = r.height();
    const float age = static_cast<float>(m_time - m_revealStart);
    m_hudBlockers.push_back({0, 0, W, H});
    m_kit.fade(0.8f * eng::smoothstep(0.0f, 0.5f, age));
    m_kit.vignette(1.0f);
    m_kit.embers({0, H * 0.3f, W, H * 0.7f}, 26, 0.7f * eng::smoothstep(0.2f, 1.0f, age));

    const float cw = 260.0f, ch = 380.0f;
    const Vec2 c{W * 0.5f, H * 0.1f + 20.0f + ch * 0.5f};
    m_kit.light({c.x + 160.0f, c.y - 220.0f}, 1.2f);
    m_kit.candleLight({c.x + 180.0f, c.y - 200.0f}, 520.0f, eng::smoothstep(0.3f, 1.2f, age));
    const float labelA = eng::smoothstep(0.2f, 0.8f, age);
    const char* title = "Das Schicksal zieht deine Karte …";
    r.text(m_fonts.scriptBold, {W * 0.5f - m_fonts.scriptBold.measure(title) * 0.5f, H * 0.1f - 30.0f}, title, chalk.withAlpha(0.85f * labelA));

    // Die Karte fällt auf den Tisch und dreht sich um
    const float flip = eng::smoothstep(0.9f, 1.7f, age);
    const float sx = std::fabs(std::cos(flip * eng::kPi));
    const float rise = eng::smoothstep(0.1f, 0.9f, age);
    const float tilt = (1.0f - rise) * 0.25f + (flip < 1.0f ? std::sin(flip * eng::kPi) * 0.05f : 0.0f) - 0.02f;
    const Rect full{c.x - cw * 0.5f, c.y - ch * 0.5f + (1.0f - rise) * -60.0f, cw, ch};
    const Rect card{c.x - cw * 0.5f * sx, full.y, cw * sx, ch};
    m_kit.beginTilt(full, tilt);
    if (flip < 0.5f) {
        // Rückseite: dunkles Leder mit Messingrahmen und Siegel
        r.softRect(card.offset(8, 12), 6.0f, 18.0f, Color::hex(0x000000, 0.6f * rise));
        r.gradientV(card, Color::hex(0x2A1A14, rise), Color::hex(0x160D09, rise));
        if (sx > 0.3f) {
            m_kit.frame(card.shrink(10.0f), brass, rise);
            m_kit.seal(card.center(), 34.0f * sx, wax, "☾", rise, 23);
        }
    } else if (sx > 0.05f) {
        drawRoleCardFace(card, v.yourRole, 1.0f);
    }
    m_kit.endTilt();

    // Beschreibung wird wie mit der Feder geschrieben
    const float textA = eng::smoothstep(1.7f, 2.2f, age);
    if (textA > 0.01f) {
        float y = c.y + ch * 0.5f + 34.0f;
        const std::vector<std::string> lines = m_fonts.subtitle.wrap(roleDescription(v.yourRole), std::min(660.0f, W - 80.0f));
        const float total = static_cast<float>(lines.size());
        for (size_t i = 0; i < lines.size(); ++i) {
            const float p = std::clamp((age - 1.9f) * 1.4f - static_cast<float>(i), 0.0f, 1.0f);
            const float lw = m_fonts.subtitle.measure(lines[i]);
            m_kit.writeText(m_fonts.subtitle, {W * 0.5f - lw * 0.5f, y}, lines[i], parchment.withAlpha(textA), p);
            y += m_fonts.subtitle.lineHeight() + 2.0f;
        }
        const float hintA = eng::smoothstep(2.2f + total * 0.7f, 2.8f + total * 0.7f, age) * (0.7f + 0.3f * std::sin(age * 3.0f));
        const float hw = m_kit.keycapWidth("Leertaste") + 8.0f + m_fonts.small.measure("Ins Dorf");
        m_kit.hint({W * 0.5f - hw * 0.5f, std::min(H - 50.0f, y + 24.0f)}, "Leertaste", "Ins Dorf", hintA);
    }
    m_kit.grain(m_config.filmGrain ? 0.05f : 0.0f);
}

void App::drawJournal(const GameView& v) {
    eng::Renderer2D& r = m_renderer;
    const float W = r.width(), H = r.height();
    const float a = m_kit.anim("journal/open", 1.0f, 6.0f);
    m_hudBlockers.push_back({0, 0, W, H});
    m_kit.fade(0.55f * a);

    // Aufgeschlagenes Buch: Ledereinband, zwei Pergamentseiten
    const float bw = std::min(1100.0f, W - 80.0f), bh = std::min(660.0f, H - 110.0f);
    const Rect book{std::round(W * 0.5f - bw * 0.5f), std::round(H * 0.5f - bh * 0.5f + (1.0f - a) * 30.0f), bw, bh};
    m_kit.light({book.right() - 80.0f, book.y - 60.0f}, 1.1f);
    r.softRect(book.offset(10, 16), 8.0f, 22.0f, Color::hex(0x000000, 0.6f * a));
    r.gradientV(book.shrink(-12.0f), Color::hex(0x3A2216, a), Color::hex(0x23140C, a));
    const float spine = book.center().x;
    const Rect leftPage{book.x, book.y, bw * 0.5f - 4.0f, bh};
    const Rect rightPage{spine + 4.0f, book.y, bw * 0.5f - 4.0f, bh};
    m_kit.beginTilt(leftPage, -0.004f);
    m_kit.paper(leftPage, 12001u, a);
    m_kit.endTilt();
    m_kit.beginTilt(rightPage, 0.004f);
    m_kit.paper(rightPage, 12002u, a);
    m_kit.endTilt();
    // Bundsteg: die Seiten wölben sich zur Mitte
    r.gradientH({spine - 46.0f, book.y + 4.0f, 46.0f, bh - 8.0f}, Color::hex(0x2B1A0E, 0.0f), Color::hex(0x2B1A0E, 0.45f * a));
    r.gradientH({spine, book.y + 4.0f, 46.0f, bh - 8.0f}, Color::hex(0x2B1A0E, 0.45f * a), Color::hex(0x2B1A0E, 0.0f));
    // Lesebändchen
    r.rect({spine + 60.0f, book.y - 6.0f, 14.0f, bh * 0.55f}, Color::hex(0x7A1F18, a));
    r.triangle({spine + 60.0f, book.y - 6.0f + bh * 0.55f}, {spine + 74.0f, book.y - 6.0f + bh * 0.55f}, {spine + 67.0f, book.y + bh * 0.55f - 14.0f},
               Color::hex(0x4E140F, a));

    // Linke Seite: ich
    Rect left = leftPage.shrink(46.0f, 40.0f);
    r.text(m_fonts.scriptLarge, {left.x, left.y - 4.0f}, "Tagebuch", inkRed.withAlpha(a));
    r.text(m_fonts.paperItalic, {left.x, left.y + 40.0f}, phaseWithDay(v), inkFaded.withAlpha(a));
    float y = left.y + 74.0f;
    const Role role = v.yourRole;
    const Color rc = eng::lerp(theme::roleColor(role), inkDark, 0.6f);
    r.circleOutline({left.x + 30.0f, y + 30.0f}, 28.0f, 1.4f, inkSoft.withAlpha(a));
    r.textCentered(m_fonts.iconLarge, {left.x + 6.0f, y + 6.0f, 48.0f, 48.0f}, roleIcon(role), rc.withAlpha(a));
    r.text(m_fonts.scriptBold, {left.x + 74.0f, y + 2.0f}, roleName(role), inkDark.withAlpha(a));
    r.text(m_fonts.paperItalic, {left.x + 75.0f, y + 34.0f}, teamName(roleTeam(role)), inkFaded.withAlpha(a));
    y += 78.0f;
    for (const std::string& line : m_fonts.paper.wrap(roleDescription(role), left.w)) {
        r.text(m_fonts.paper, {left.x, y}, line, inkDark.withAlpha(0.9f * a));
        y += m_fonts.paper.lineHeight();
    }
    y += 16.0f;
    r.text(m_fonts.script, {left.x, y}, "Was ich weiß", inkRed.withAlpha(a));
    y += m_fonts.script.lineHeight() + 2.0f;
    const std::vector<std::pair<std::string, Color>> lines = roleStatusLines(v);
    if (lines.empty()) {
        r.text(m_fonts.hand, {left.x, y}, "Noch nichts. Augen und Ohren offen halten.", inkSoft.withAlpha(a));
        y += m_fonts.hand.lineHeight();
    }
    for (const auto& [text, color] : lines) {
        if (y > left.bottom() - 30.0f) break;
        r.text(m_fonts.hand, {left.x + 14.0f, y}, m_fonts.hand.ellipsize(text, left.w - 14.0f), eng::lerp(color, inkDark, 0.82f).withAlpha(a));
        m_kit.diamond({left.x + 4.0f, y + m_fonts.hand.lineHeight() * 0.55f}, 2.2f, inkRed.withAlpha(0.8f * a));
        y += m_fonts.hand.lineHeight();
    }
    if (const PlayerView* me = v.me(); me && me->house != kNoPlayer)
        r.text(m_fonts.paperItalic, {left.x, left.bottom() - 4.0f}, "Mein Haus ist auf der Karte markiert.", inkFaded.withAlpha(a));

    // Rechte Seite: die anderen
    Rect right = rightPage.shrink(46.0f, 40.0f);
    right.x += 30.0f;
    right.w -= 30.0f;
    r.text(m_fonts.scriptLarge, {right.x, right.y - 4.0f}, "Die Bewohner", inkRed.withAlpha(a));
    float ry = right.y + 48.0f;
    const float rowH = std::min(28.0f, (right.h * 0.6f) / std::max<float>(1.0f, static_cast<float>(v.players.size())));
    for (const PlayerView& p : v.players) {
        std::string name = p.name;
        if (p.captain) name = "♛ " + name;
        if (p.id == v.you) name += " (ich)";
        const Color nameInk = p.alive ? eng::lerp(theme::playerColor(p.id), inkDark, 0.72f) : inkFaded;
        r.text(m_fonts.hand, {right.x, ry + rowH * 0.5f - m_fonts.hand.lineHeight() * 0.5f}, m_fonts.hand.ellipsize(name, right.w * 0.48f),
               nameInk.withAlpha(a));
        if (!p.alive) {
            // Durchgestrichen mit roter Tinte
            const float nw = std::min(m_fonts.hand.measure(name), right.w * 0.48f);
            m_kit.inkLine({right.x - 2.0f, ry + rowH * 0.55f}, {right.x + nw + 4.0f, ry + rowH * 0.45f}, 1.6f, inkRed.withAlpha(0.85f * a), 1.0f,
                          static_cast<uint32_t>(p.id));
        }
        std::string marks;
        if (p.lover) marks += "♥";
        if (p.enchanted) marks += " ♪";
        if (p.marked) marks += " ♠";
        if (!marks.empty())
            r.text(m_fonts.icon, {right.x + right.w * 0.5f, ry + rowH * 0.5f - m_fonts.icon.lineHeight() * 0.5f}, marks, inkRed.withAlpha(a));
        const std::string known = p.role == Role::Unknown ? std::string("?") : std::string(roleIcon(p.role)) + " " + roleName(p.role);
        r.textRight(m_fonts.paperItalic, {right.right(), ry + rowH * 0.5f - m_fonts.paperItalic.lineHeight() * 0.5f},
                    m_fonts.paperItalic.ellipsize(known, right.w * 0.38f),
                    (p.role == Role::Unknown ? inkFaded : eng::lerp(theme::roleColor(p.role), inkDark, 0.6f)).withAlpha(a));
        ry += rowH;
    }
    ry += 14.0f;
    r.text(m_fonts.script, {right.x, ry}, "Chronik", inkRed.withAlpha(a));
    ry += m_fonts.script.lineHeight() + 2.0f;
    std::vector<const LogEntry*> deaths;
    for (const LogEntry& e : m_net.log())
        if (!e.isChat && e.kind == NoticeKind::Death) deaths.push_back(&e);
    if (deaths.empty()) r.text(m_fonts.paperItalic, {right.x, ry}, "Noch ist niemand gestorben.", inkSoft.withAlpha(a));
    for (auto it = deaths.rbegin(); it != deaths.rend() && ry < right.bottom() - 10.0f; ++it) {
        r.text(m_fonts.paperItalic, {right.x, ry}, m_fonts.paperItalic.ellipsize("† " + (*it)->text, right.w), inkDark.withAlpha(a));
        ry += m_fonts.paperItalic.lineHeight() + 2.0f;
    }

    const float hintW = m_kit.keycapWidth("J") + 8.0f + m_fonts.small.measure("Schließen");
    m_kit.hint({book.center().x - hintW * 0.5f, book.bottom() + 20.0f}, "J", "Schließen", a);
}
void App::drawSubtitles() {
    if (!m_config.subtitles || m_subtitle.empty() || !m_revealDone || m_pauseOpen || m_journalOpen) return;
    const float age = static_cast<float>(m_time - m_subtitleTime);
    if (age > 6.0f) return;
    eng::Renderer2D& r = m_renderer;
    const float a = eng::smoothstep(0.0f, 0.3f, age) * (1.0f - eng::smoothstep(5.0f, 6.0f, age));
    const float W = r.width();
    const std::vector<std::string> lines = m_fonts.subtitle.wrap(m_subtitle, std::min(760.0f, W - 120.0f));
    const float lh = m_fonts.subtitle.lineHeight();
    float y = r.height() * 0.68f - static_cast<float>(lines.size()) * lh * 0.5f;
    const float bandH = static_cast<float>(lines.size()) * lh + 20.0f;
    r.gradientH({0, y - 10.0f, W * 0.5f, bandH}, ink.withAlpha(0.0f), ink.withAlpha(0.55f * a));
    r.gradientH({W * 0.5f, y - 10.0f, W * 0.5f, bandH}, ink.withAlpha(0.55f * a), ink.withAlpha(0.0f));
    for (const std::string& line : lines) {
        r.textCentered(m_fonts.subtitle, {0, y + 1.5f, W, lh}, line, Color::hex(0x000000, 0.6f * a));
        r.textCentered(m_fonts.subtitle, {0, y, W, lh}, line, parchment.withAlpha(a));
        y += lh;
    }
}

} // namespace ww
