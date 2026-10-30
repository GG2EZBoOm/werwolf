#include "server/Bot.h"
#include "server/LocalLanguage.h"

#include <algorithm>
#include <cmath>

namespace ww {

void Bot::rememberReason(std::string kind, uint8_t target, uint8_t source, int day, double time,
                         ChatChannel channel, float weight, Text text, std::optional<Evidence> evidence) {
    // One current statement per source/subject/type/channel: repetitions cannot farm evidence.
    std::erase_if(m_knowledge, [&](const Knowledge& n) {
        return n.kind == kind && n.target == target && n.source == source && n.channel == channel;
    });
    m_knowledge.push_back({std::move(kind), target, source, day, time, channel, weight, std::move(text), evidence});
    while (m_knowledge.size() > 128) m_knowledge.pop_front();
}

void Bot::rememberDialogue(const Match& match, uint8_t speaker, ChatChannel channel, const dialogue::Message& message) {
    const auto* from = match.find(speaker);
    if (!from) return;
    for (const auto& c : message.claims) {
        const auto* target = match.find(c.target);
        if (!target) continue;
        const auto old = std::find_if(m_knowledge.begin(), m_knowledge.end(), [&](const Knowledge& n) {
            return n.kind == "behauptung" && n.source == speaker && n.target == c.target && n.channel == channel;
        });
        const bool changed = old != m_knowledge.end() && old->weight * c.weight < 0;
        if (old != m_knowledge.end() && old->weight * c.weight > 0 && std::abs(old->weight) > std::abs(c.weight) && !message.correction)
            continue; // a later weak opinion cannot erase the earlier report used by the rule-based memory
        if (changed || message.correction) {
            const bool correction = message.correction;
            rememberReason("widerspruch", speaker, speaker, match.day(), match.clock(), channel, correction ? 0.0f : .6f,
                correction ? tx("{} hat die frühere Aussage über {} an Tag {} ausdrücklich korrigiert. Die alte Beschuldigung zählt nicht zusätzlich.",
                                "{} explicitly corrected the earlier statement about {} on day {}. The old accusation does not count again.", from->name, target->name, match.day())
                           : tx("{} hat an Tag {} die Einschätzung zu {} umgekehrt, ohne die frühere Aussage zu erklären. Das ist ein Widerspruch, kein Rollenbeweis.",
                                "{} reversed the assessment on day {} about {} without explaining the earlier statement. That is a contradiction, not proof of a role.",
                                from->name, match.day(), target->name));
        }
        Text reason;
        if (message.seerReport && c.fact)
            reason = c.weight > 0
                ? tx("{} behauptete an Tag {}, {} als Seherin geprüft und als böse erkannt zu haben. Das ist ein berichtetes Ergebnis, keine eigene Prüfung von mir.",
                     "{} claimed on day {} that a seer check identified {} as evil. This is a reported result, not my own check.", from->name, match.day(), target->name)
                : tx("{} behauptete an Tag {}, {} durch eine Seherprüfung entlasten zu können. Das ist ein berichtetes Ergebnis, keine eigene Prüfung von mir.",
                     "{} claimed on day {} that a seer check cleared {}. This is a reported result, not my own check.", from->name, match.day(), target->name);
        else
            reason = c.weight > 0
                ? tx("{} hat {} an Tag {} beschuldigt. Ich kenne diese Aussage, habe sie aber nicht selbst bestätigt.",
                     "{} accused {} on day {}. I heard that claim, but have not confirmed it myself.", from->name, target->name, match.day())
                : tx("{} hat {} an Tag {} entlastet. Das spricht für diese Person, beweist aber keine Unschuld.",
                     "{} defended {} on day {}. That supports this person, but does not prove innocence.", from->name, target->name, match.day());
        rememberReason("behauptung", c.target, speaker, match.day(), match.clock(), channel, c.weight, std::move(reason));
        m_knowledge.back().report = message.seerReport && c.fact;
    }
    if (message.claimedRole != Role::Unknown) {
        const auto old = std::find_if(m_knowledge.begin(), m_knowledge.end(), [&](const Knowledge& n) {
            return n.kind == "rollenbehauptung" && n.source == speaker && n.channel == channel;
        });
        if (old != m_knowledge.end() && old->role != message.claimedRole) {
            const Role earlier = old->role;
            rememberReason("rollenwechsel", speaker, speaker, match.day(), match.clock(), channel, message.correction ? 0 : .6f,
                message.correction
                    ? tx("{} hat an Tag {} die Rollenbehauptung von {} zu {} ausdrücklich korrigiert.",
                         "{} explicitly corrected the role claim on day {} from {} to {}.", from->name, match.day(), roleText(earlier), roleText(message.claimedRole))
                    : tx("{} behauptete zuerst {}, jetzt an Tag {} aber {} zu sein. Diese Angaben widersprechen sich.",
                         "{} first claimed to be {}, but now on day {} claims to be {}. Those statements contradict each other.",
                         from->name, roleText(earlier), match.day(), roleText(message.claimedRole)));
        }
        rememberReason("rollenbehauptung", speaker, speaker, match.day(), match.clock(), channel, 0,
            tx("{} hat sich an Tag {} als {} ausgegeben. Eine Rollenbehauptung allein beweist diese Rolle nicht.",
               "{} claimed on day {} to be {}. Claiming a role alone does not prove it.", from->name, match.day(), roleWithArticle(message.claimedRole)));
        m_knowledge.back().role = message.claimedRole;
    }
    if (message.place != dialogue::Place::Unknown) {
        const std::array<Text, 6> places = {{{"unterwegs", "out and about"}, {"zu Hause", "at home"},
            {"in der Kapelle", "at the chapel"}, {"am Lagerfeuer", "at the campfire"}, {"am Brunnen", "at the well"}, {"im Wald", "in the forest"}}};
        const auto old = std::find_if(m_knowledge.begin(), m_knowledge.end(), [&](const Knowledge& n) {
            return n.kind == "alibibehauptung" && n.source == speaker && n.channel == channel;
        });
        if (old != m_knowledge.end() && old->day == match.day() && old->place != message.place &&
            (old->exclusive || message.exclusiveAlibi))
            rememberReason("alibiwechsel", speaker, speaker, match.day(), match.clock(), channel, message.correction ? 0 : .6f,
                message.correction
                    ? tx("{} hat das frühere Alibi an Tag {} ausdrücklich korrigiert.", "{} explicitly corrected the earlier alibi on day {}.", from->name, match.day())
                    : tx("{} hat an Tag {} unterschiedliche Orte für dieselbe ganze Nacht angegeben. Beide Alibis können nicht gleichzeitig stimmen.",
                         "{} named different locations for the same entire night on day {}. Both alibis cannot be true at once.", from->name, match.day()));
        rememberReason("alibibehauptung", speaker, speaker, match.day(), match.clock(), channel, 0,
            tx("{} gab an Tag {} an, nachts {} gewesen zu sein. Dieses Alibi ist bisher nur eine eigene Aussage dieser Person.",
               "{} said on day {} they had been {} at night. So far this alibi is only that person's own statement.",
               from->name, match.day(), places[static_cast<size_t>(message.place)]));
        m_knowledge.back().place = message.place;
        m_knowledge.back().exclusive = message.exclusiveAlibi;
    }
}

void Bot::addLanguageKnowledge(const Match& match, uint8_t speaker, ChatChannel channel, LanguageRequest& r) const {
    const auto view = match.viewFor(m_id);
    const bool evil = isEvilRole(view.yourRole);
    const uint8_t sacrificed = evil ? sacrificeTarget(match) : kNoPlayer;
    std::vector<LanguageRequest::Reason> notes;
    auto permittedTarget = [&](uint8_t target, float weight) {
        const auto* p = view.player(target);
        if (!p || !p->alive) return false;
        if (evil && p->alive && ally(view, p->role) && target != sacrificed && weight > 0) return false;
        if (!evil) {
            // Private knowledge can veto a misleading statement without revealing its secret reason.
            if (weight > 0 && p->role != Role::Unknown && !isEvilRole(p->role)) return false;
            for (const auto& check : view.seerResults)
                if (check.player == target && ((weight > 0 && !isEvilRole(check.role)) || (weight < 0 && isEvilRole(check.role)))) return false;
            if (weight < 0 && (m_wolfWitness.contains(target) || m_vampireWitness.contains(target))) return false;
        }
        return true;
    };
    for (const auto& n : m_knowledge) {
        // Public statements can be remembered elsewhere. Pack/family/local conversations stay
        // in their original channel; a whisper never becomes public through the model.
        if (n.channel != ChatChannel::Global && n.channel != channel) continue;
        if (!permittedTarget(n.target, n.weight)) continue;
        const bool hearsay = n.kind == "behauptung";
        float weight = n.weight;
        if (hearsay) weight *= m_dialogue.credibility(n.source) * (n.source == n.target ? .25f : 1.0f);
        if ((hearsay && !n.report) || n.kind == "abstimmung") weight *= std::pow(.65f, static_cast<float>(std::max(0, match.day() - n.day)));
        notes.push_back({n.target, n.source, n.day, n.time, weight, n.kind, n.text, n.evidence});
    }
    for (const auto& p : view.players) {
        const float bond = relationship(p.id);
        if (p.id == m_id || !p.alive || std::abs(bond) < .35f) continue;
        if (!permittedTarget(p.id, -bond)) continue;
        notes.push_back({p.id, m_id, match.day(), match.clock(), 0, "beziehung", bond > 0
            ? tx("{} steht mir nahe. Deshalb bin ich dieser Person gegenüber wohlwollender; Freundschaft beweist aber keine Unschuld.",
                 "{} is close to me. That makes me more sympathetic towards this person, but friendship does not prove innocence.", p.name)
            : tx("Mit {} habe ich ein angespanntes Verhältnis. Das färbt meine Einschätzung, ist aber kein Beweis für eine böse Rolle.",
                 "My relationship with {} is strained. That colors my judgment, but is not evidence of an evil role.", p.name), {}});
    }
    // Only results which this bot has already chosen to disclose are available for free selection.
    // In particular, a quiet seer cannot be tricked into exporting unrevealed checks or role tables.
    if (m_seerRevealed && !evil)
        for (const auto& check : view.seerResults) {
            if (!m_sharedResults.contains(check.player)) continue;
            const auto* target = match.find(check.player);
            if (!target) continue;
            const int night = m_checkedNight.contains(check.player) ? m_checkedNight.at(check.player) : 0;
            Text reason = night > 0
                ? tx("Meine bereits offengelegte Prüfung von {} stammt aus Nacht {}. {}",
                     "My already disclosed check of {} was made on night {}. {}", target->name, night, explain(match, check.player))
                : explain(match, check.player);
            notes.push_back({check.player, m_id, night, 0, isEvilRole(check.role) ? 8.0f : -8.0f, "offengelegte_pruefung", reason,
                Evidence{Evidence::Kind::Seer, check.player, isEvilRole(check.role) ? 2.4f : -1.8f}});
        }
    // A witnessed vote plus a publicly revealed result can confirm or refute someone's report.
    // Do not use private seer results or pack knowledge as if the whole village had seen them.
    if (view.settings.revealRoles && view.phase != Phase::Night)
        for (const auto& n : m_knowledge) {
            if (n.kind != "behauptung" || (n.channel != ChatChannel::Global && n.channel != channel)) continue;
            const auto* p = view.player(n.target);
            const auto* actual = match.find(n.target);
            const auto* source = match.find(n.source);
            if (!p || p->alive || p->role == Role::Unknown || !actual || !source || std::abs(n.weight) < 1.5f) continue;
            const bool agrees = (n.weight > 0) == isEvilRole(p->role);
            if (!permittedTarget(n.source, agrees ? -.5f : .8f)) continue;
            notes.push_back({n.source, n.source, actual->deathDay, actual->deathClock, agrees ? -.5f : .8f,
                "aufgedecktes_ergebnis", agrees
                    ? tx("Die Aussage von {} über {} von Tag {} passte später zur öffentlich aufgedeckten Rolle. Das stärkt für mich diese Quelle.",
                         "{}'s claim about {} from day {} later matched the publicly revealed role. That strengthens this source for me.", source->name, p->name, n.day)
                    : tx("Die Aussage von {} über {} von Tag {} passte nicht zur später öffentlich aufgedeckten Rolle. Deshalb zweifle ich an dieser Quelle.",
                         "{}'s claim about {} from day {} did not match the later publicly revealed role. That is why I doubt this source.", source->name, p->name, n.day), {}});
        }
    // Prefer the subject of the question, then strong/current evidence. Bound prompt size independently
    // of the number of residents and game length. No unfiltered danger scores (wolves use them to hunt).
    uint8_t topic = kNoPlayer;
    if (!r.history.empty()) topic = understand(match, speaker, r.history.back().second).topic;
    r.focus = topic == m_id ? kNoPlayer : topic;
    std::stable_sort(notes.begin(), notes.end(), [&](const auto& a, const auto& b) {
        auto rank = [&](const auto& n) {
            return (n.target == topic ? 20.0 : 0.0) + std::min(8.0f, std::abs(n.weight)) +
                   (n.source == m_id ? .4 : 0.0) + .2 / (1 + std::max(0, match.day() - n.day));
        };
        return rank(a) > rank(b);
    });
    if (notes.size() > 12) notes.resize(12);
    r.reasons = std::move(notes);
    std::map<uint8_t, float> scores;
    for (const auto& n : r.reasons) scores[n.target] += n.weight;
    for (const auto& [target, weight] : scores)
        r.assessments.push_back({target, std::clamp(weight - .65f * relationship(target), -8.0f, 8.0f), relationship(target)});
}

} // namespace ww
