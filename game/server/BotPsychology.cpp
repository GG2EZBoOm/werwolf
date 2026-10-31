#include "server/Bot.h"

#include <algorithm>
#include <cmath>

namespace ww {
namespace {
int votesFor(const GameView& view, uint8_t target) {
    if (view.election) return 0; // being elected captain is not a threat of execution
    int count = 0;
    for (const auto& p : view.players) if (p.alive && p.vote == target) ++count;
    return count;
}
bool publicDiscussion(Phase phase) { return phase == Phase::Assembly || phase == Phase::Vote || phase == Phase::Execution; }
}

float Bot::relationship(uint8_t other) const {
    const auto it = m_relationships.find(other);
    return it == m_relationships.end() ? 0.0f : it->second;
}

void Bot::changeRelationship(uint8_t other, float amount) {
    if (other != m_id && other != kNoPlayer)
        m_relationships[other] = std::clamp(relationship(other) + amount, -1.0f, 1.0f);
}

float Bot::socialBias(uint8_t other) const {
    const auto grudge = m_resentment.find(other);
    // Personal dislike is weaker than a witness report, and cannot trump a known role.
    return -0.65f * relationship(other) + (grudge == m_resentment.end() ? 0.0f : grudge->second * (0.6f + m_feelings.anger));
}

void Bot::feelChat(const Match& match, uint8_t speaker, const dialogue::Message& message) {
    for (const auto& claim : message.claims) {
        const auto* target = match.find(claim.target);
        if (!target || !target->alive || claim.weight == 0) continue;
        auto& seen = m_socialClaims[{speaker, claim.target}];
        if (seen.day != match.day()) seen = {match.day(), 0};
        const unsigned sign = claim.weight > 0 ? 1u : 2u;
        if (seen.signs & sign) continue; // repeating the same accusation does not farm panic or friendship
        seen.signs |= sign;
        if (claim.target == m_id) {
            if (claim.weight > 0) {
                m_feelings.stress = std::min(1.0f, m_feelings.stress + 0.42f);
                m_feelings.anger = std::min(1.0f, m_feelings.anger + 0.18f);
                changeRelationship(speaker, -0.3f);
            } else {
                m_feelings.stress = std::max(0.0f, m_feelings.stress - 0.12f);
                changeRelationship(speaker, 0.32f);
            }
        } else if (relationship(claim.target) >= 0.4f) {
            changeRelationship(speaker, claim.weight > 0 ? -0.12f : 0.12f);
            if (claim.weight > 0) m_feelings.anger = std::min(1.0f, m_feelings.anger + 0.06f);
        }
    }
}

void Bot::updateFeelings(float dt, const Match& match, const GameView& view) {
    const auto* me = match.find(m_id);
    if (!me) return;
    if (!m_socialInitialized) {
        m_socialInitialized = true;
        for (const auto& p : view.players)
            if (p.id != m_id && p.alive && p.house == me->house && me->house != kNoPlayer) changeRelationship(p.id, 0.2f);
    }
    m_feelings.fear = std::max(0.0f, m_feelings.fear - dt * 0.0008f);
    m_feelings.anger = std::max(0.0f, m_feelings.anger - dt * 0.0012f);
    m_feelings.stress = std::max(0.0f, m_feelings.stress - dt * 0.012f);
    for (auto& [id, resentment] : m_resentment) resentment = std::max(0.0f, resentment - dt * 0.0002f);
    if (!me->alive || view.phase == Phase::GameOver) return;
    const bool wolf = isEvilRole(view.yourRole);
    const auto visible = match.entitiesFor(m_id);
    // Quiet time together strengthens a friendship slowly; hostile people do not become friends by standing nearby.
    if (view.phase == Phase::Day)
        for (const auto& e : visible) {
            const auto* p = match.find(e.id);
            const Vec3 pos{e.x, 0, e.z};
            if (!p || e.kind != EntityLiving || e.id == m_id || relationship(e.id) < -0.1f || relationship(e.id) >= 0.55f ||
                m_dialogue.suspicion(e.id, view.day) > 0.5f || distanceXZ(pos, me->position) > 5.0f || !match.hasLineOfSight(pos, me->position)) continue;
            changeRelationship(e.id, dt * 0.003f);
        }
    for (const auto& p : match.players()) {
        if (p.alive) { m_feltDeaths.erase(p.id); continue; } // revival permits a reaction to a later death
        bool visibleBody = false;
        if (view.phase == Phase::Night)
            for (const auto& e : visible)
                if (e.id == p.id && e.kind == EntityCorpse && distanceXZ({e.x, 0, e.z}, me->position) < 14.0f &&
                    match.hasLineOfSight({e.x, 0, e.z}, me->position)) visibleBody = true;
        if (view.phase == Phase::Night && !visibleBody) continue; // no knowledge of a distant hidden murder
        if (p.deathCause == DeathCause::Left) continue;
        const auto* known = view.player(p.id);
        if (m_feltDeaths.insert(p.id).second) {
            const float bond = std::max(0.0f, relationship(p.id));
            m_feelings.fear = std::min(1.0f, m_feelings.fear + (wolf ? 0.06f : 0.25f) + bond * 0.25f);
            if (bond >= 0.4f) m_feelings.anger = std::min(1.0f, m_feelings.anger + 0.25f);
            if (!wolf && p.diedAtNight)
                plan(Phase::Day, view.day + (view.phase == Phase::Night ? 1 : 0), match.clock() + 3.0,
                     bond >= 0.4f ? tx("{} stand mir nahe. Ich gehe heute Nacht nicht mehr allein raus.",
                                       "{} was my friend. I'm not going out alone tonight.", p.name)
                                  : Text{"Nach diesen Morden habe ich Angst. Am liebsten würde ich heute Nacht im Haus bleiben.",
                                         "These murders scare me. I'd rather stay inside tonight."}, {}, p.id, p.id);
        }
        // A misjudgment is known only when the victim's village role is public. No hidden-role peeking.
        if ((p.deathCause != DeathCause::Vote && p.deathCause != DeathCause::Scapegoat) || !known ||
            known->role == Role::Unknown || roleTeam(known->role) != Team::Village || !m_feltVerdicts.insert(p.id).second || wolf) continue;
        m_feelings.anger = std::min(1.0f, m_feelings.anger + 0.4f + std::max(0.0f, relationship(p.id)) * 0.3f);
        uint8_t blamed = kNoPlayer;
        float blame = -1;
        if (const auto it = m_votes.find(p.deathDay); it != m_votes.end())
            for (const auto& [voter, target] : it->second) {
                if (target != p.id || voter == m_id) continue;
                const auto* v = view.player(voter);
                if (!v || !v->alive) continue;
                changeRelationship(voter, -0.25f);
                m_resentment[voter] = std::min(1.0f, m_resentment[voter] + 0.5f);
                const float score = socialBias(voter);
                if (score > blame) { blamed = voter; blame = score; }
            }
        if (const auto* b = match.find(blamed))
            plan(Phase::Assembly, p.deathDay + 1, 0,
                 tx("{}, du hast gegen {} gestimmt. Das war ein Fehlurteil. Ich bin wütend und werde dir nicht einfach wieder folgen!",
                    "{}, you voted against {}. That was a wrongful execution. I'm angry and won't just follow you again!", b->name, p.name),
                 Evidence{Evidence::Kind::Opinion, blamed, 0.8f}, blamed);
    }
    if (view.phase == Phase::Vote && !view.election && votesFor(view, m_id) > 0)
        m_feelings.stress = std::max(m_feelings.stress, std::min(1.0f, 0.3f * static_cast<float>(votesFor(view, m_id))));
}

Text Bot::emotionalStyle(Text text) const {
    if (m_feelings.stress < 0.3f) return text;
    // Keep the claim and its source intact. Short bursts and punctuation convey pressure without fake typos in names.
    auto rush = [&](std::string& s) {
        if (const size_t stop = s.find(". "); stop != std::string::npos) s[stop] = '!';
        if (!s.empty() && s.back() == '.') s.back() = '!';
        else if (!s.empty() && s.back() != '!') s += '!';
        if (m_feelings.stress >= 0.7f && !s.empty() && s.back() == '!') s += '!';
    };
    rush(text.de); rush(text.en);
    return text;
}

Text Bot::lastWords(const Match& match) const {
    const auto view = match.viewFor(m_id);
    if (view.yourRole == Role::Hunter) return {"Hängt mich, und ich nehme jemanden mit.", "Hang me and someone goes with me."};
    if (view.yourRole == Role::Witch && view.healAvailable) return {"Wer soll euch heute Nacht retten, wenn ich weg bin?", "Who saves you tonight if I'm gone?"};
    if (view.yourRole == Role::Witch && view.poisonAvailable) return {"Mein letzter Trank ist noch voll, überlegt euch das gut.", "My last potion is still full, think this through."};
    if (view.yourRole == Role::Seer)
        for (const auto& check : view.seerResults)
            if (isWolfRole(check.role) && match.find(check.player)->alive)
                return tx("Ich hab {} geprüft, das ist ein Wolf!", "I checked {}, they're a wolf!", match.find(check.player)->name);
    if (m_personality.style == Personality::Style::Terse) return {"Wer hat mich denn wirklich gesehen?", "Who actually saw me?"};
    if (m_feelings.anger > .5f) return {"Jetzt mal ehrlich, wer von euch hat überhaupt einen Beleg gegen mich?", "Seriously, which of you actually has evidence against me?"};
    return {"Bevor ihr mich hängt, schaut noch mal, von wem der Vorwurf kam.", "Before you hang me, check who made the accusation."};
}

Text Bot::roleHint(const Match& match) const {
    const auto view = match.viewFor(m_id);
    if (view.yourRole == Role::Hunter && !m_shot)
        return {"Überlegt euch das gut. Hängt mich, und ich nehme jemanden mit. Mein letzter Schuss sitzt.",
                "Think carefully. Hang me and I'll take someone with me. I still have my last shot."};
    if (view.yourRole == Role::Witch) {
        if (view.poisonAvailable) return {"Unterschätzt meine Fläschchen nicht. Ich habe noch einen Gifttrank. Macht mich nicht vorschnell zu eurem Feind.",
                                        "Don't underestimate my bottles. I still have a poison potion. Don't make me your enemy too quickly."};
        if (view.healAvailable) return {"Ich könnte heute Nacht noch ein Leben retten. Überlegt euch, ob ihr mich wirklich verlieren wollt.",
                                      "I could still save a life tonight. Think about whether you really want to lose me."};
        return {"Meine Tränke sind aufgebraucht. Ich kann euch nicht mehr helfen, aber das macht mich nicht zum Werwolf.",
                "My potions are spent. I can't help you with them any more, but that doesn't make me a werewolf."};
    }
    return {};
}

bool Bot::shouldReveal(const Match& match, uint8_t target) const {
    const auto view = match.viewFor(m_id);
    if (view.yourRole != Role::Seer) return true;
    if (m_seerRevealed) return true;
    if (view.phase == Phase::Night) return false;
    if (m_dialogue.otherSeer(m_id) || m_feelings.stress >= 0.65f || votesFor(view, m_id) >= 2) return true;
    if (!publicDiscussion(view.phase)) return false;
    for (const auto& result : view.seerResults) {
        const auto* p = view.player(result.player);
        if (!p || !p->alive || (target != kNoPlayer && target != result.player)) continue;
        if (isEvilRole(result.role)) return view.phase == Phase::Vote || m_phaseTime >= 2.0f + 3.0f * m_personality.caution;
        if (votesFor(view, result.player) >= 2) return true; // save a checked innocent from execution
    }
    return false;
}

uint8_t Bot::sacrificeTarget(const Match& match) const {
    const auto view = match.viewFor(m_id);
    if (!isEvilRole(view.yourRole) || !publicDiscussion(view.phase) || view.election) return kNoPlayer;
    int wolves = 0, alive = 0;
    for (const auto& p : view.players) if (p.alive) { ++alive; if (ally(view, p.role)) ++wolves; }
    if (wolves < 2 || alive - wolves <= wolves + 1) return kNoPlayer; // preserve an imminent pack win
    uint8_t target = kNoPlayer;
    float best = 0;
    for (const auto& p : view.players) {
        if (!p.alive || p.id == m_id || !ally(view, p.role)) continue;
        int outsideVotes = 0;
        for (const auto& voter : view.players)
            if (voter.alive && voter.vote == p.id && !ally(view, voter.role)) ++outsideVotes;
        const float evidence = m_dialogue.suspicion(p.id, view.day);
        if (outsideVotes < 2 || (outsideVotes < 3 && evidence < 2.0f)) continue;
        if (outsideVotes < votesFor(view, m_id)) continue; // saving myself is not a credibility sacrifice
        const float score = static_cast<float>(outsideVotes) + evidence - 0.3f * relationship(p.id);
        if (score > best) { target = p.id; best = score; }
    }
    return target;
}

void Bot::roleplay(const Match& match, const SayFn& say) {
    const auto view = match.viewFor(m_id);
    const auto* me = view.me();
    if (!me || !me->alive || !publicDiscussion(view.phase) || !m_replies.empty() || match.clock() < m_roleSpeakAfter ||
        match.clock() < m_replyAfter) return;
    Text line;
    Speech speech;
    if (const auto* victim = match.find(sacrificeTarget(match)); victim && m_sacrificed != victim->id &&
        (view.phase != Phase::Vote || view.timeLeft > 2.0f || me->vote == victim->id)) {
        m_sacrificed = victim->id;
        if (view.phase == Phase::Vote && view.timeLeft > 2.0f && me->vote != victim->id) {
            m_voted = false;
            m_think = 0.7f;
        }
        line = tx("Ich habe {} zu lange vertraut. Nach diesen Vorwürfen und Stimmen gehe ich nicht mehr mit. Ich stimme gegen {}.",
                  "I trusted {} for too long. After these accusations and votes, I can't stand by them. I'm voting against {}.", victim->name, victim->name);
        speech.evidence = Evidence{Evidence::Kind::Opinion, victim->id, 1.2f};
        speech.topic = victim->id;
    } else if (view.yourRole == Role::Seer && shouldReveal(match)) {
        uint8_t target = kNoPlayer;
        for (const auto& r : view.seerResults) {
            const auto* p = view.player(r.player);
            if (!p || !p->alive || m_sharedResults.contains(r.player) || !shouldReveal(match, r.player)) continue;
            if (target == kNoPlayer || isEvilRole(r.role)) target = r.player;
        }
        if (target != kNoPlayer) line = explain(match, target, &speech.evidence);
        else if (!m_seerRevealed && (m_feelings.stress >= 0.65f || votesFor(view, m_id) >= 2)) {
            m_seerRevealed = true;
            line = {"Bevor ihr mich hängt: Ich bin die Seherin. Ich habe noch keinen lebenden Werwolf sicher geprüft.",
                    "Before you hang me: I'm the seer. I haven't confirmed a living werewolf yet."};
        }
    } else if (m_roleHintDay != view.day && (m_feelings.stress >= 0.3f || votesFor(view, m_id) > 0)) {
        line = roleHint(match);
        if (!line.empty()) m_roleHintDay = view.day;
    }
    if (line.empty()) return;
    m_roleSpeakAfter = match.clock() + 15.0;
    m_replyAfter = match.clock() + 5.0;
    m_chatTimer = -1.0f;
    speak(say, line, false, speech);
}

} // namespace ww
