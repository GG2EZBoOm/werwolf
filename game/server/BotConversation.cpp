#include "server/Bot.h"

#include <algorithm>

namespace ww {
namespace {

bool secretChannel(ChatChannel channel) {
    return channel == ChatChannel::Wolves || channel == ChatChannel::Family || channel == ChatChannel::Dead;
}

// "Mia, die Rolle …" / "Mia, ich glaube dir" rather than "Mia, Die …" / "Mia, Ich …"; English "I" and
// player names keep their capital.
std::string lowerFirst(std::string s, const Match& match) {
    if (s.empty() || s[0] < 'A' || s[0] > 'Z') return s;
    if (s.substr(0, s.find_first_of(" ,.!?:;'")) == "I") return s;
    for (const auto& p : match.players())
        if (!p.name.empty() && s.starts_with(p.name) &&
            (s.size() == p.name.size() || std::string_view(" ,.!?:;'").find(s[p.name.size()]) != std::string_view::npos))
            return s;
    s[0] = static_cast<char>(s[0] - 'A' + 'a');
    return s;
}

Text addressTo(const std::string& name, const Text& response, const Match& match) {
    return {name + ", " + lowerFirst(response.de, match), name + ", " + lowerFirst(response.en, match)};
}

const dialogue::Claim* factAbout(const dialogue::Message& message, uint8_t target) {
    for (const auto& c : message.claims)
        if (c.target == target && c.fact) return &c;
    return nullptr;
}

const dialogue::RoleReport* roleReportAbout(const dialogue::Message& message, uint8_t target) {
    for (const auto& r : message.roleReports)
        if (r.target == target) return &r;
    return nullptr;
}

// Seer results and role reports: statements of fact that someone can confirm or refute.
bool reportsFacts(const dialogue::Message& message) { return message.seerReport || !message.roleReports.empty(); }

std::vector<uint8_t> reportedPlayers(const dialogue::Message& message) {
    std::vector<uint8_t> out;
    for (const auto& r : message.roleReports) out.push_back(r.target);
    if (message.seerReport)
        for (const auto& c : message.claims)
            if (c.fact && std::find(out.begin(), out.end(), c.target) == out.end()) out.push_back(c.target);
    return out;
}

} // namespace

float Bot::suspicion(const Match& match, uint8_t target) const {
    const auto own = m_suspicion.find(target);
    // gutgläubige Bots geben mehr auf das, was andere sagen, skeptische weniger
    return (own == m_suspicion.end() ? 0.0f : own->second) + m_personality.trust * m_dialogue.suspicion(target, match.day()) + socialBias(target);
}

dialogue::Message Bot::understand(const Match& match, uint8_t speaker, const Text& text) const {
    std::vector<dialogue::Person> people;
    for (const auto& p : match.players()) people.push_back({p.id, p.name});
    uint8_t topic = kNoPlayer;
    if (auto it = m_topics.find(speaker); it != m_topics.end() && match.clock() - it->second.time < 60.0) topic = it->second.player;
    auto message = dialogue::parse(text.de.empty() ? text.en : text.de, people, speaker, m_id, topic);
    if (!message.seerReport && m_dialogue.claimedRole(speaker) == Role::Seer) dialogue::presentSeerResults(message);
    return message;
}

int Bot::nightsSoFar(const Match& match) {
    const int day = match.day();
    return std::max(0, match.phase() == Phase::Night || match.phase() == Phase::Morning ? day : day - 1);
}

bool Bot::knowsFirstHand(const Match& match, const Evidence& heard) const {
    if (heard.kind == Evidence::Kind::KnownRole || heard.kind == Evidence::Kind::Vouch || heard.kind == Evidence::Kind::Opinion) return false;
    for (const auto& result : match.viewFor(m_id).seerResults)
        if (result.player == heard.target && isEvilRole(result.role) == (heard.weight > 0)) return true;
    return heard.weight > 0 && (m_wolfWitness.contains(heard.target) || m_vampireWitness.contains(heard.target));
}

int Bot::verdict(const Match& match, const dialogue::Message& message, uint8_t target) const {
    const auto view = match.viewFor(m_id);
    Role known = target == m_id ? view.yourRole : Role::Unknown;
    if (const auto* p = view.player(target); known == Role::Unknown && p) known = p->role;
    for (const auto& result : view.seerResults)
        if (known == Role::Unknown && result.player == target) known = result.role;
    if (known == Role::Unknown) return 0;
    if (const auto* r = roleReportAbout(message, target))
        return (r->role == Role::Villager ? !isEvilRole(known) : r->role == known) ? 1 : -1;
    if (const auto* c = factAbout(message, target); c && message.seerReport) return (c->weight > 0) == isEvilRole(known) ? 1 : -1;
    return 0;
}

bool Bot::repeats(uint8_t speaker, const dialogue::Message& message) const {
    auto same = [&](const Reply& r) { return r.speaker == speaker && r.intent == message.intent && r.topic == message.topic; };
    return std::any_of(m_replies.begin(), m_replies.end(), same) || (m_lastReply && same(*m_lastReply));
}

int Bot::replyPriority(const Match& match, uint8_t speaker, const Text& text, ChatChannel channel, const Speech* speech) const {
    const auto* me = match.find(m_id);
    const auto* from = match.find(speaker);
    if (!me || !from || speaker == m_id || !me->alive || !from->alive || match.phase() == Phase::GameOver ||
        !match.canHear(m_id, speaker, channel) || match.hidesSpeaker(m_id, channel)) return 0;
    if (match.chatChannel(m_id, secretChannel(channel)) != channel) return 0;
    // A busy bot still takes questions meant for it (they wait in line), but leaves small talk to others.
    const bool busy = !m_replies.empty() || match.clock() < m_replyAfter;
    if (busy && m_replies.size() >= 2) return 0;
    if (from->bot) {
        // Bots only respond to another bot's first-hand evidence, and never to a response.
        if (!speech || !speech->evidence || speech->reply) return 0;
        const Evidence& heard = *speech->evidence;
        const auto view = match.viewFor(m_id);
        if (heard.target == m_id) return heard.weight > 0 ? 90 : 0;
        if (heard.kind == Evidence::Kind::Seer && view.yourRole == Role::Seer) return 85; // that is my role
        dialogue::Message asMessage;
        asMessage.seerReport = heard.kind == Evidence::Kind::Seer;
        asMessage.claims.push_back({heard.target, heard.weight, true});
        if (wouldCounterclaim(match, asMessage, speaker)) return 85;
        if (knowsFirstHand(match, heard) && shouldReveal(match, heard.target)) return 60;
        if (heard.weight > 0 && relationship(heard.target) >= 0.45f && suspicion(match, heard.target) < 3.0f &&
            (heard.kind == Evidence::Kind::Opinion || heard.kind == Evidence::Kind::Sighting)) return 45;
        const auto* accused = view.player(heard.target);
        if (heard.weight > 0 && accused && ally(view, accused->role)) return 50; // cover for the pack
        if (heard.kind == Evidence::Kind::Seer && m_dialogue.otherSeer(speaker)) return 40;             // two seers: one is lying
        return 0;
    }
    const auto message = understand(match, speaker, text);
    const auto* addressed = match.find(message.addressed);
    const auto* topic = match.find(message.topic);
    const auto reported = reportedPlayers(message);
    const bool accused = std::any_of(message.claims.begin(), message.claims.end(),
                                     [&](const dialogue::Claim& c) { return c.target == m_id && c.weight > 0; });
    // Whoever is named or accused speaks up even when the message was addressed to someone else.
    int priority = 0;
    if (addressed && addressed->alive && message.addressed == m_id) {
        priority = 100;
    } else if (std::find(reported.begin(), reported.end(), m_id) != reported.end()) {
        priority = 95; // confirm or refute what was said about me
    } else if (accused) {
        priority = 90;
    } else if ((message.claimedRole == Role::Seer && match.viewFor(m_id).yourRole == Role::Seer) ||
               wouldCounterclaim(match, message, speaker)) {
        priority = 85; // that is my role – or a wolf pretends it is
    } else if (addressed && addressed->alive) {
        return 0;
    } else if (message.topic == m_id) {
        priority = 70;
    } else if (reportsFacts(message)) {
        priority = 40; // a seer's results deserve a reaction from the room
    } else if (addressed || (topic && !topic->alive)) {
        priority = 30; // someone should point out that this player is dead
    } else if (message.intent == dialogue::Intent::Unknown) {
        return 0;
    } else {
        priority = m_lastReply && m_lastReply->speaker == speaker && match.clock() - m_lastReply->due < 60.0 ? 20 : 10;
    }
    if (busy && (priority < 20 || repeats(speaker, message))) return 0;
    return priority;
}

void Bot::hear(const Match& match, uint8_t speaker, const Text& text, ChatChannel channel, bool reply, const Speech* speech) {
    const auto* me = match.find(m_id);
    const auto* from = match.find(speaker);
    if (!me || !from || !me->alive || !from->alive || speaker == m_id ||
        match.phase() == Phase::GameOver || !match.canHear(m_id, speaker, channel) || match.hidesSpeaker(m_id, channel)) return;
    // From bots only first-hand evidence counts, never their wording: a repeated rumour must not
    // turn into a new witness.
    lookAt(speaker, match.clock() + 3.0);
    // someone already said this; nodding along beats repeating it
    if (speech && speech->topic != kNoPlayer) {
        std::erase_if(m_planned, [&](const Planned& p) { return p.event == speech->topic; });
        m_heardTopic[speech->topic] = match.clock();
    }
    // A cry for help nearby at night: run as well, towards a door one can bolt.
    if (match.phase() == Phase::Night && !isWolfRole(match.viewFor(m_id).yourRole)) {
        if (speech && speech->alarm && speech->topic != kNoPlayer) {
            m_fleeFrom = speech->topic;
            m_fleeUntil = match.clock() + 5.0;
        } else if (!from->bot) {
            std::string lower = text.de.empty() ? text.en : text.de;
            for (char& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            const bool cry = lower.find("hilfe") != std::string::npos || lower.find("help") != std::string::npos ||
                             lower.find("lauft") != std::string::npos || lower.find("äuft") != std::string::npos ||
                             lower.find("renn") != std::string::npos || lower.find("flieh") != std::string::npos ||
                             lower.find("run") != std::string::npos;
            if (cry && lower.find("wolf") != std::string::npos) {
                m_goHome = true;
                m_staysOut = false;
                m_hesitate = 0.0f;
            }
        }
    }
    const Evidence* heard = from->bot && speech && speech->evidence ? &*speech->evidence : nullptr;
    if (from->bot && !heard) return;
    dialogue::Message message;
    if (heard) {
        message.intent = dialogue::Intent::Statement;
        message.claims.push_back({heard->target, heard->weight, heard->kind != Evidence::Kind::Opinion});
        message.mentions.push_back(heard->target);
        message.topic = heard->target;
        // A bot presenting seer results claims the seer, true or not; later reveals can expose a liar.
        message.seerReport = heard->kind == Evidence::Kind::Seer;
        if (message.seerReport) message.claimedRole = Role::Seer;
    } else {
        message = understand(match, speaker, text);
    }
    std::erase_if(message.claims, [&](const dialogue::Claim& c) {
        const auto* p = match.find(c.target);
        return !p || (!p->alive && !(message.seerReport && c.fact)); // a result about the dead is checked against the reveal
    });
    float before = 0;
    for (const auto& claim : message.claims) before += m_dialogue.suspicion(claim.target, match.day());
    // Naming a role that was revealed already proves nothing; only a wrong one tells us something.
    dialogue::Message remembered = message;
    {
        auto revealedAlready = [&](uint8_t id) {
            const auto* p = match.find(id);
            return p && !p->alive && verdict(match, message, id) > 0;
        };
        std::erase_if(remembered.claims, [&](const dialogue::Claim& c) { return c.fact && revealedAlready(c.target); });
        std::erase_if(remembered.roleReports, [&](const dialogue::RoleReport& r) { return revealedAlready(r.target); });
    }
    const bool contradiction = m_dialogue.remember(speaker, remembered, match.day());
    feelChat(match, speaker, remembered);
    if (heard && heard->kind == Evidence::Kind::Vouch) m_dialogue.support(heard->target);
    if (!message.claims.empty() || reportsFacts(message) || message.claimedRole != Role::Unknown) m_lastDiscussion = match.clock();
    const auto view = match.viewFor(m_id);
    if (reportsFacts(message) || message.claimedRole == Role::Seer) {
        m_dialogue.observeRevealed(view); // a wrong result about the dead is caught at once
        for (uint8_t target : reportedPlayers(remembered))
            if (const int v = verdict(match, remembered, target)) m_dialogue.judge(speaker, v > 0);
        if (message.claimedRole == Role::Seer && view.yourRole == Role::Seer) m_dialogue.judge(speaker, false);
        // A seer checks once per night: before the first night nobody, on day 2 at most one person.
        if (message.seerReport && m_dialogue.seerChecks(speaker) > nightsSoFar(match)) m_dialogue.judge(speaker, false);
    }
    // Wolves remember who is dangerous to the pack: whoever accuses one of them, a seer most of all.
    if (isEvilRole(view.yourRole))
        for (const auto& c : message.claims) {
            const auto* p = view.player(c.target);
            if (c.weight > 0 && (c.target == m_id || (p && ally(view, p->role)))) m_suspicion[speaker] += message.seerReport ? 3.0f : 1.0f;
        }
    float after = 0;
    for (const auto& claim : message.claims) after += m_dialogue.suspicion(claim.target, match.day());
    if (match.phase() == Phase::Vote && !match.election() && m_voted && match.timeLeft() > 4.0f &&
        match.clock() >= m_reconsiderAfter && std::abs(after - before) >= 0.65f) {
        m_voted = false;
        m_think = m_rng.uniform(1.5f, 3.0f);
        m_reconsiderAfter = match.clock() + 6.0;
    }
    if (message.topic != kNoPlayer) m_topics[speaker] = {message.topic, match.clock()};
    const int priority = reply ? replyPriority(match, speaker, text, channel, speech) : 0;
    if (priority == 0) return;
    std::optional<Evidence> evidence;
    Text response = styled(heard ? react(match, speaker, *heard, &evidence) : answer(match, speaker, message, contradiction, &evidence),
                           m_personality.style);
    if (response.empty()) return;
    // Wer dreimal dasselbe fragt, bekommt nicht dreimal denselben Satz
    const Reply* previous = !m_replies.empty() ? &m_replies.back() : m_lastReply ? &*m_lastReply : nullptr;
    if (previous && previous->speaker == speaker && previous->text == addressTo(from->name, response, match))
        response = styled({"Wie gesagt – dazu habe ich gerade nichts Neues.", "Like I said – nothing new from me on that right now."},
                          m_personality.style);
    // a wolf who answers with seer results has now claimed the seer
    if (evidence && evidence->kind == Evidence::Kind::Seer && isEvilRole(view.yourRole)) {
        m_claimedSeer = true;
        m_fakeReportDay = match.day();
    }
    if ((message.topic == kNoPlayer || message.topic == m_id) &&
        (message.intent == dialogue::Intent::AskSuspicion || message.intent == dialogue::Intent::AskVote)) {
        const uint8_t discussed = message.intent == dialogue::Intent::AskVote && match.phase() == Phase::Vote && me->vote != kNoPlayer
            ? me->vote : strongestSuspect(match, 1.0f);
        if (discussed != kNoPlayer) m_topics[speaker] = {discussed, match.clock()};
    }
    // Questions asked in quick succession are answered one after another, like a person typing.
    // After a seer's results, whoever is not named takes a moment longer, so replies do not arrive all at once.
    const float haste = 1.0f - 0.6f * m_feelings.stress;
    const float think = m_rng.uniform(1.2f, 2.8f) * haste + (reportsFacts(message) && priority < 90 ? m_rng.uniform(1.5f, 3.0f) : 0.0f);
    const double due = std::max(match.clock() + think, m_nextReply);
    m_nextReply = due + m_rng.uniform(2.5f, 3.5f) * haste;
    m_replies.push_back(Reply{speaker, channel, match.phase(), match.day(), due, addressTo(from->name, response, match),
                              message.intent, message.topic, Speech{evidence, true}});
}

Text Bot::explain(const Match& match, uint8_t target, std::optional<Evidence>* evidence) const {
    using Kind = Evidence::Kind;
    auto cite = [&](Kind kind, float weight) {
        if (evidence) *evidence = Evidence{kind, target, weight};
    };
    const auto* p = match.find(target);
    if (!p) return {"Ich habe noch keinen begründeten Verdacht. Was hast du beobachtet?",
                    "I don't have a solid suspect yet. What did you observe?"};
    if (target == m_id) return {"Ich bin kein Werwolf. Wenn du mich verdächtigst, sag mir bitte, warum.",
                                "I'm not a werewolf. If you suspect me, please tell me why."};
    const auto view = match.viewFor(m_id);
    for (const auto& result : view.seerResults) {
        if (result.player != target) continue;
        if (!shouldReveal(match, target))
            return {"Meine Einschätzung behalte ich vorerst für mich. Ich will erst sehen, wohin die Diskussion führt.",
                    "I'm keeping my assessment to myself for now. I want to see where the discussion goes first."};
        cite(Kind::Seer, isEvilRole(result.role) ? 2.4f : -1.8f);
        if (isVampireRole(result.role))
            return tx("Ich habe {} als Seherin geprüft: Vampir.", "I checked {} as the seer: vampire.", p->name);
        return isEvilRole(result.role)
            ? tx("Ich habe {} als Seherin geprüft: Werwolf.", "I checked {} as the seer: werewolf.", p->name)
            : tx("Meine Prüfung als Seherin hat bei {} keinen Werwolf ergeben.", "My seer check did not identify {} as a werewolf.", p->name);
    }
    if (const auto* known = view.player(target); known && known->role != Role::Unknown && !isEvilRole(known->role)) {
        cite(Kind::KnownRole, -1.0f);
        return tx("Die Rolle von {} ist mir bekannt. Ich verdächtige diese Person nicht als Werwolf.",
                  "I know {}'s role. I don't suspect this person of being a werewolf.", p->name);
    }
    if (auto seen = m_vampireWitness.find(target); seen != m_vampireWitness.end()) {
        cite(Kind::Witness, 2.4f);
        return tx("Ich habe {} in Nacht {} mit rot glühenden Augen gesehen – ein Vampir!",
                  "On night {1} I saw {0} with glowing red eyes – a vampire!", p->name, seen->second);
    }
    if (auto seen = m_wolfWitness.find(target); seen != m_wolfWitness.end()) {
        cite(Kind::Witness, 2.4f);
        return tx("Ich habe {} in Nacht {} selbst bei der Verwandlung erkannt.",
                  "I personally recognised {} transforming on night {}.", p->name, seen->second);
    }
    const float heard = m_dialogue.suspicion(target, match.day());
    if (const auto* source = match.find(m_dialogue.source(target, match.day())); source && heard > 0.2f) {
        if (heard > 1.8f)
            return tx("{} hat {} schwer belastet. Ich nehme das ernst, auch wenn ich es nicht selbst gesehen habe.",
                      "{} made a serious case against {}. I take it seriously, even though I didn't see it myself.",
                      source->name, p->name);
        return tx("{} hat {} beschuldigt. Das ist für mich ein Hinweis, noch kein Beweis. Gibt es einen weiteren Zeugen?",
                  "{} accused {}. That's a lead, not proof. Is there another witness?", source->name, p->name);
    }
    if (suspicion(match, target) > 1.0f)
        if (const auto it = m_resentment.find(target); it != m_resentment.end() && it->second > 0.2f)
            return tx("Nach dem Fehlurteil misstraue ich {}. Ich bin noch wütend, aber das allein beweist keine Wolfsrolle.",
                      "After that wrongful execution, I distrust {}. I'm still angry, but that alone doesn't prove they're a wolf.", p->name);
    if (suspicion(match, target) > 1.0f)
        return tx("Meine Beobachtungen im Spiel sprechen gegen {}. Ganz sicher bin ich mir noch nicht.",
                  "My observations in the game point towards {}. I'm not certain yet.", p->name);
    return tx("Gegen {} habe ich bisher keinen belastbaren Hinweis. Ein Name allein reicht mir nicht.",
              "I don't have solid evidence against {} so far. A name alone isn't enough.", p->name);
}

Text Bot::react(const Match& match, uint8_t speaker, const Evidence& heard, std::optional<Evidence>* evidence) const {
    using Kind = Evidence::Kind;
    const auto view = match.viewFor(m_id);
    dialogue::Message asMessage;
    asMessage.seerReport = heard.kind == Kind::Seer;
    asMessage.claims.push_back({heard.target, heard.weight, true});
    if (wouldCounterclaim(match, asMessage, speaker)) return counterclaim(match, speaker, evidence);
    if (heard.target == m_id) {
        if (heard.weight > 0) if (const Text hint = roleHint(match); !hint.empty()) return hint;
        // Not every accused bot should deny with the same sentence.
        const size_t pick = static_cast<size_t>(m_id + match.day());
        if (heard.kind == Kind::Seer && !isEvilRole(view.yourRole))
            return {"Das ist gelogen – ich bin kein Werwolf. Wer so etwas behauptet, ist wahrscheinlich selbst einer.",
                    "That's a lie – I'm not a werewolf. Whoever claims that is probably one."};
        if (heard.kind == Kind::Sighting) {
            const std::array<Text, 2> lines = {{{"Ich war nur auf dem Heimweg.", "I was just on my way home."},
                {"Da war ich, ja – aber ich habe niemandem etwas getan.", "I was there, yes – but I didn't hurt anyone."}}};
            return lines[pick % lines.size()];
        }
        if (heard.kind == Kind::Opinion)
            return {"Dein Vorwurf trifft mich. Ich kann mich geirrt haben, aber ich habe nach meiner Einschätzung gehandelt!",
                    "That accusation hurts. I may have been wrong, but I acted on my own judgment!"};
        if (heard.kind == Kind::Seer) {
            const std::array<Text, 2> lines = {{{"Das ist gelogen. Bist du überhaupt die Seherin?", "That's a lie. Are you even the seer?"},
                {"Eine Seherin, die erst jetzt auftaucht? Das glaube ich nicht.", "A seer who only shows up now? I don't believe that."}}};
            return lines[pick % lines.size()];
        }
        if (heard.kind == Kind::Witness) {
            const std::array<Text, 3> lines = {{{"Das stimmt nicht. Im Dunkeln hast du dich getäuscht.",
                                                 "That's not true. You must have been mistaken in the dark."},
                {"Ich? Ich war die ganze Nacht nicht in deiner Nähe.", "Me? I wasn't anywhere near you all night."},
                {"Bei dem Licht erkennt man doch kein Gesicht. Das war jemand anderes.",
                 "Nobody can make out a face in that light. That was someone else."}}};
            return lines[pick % lines.size()];
        }
        return {"Das stimmt nicht! Ich bin kein Werwolf.", "That's not true! I'm not a werewolf."};
    }
    const auto* target = match.find(heard.target);
    if (!target) return {};
    if (heard.target == sacrificeTarget(match))
        return tx("Ich halte nicht mehr zu {}. Die Vorwürfe wiegen zu schwer. Ich stimme dagegen.",
                  "I'm no longer standing by {}. The accusations are too serious. I'm voting against them.", target->name);
    if (heard.weight > 0 && relationship(heard.target) >= 0.45f && suspicion(match, heard.target) < 3.0f &&
        (heard.kind == Kind::Opinion || heard.kind == Kind::Sighting)) {
        if (evidence) *evidence = Evidence{Kind::Opinion, heard.target, -0.4f};
        return tx("{} zählt zu meinen Freunden. Bevor ich mich gegen diese Person stelle, will ich Belege sehen.",
                  "{} is my friend. Before I turn against them, I want to see evidence.", target->name);
    }
    if (heard.kind == Kind::Seer && view.yourRole == Role::Seer) {
        if (view.seerResults.empty()) return {"Moment – ich bin die Seherin, nicht du. Du lügst.", "Hold on – I'm the seer, not you. You're lying."};
        return tx("Moment – ich bin die Seherin, nicht du. {}", "Hold on – I'm the seer, not you. {}",
                  explain(match, view.seerResults.back().player, evidence));
    }
    if (knowsFirstHand(match, heard))
        return tx("Das kann ich bestätigen. {}", "I can confirm that. {}", explain(match, heard.target, evidence));
    if (heard.kind == Kind::Seer && m_dialogue.otherSeer(speaker) && !ally(view, target->role))
        return {"Zwei Seherinnen? Eine von euch lügt – und wer sich erst nach einer Beschuldigung meldet, macht sich verdächtig.",
                "Two seers? One of you is lying – and whoever only speaks up after being accused looks suspicious."};
    return tx("Bist du dir sicher? {} kam mir bisher harmlos vor.", "Are you sure? {} has seemed harmless to me so far.", target->name);
}

Text Bot::answer(const Match& match, uint8_t speaker, const dialogue::Message& message, bool contradiction,
                 std::optional<Evidence>* evidence) const {
    using dialogue::Intent;
    const auto* me = match.find(m_id);
    if (!me) return {};
    const auto view = match.viewFor(m_id);
    const auto* topic = match.find(message.topic);
    if (contradiction) return {"Das widerspricht deiner früheren Aussage. Was hat deine Einschätzung geändert?",
                              "That contradicts your earlier statement. What changed your mind?"};
    if (message.seerReport && m_dialogue.seerChecks(speaker) > nightsSoFar(match)) {
        const int nights = nightsSoFar(match);
        if (nights <= 0)
            return {"Moment – es gab noch gar keine Nacht. Die Seherin kann noch niemanden geprüft haben. Du lügst.",
                    "Hold on – there hasn't been a night yet. The seer can't have checked anyone. You're lying."};
        if (nights == 1)
            return {"Moment – es gab erst eine Nacht, und die Seherin prüft nur einmal pro Nacht. So viele Ergebnisse kannst du nicht haben.",
                    "Hold on – there has only been one night, and the seer checks once per night. You can't have that many results."};
        return tx("Moment – es gab erst {} Nächte, und die Seherin prüft nur einmal pro Nacht. So viele Ergebnisse kannst du nicht haben.",
                  "Hold on – there have only been {} nights, and the seer checks once per night. You can't have that many results.", nights);
    }
    if (std::any_of(message.claims.begin(), message.claims.end(), [&](const dialogue::Claim& c) { return c.target == m_id && c.weight > 0; })) {
        if (const Text hint = roleHint(match); !hint.empty()) return hint;
    }
    // Someone else claims my role: one of us is lying, and I know who.
    if (message.claimedRole == Role::Seer && view.yourRole == Role::Seer)
        return {"Moment – ich bin die Seherin, nicht du. Du lügst.", "Hold on – I'm the seer, not you. You're lying."};
    // A wolf whose pack was exposed claims the seer for itself and turns the accusation around.
    if (wouldCounterclaim(match, message, speaker)) return counterclaim(match, speaker, evidence);
    // A result or role named for me: I know whether it is true.
    const auto reported = reportedPlayers(message);
    if (std::find(reported.begin(), reported.end(), m_id) != reported.end()) {
        const auto* fact = factAbout(message, m_id);
        const bool calledWolf = fact && fact->weight > 0;
        if (isEvilRole(view.yourRole))
            return calledWolf ? Text{"Das stimmt nicht! Du lügst, ich bin kein Werwolf.", "That's not true! You're lying, I'm not a werewolf."}
                              : Text{"Stimmt, ich stehe auf eurer Seite.", "That's right, I'm on your side."};
        if (verdict(match, message, m_id) > 0) {
            if (evidence) *evidence = Evidence{Evidence::Kind::Vouch, speaker, -1.5f};
            if (roleReportAbout(message, m_id))
                return tx("Stimmt, ich bin {}. Du sagst die Wahrheit.", "That's right, I'm {}. You're telling the truth.",
                          roleWithArticle(view.yourRole));
            return {"Stimmt, ich bin kein Werwolf. Du sagst die Wahrheit.", "That's right, I'm not a werewolf. You're telling the truth."};
        }
        if (calledWolf)
            return {"Das ist gelogen – ich bin kein Werwolf. Wer so etwas behauptet, ist wahrscheinlich selbst einer.",
                    "That's a lie – I'm not a werewolf. Whoever claims that is probably one."};
        return {"Nein, das stimmt nicht – meine Rolle ist eine andere. Du lügst.", "No, that's not right – my role is a different one. You're lying."};
    }
    for (const auto& c : message.claims) {
        if (c.target == m_id && c.weight > 0) {
            if (relationship(speaker) < -0.55f)
                return {"Schon wieder du! Deine ständigen Beschuldigungen machen mich wütend. Ich traue dir nicht mehr. Zeig endlich Beweise!",
                        "You again! Your constant accusations make me angry. I don't trust you any more. Show some evidence!"};
            return {"Ich widerspreche deiner Beschuldigung. Was genau hast du gesehen, und in welcher Nacht?",
                    "I dispute that accusation. What exactly did you see, and on which night?"};
        }
    }
    // A result about someone already revealed: everybody can check it, but a right one proves nothing.
    for (uint8_t target : reported) {
        const auto* p = match.find(target);
        const auto* known = view.player(target);
        if (!p || p->alive || !known || known->role == Role::Unknown) continue;
        if (verdict(match, message, target) > 0)
            return tx("{} war ja schon aufgedeckt – das beweist leider nichts.", "{} had already been revealed – that doesn't prove anything.", p->name);
        return tx("{} war aber {}. Da stimmt etwas nicht.", "But {} was {}. Something doesn't add up.", p->name, roleWithArticle(known->role));
    }
    if (reportsFacts(message)) {
        if (m_dialogue.liar(speaker)) return {"Dir glaube ich nichts mehr. Deine Angaben haben nicht gestimmt.",
                                              "I don't believe you any more. Your claims didn't hold up."};
        if (message.claimedRole == Role::Seer && m_dialogue.otherSeer(speaker))
            return {"Zwei Seherinnen? Eine von euch lügt. Was habt ihr beide in welcher Nacht geprüft?",
                    "Two seers? One of you is lying. What did each of you check, and on which night?"};
        const dialogue::Claim* wolfResult = nullptr;
        const dialogue::Claim* goodResult = nullptr;
        for (const auto& c : message.claims) {
            const auto* p = match.find(c.target);
            if (!c.fact || !p || !p->alive) continue;
            if (c.weight > 0 && !wolfResult) wolfResult = &c;
            if (c.weight < 0 && !goodResult) goodResult = &c;
        }
        if (wolfResult) {
            const std::string& accused = match.find(wolfResult->target)->name;
            const auto* known = view.player(wolfResult->target);
            if (wolfResult->target == sacrificeTarget(match))
                return tx("Ich werde {} nicht länger decken. Ich stimme ebenfalls dagegen.",
                          "I won't cover for {} any longer. I'm voting against them too.", accused);
            if (known && ally(view, known->role))
                return {"Das kann jeder behaupten. Wer bestätigt, dass du wirklich die Seherin bist?",
                        "Anyone can claim that. Who confirms you're really the seer?"};
            if (m_dialogue.confirmed(speaker) > 0)
                return tx("Deine Angaben haben bisher gestimmt. Ich stimme heute für {}.", "Your claims have held up so far. I'm voting for {} today.", accused);
            if (m_dialogue.credibility(speaker) >= 0.75f)
                return tx("Okay. Wenn das stimmt, sollten wir heute {} wählen.", "Okay. If that's true, we should vote for {} today.", accused);
            return {"Kann das jemand bestätigen? Seherin kann jeder spielen.", "Can anyone confirm that? Anyone can play the seer."};
        }
        if (goodResult)
            return tx("Gut zu wissen. Dann lassen wir {} erst einmal in Ruhe.", "Good to know. Then we'll leave {} alone for now.",
                      match.find(goodResult->target)->name);
    }
    auto dead = [&](const Match::Player& p) {
        if (const auto* known = view.player(p.id); known && known->role != Role::Unknown)
            return tx("{} ist schon tot – die Rolle war {}.", "{} is already dead – their role was {}.", p.name, roleText(known->role));
        return tx("{} ist schon tot.", "{} is already dead.", p.name);
    };
    // "Anton, wo warst du?" to a dead Anton must not be answered as if it were meant for me.
    if (const auto* addressee = match.find(message.addressed); addressee && !addressee->alive) return dead(*addressee);
    if (topic && !topic->alive) {
        if (!message.question) return dead(*topic);
        topic = nullptr; // a question about the dead: talk about the living instead
    }
    // "Anton, was denkst du?" asks for my opinion, not about me.
    const bool aboutMe = topic && topic->id == m_id;
    const auto* other = aboutMe ? nullptr : topic;
    switch (message.intent) {
    case Intent::AskBelief:
        if (other) return explain(match, other->id, evidence);
        if (isEvilRole(view.yourRole)) return {"Weil das jeder behaupten kann.", "Because anyone can claim that."};
        if (m_dialogue.liar(speaker)) return {"Weil deine Angaben nicht gestimmt haben.", "Because your claims didn't hold up."};
        if (m_dialogue.confirmed(speaker) > 0 || m_dialogue.credibility(speaker) >= 1.0f)
            return {"Ich glaube dir. Deine Angaben haben bisher gestimmt.", "I believe you. Your claims have held up so far."};
        return {"Noch hat niemand deine Angaben bestätigt. Wer kann für dich sprechen?",
                "Nobody has confirmed your claims yet. Who can vouch for you?"};
    case Intent::AskReason:
        return explain(match, other ? other->id : strongestSuspect(match, 1.0f), evidence);
    case Intent::AskInnocence:
        if (aboutMe) return {"Ja. Ich bin kein Werwolf, auch wenn ich das nicht beweisen kann.",
                             "Yes. I'm not a werewolf, even if I can't prove it."};
        if (other) return explain(match, other->id, evidence);
        for (const auto& p : view.players)
            if (p.alive && p.id != m_id && p.role != Role::Unknown && !isEvilRole(p.role)) return explain(match, p.id, evidence);
        for (const auto& p : view.players)
            if (p.alive && p.id != m_id && suspicion(match, p.id) < -0.5f)
                return tx("{} wurde im Gespräch entlastet. Bewiesen ist die Unschuld damit noch nicht.",
                          "{} was defended in the conversation. That doesn't prove innocence yet.", p.name);
        return {"Ich kann bisher niemanden sicher entlasten. Hast du eine überprüfbare Beobachtung?",
                "I can't confidently clear anyone yet. Do you have an observation we can verify?"};
    case Intent::AskSuspicion:
    case Intent::AskVote: {
        const uint8_t suspect = other ? other->id : strongestSuspect(match, 1.0f);
        if (message.intent == Intent::AskVote && me->vote != kNoPlayer && match.phase() == Phase::Vote) {
            if (const auto* voted = match.find(me->vote))
                return tx("Meine Stimme geht an {}. {}", "My vote goes to {}. {}", voted->name, explain(match, voted->id, evidence));
        }
        return explain(match, suspect, evidence);
    }
    case Intent::AskAlibi: {
        if (other)
            return tx("Wo {} die ganze Nacht war, kann ich nicht belegen. Frag nach einem Zeugen.",
                      "I can't account for {}'s whole night. Ask for a witness.", other->name);
        if (isEvilRole(me->role)) return {"Ich war unterwegs. Ein lückenloses Alibi habe ich nicht.",
                                         "I was out and about. I don't have a complete alibi."};
        auto most = std::max_element(m_nightPlaces.begin(), m_nightPlaces.end());
        if (m_alibiNight < 0 || *most < 1.0f) return {"Ich habe noch keine vergangene Nacht, für die ich ein Alibi nennen kann.",
                                                    "There hasn't been a night I can give an alibi for yet."};
        const size_t place = static_cast<size_t>(most - m_nightPlaces.begin());
        const std::array<Text, 6> places = {{{"unterwegs", "out and about"}, {"in meinem Haus", "in my house"},
            {"in der Kapelle", "in the chapel"}, {"am Lagerfeuer", "at the campfire"}, {"am Brunnen", "at the well"},
            {"im Wald", "in the forest"}}};
        return tx("In Nacht {} war ich überwiegend {}. Das deckt nicht jeden Moment ab.",
                  "On night {} I was mostly {}. That doesn't account for every moment.", m_alibiNight, places[place]);
    }
    case Intent::AskRole:
        if (other) return explain(match, other->id, evidence);
        if (aboutMe && message.wolfWord) return {"Nein, ich bin kein Werwolf.", "No, I'm not a werewolf."};
        if (me->role == Role::Seer && !view.seerResults.empty()) return explain(match, view.seerResults.back().player, evidence);
        if (m_feelings.stress >= 0.3f) if (const Text hint = roleHint(match); !hint.empty()) return hint;
        return {"Meine Rolle behalte ich vorerst für mich. Über konkrete Beobachtungen können wir sprechen.",
                "I'm keeping my role to myself for now. We can discuss specific observations."};
    case Intent::RoleClaim:
        if (message.claimedRole == Role::Seer)
            return {"Du behauptest, die Seherin zu sein. Wen hast du in welcher Nacht geprüft, und was war das Ergebnis?",
                    "You claim to be the seer. Who did you check, on which night, and what was the result?"};
        return {"Ich merke mir deine Rollenbehauptung. Sie macht dich für mich noch nicht automatisch unschuldig.",
                "I'll remember your role claim. It doesn't automatically clear you."};
    case Intent::Alibi:
        return {"Ich habe dein Alibi gehört. Wer kann bestätigen, dass du dort warst?",
                "I've heard your alibi. Who can confirm you were there?"};
    case Intent::Greeting:
        return {"Hallo! Hast du etwas Verdächtiges gesehen? Nenne mir die Person und deine Beobachtung.",
                "Hello! Have you seen anything suspicious? Tell me who and what you observed."};
    case Intent::Statement:
        for (const auto& c : message.claims) {
            const auto* accused = match.find(c.target);
            if (!accused) continue;
            if (c.target == m_id) return {"Danke für dein Vertrauen.", "Thanks for trusting me."};
            if (c.weight < 0) {
                if (suspicion(match, c.target) >= 4.0f)
                    return tx("Deine Entlastung von {} ist angekommen. Meine eigenen Hinweise dagegen sind aber stärker. {}",
                              "I've heard your defence of {}. My own contrary evidence is stronger, though. {}",
                              accused->name, explain(match, c.target, evidence));
                return tx("Ich berücksichtige deine Entlastung von {}. Wie kannst du das belegen?",
                          "I'll take your defence of {} into account. How can you back it up?", accused->name);
            }
            const auto* known = view.player(c.target);
            if (relationship(c.target) >= 0.45f && c.weight < 3.0f && suspicion(match, c.target) < 3.0f &&
                (!known || known->role == Role::Unknown)) {
                if (evidence) *evidence = Evidence{Evidence::Kind::Opinion, c.target, -0.4f};
                return tx("{} zählt zu meinen Freunden. Eine bloße Vermutung reicht mir nicht. Welche Belege hast du?",
                          "{} is my friend. I won't abandon them over a hunch. What evidence do you have?", accused->name);
            }
            if (c.target == sacrificeTarget(match))
                return tx("Auch ich stimme gegen {}. Diese Vorwürfe lassen sich nicht mehr wegreden.",
                          "I'm voting against {} too. These accusations can't be dismissed any more.", accused->name);
            if (known && ally(view, known->role))
                return tx("Die Beschuldigung gegen {} überzeugt mich nicht. Hast du mehr als einen Verdacht?",
                          "I'm not convinced by the accusation against {}. Do you have more than a suspicion?", accused->name);
            return tx("Ich merke mir deinen Verdacht gegen {}. {}", "I'll remember your suspicion of {}. {}",
                      accused->name, c.weight >= 2.3f
                          ? Text{"Wann und wo hast du das beobachtet?", "When and where did you observe that?"}
                          : Text{"Was hast du konkret beobachtet?", "What exactly did you observe?"});
        }
        break;
    default: break;
    }
    (void)speaker;
    return {"Das kann ich noch nicht eindeutig zuordnen. Wen meinst du, und was hast du gesehen?",
            "I'm not sure what you mean yet. Who are you talking about, and what did you see?"};
}

void Bot::speak(const SayFn& say, const Text& text, bool secret, Speech speech) {
    if (speech.evidence) lookAt(speech.evidence->target, m_now + 3.0); // den ansehen, über den man spricht
    if (speech.evidence && speech.evidence->kind == Evidence::Kind::Seer) {
        m_seerRevealed = true;
        m_sharedResults.insert(speech.evidence->target);
    }
    if (dialogue::parse(text.de, {}, m_id, m_id).claimedRole == Role::Seer) m_seerRevealed = true;
    const Text line = emotionalStyle(speech.reply ? text : styled(text, m_personality.style));
    m_speaking = std::move(speech);
    say(m_id, line, secret);
    m_speaking.reset();
}

void Bot::updateConversation(float dt, const Match& match, const SayFn& say) {
    const auto* me = match.find(m_id);
    if (!me) return;
    if (me->alive && match.phase() == Phase::Night) {
        if (m_alibiNight != match.day()) { m_nightPlaces.fill(0); m_alibiNight = match.day(); }
        dialogue::Place place = dialogue::Place::Unknown;
        const int house = match.world().houseAt({me->position.x, me->position.z});
        if (house >= 0 && house == me->house) place = dialogue::Place::Home;
        else if (house >= 0 && house == match.world().chapelIndex()) place = dialogue::Place::Chapel;
        else if (distanceXZ(me->position, {0, 0, 0}) < 9) place = dialogue::Place::Fire;
        else if (distanceXZ(me->position, match.world().well()) < 7) place = dialogue::Place::Well;
        else if (Vec2{me->position.x, me->position.z}.length() > 48) place = dialogue::Place::Forest;
        m_nightPlaces[static_cast<size_t>(place)] += dt;
    }
    if (!me->alive) m_replies.clear();
    while (!m_replies.empty() && (m_replies.front().phase != match.phase() || m_replies.front().day != match.day()))
        m_replies.pop_front();
    if (m_replies.empty() || match.clock() < m_replies.front().due) return;
    Reply pending = std::move(m_replies.front());
    m_replies.pop_front(); // before say() re-enters the server's chat routing
    const auto* from = match.find(pending.speaker);
    const bool secret = secretChannel(pending.channel);
    if (!from || !from->alive || match.chatChannel(m_id, secret) != pending.channel ||
        !match.canHear(pending.speaker, m_id, pending.channel) ||
        (pending.channel == ChatChannel::Nearby && me->wolfForm())) return;
    m_replyAfter = match.clock() + 5.0;
    pending.due = match.clock();
    m_lastReply = pending;
    m_chatTimer = -1.0f;
    speak(say, pending.text, secret, pending.speech);
}

void deliverChat(std::vector<Bot>& bots, const Match& match, uint8_t speaker, const Text& text, ChatChannel channel,
                 eng::Rng& rng) {
    std::optional<Bot::Speech> speech;
    for (const Bot& bot : bots)
        if (bot.id() == speaker && bot.speaking()) speech = *bot.speaking();
    const Bot::Speech* said = speech ? &*speech : nullptr;
    struct Candidate { uint8_t id; int priority; float tie; };
    std::vector<Candidate> candidates;
    for (const Bot& bot : bots)
        if (const int priority = bot.replyPriority(match, speaker, text, channel, said); priority > 0)
            candidates.push_back({bot.id(), priority, rng.uniform()});
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.priority != b.priority ? a.priority > b.priority : a.tie < b.tie; });
    // The best match answers; everyone else who is named or accused speaks up as well. When that
    // happened (a seer named someone), one more voice from the room reacts.
    std::vector<uint8_t> responders;
    for (const auto& c : candidates)
        if (responders.empty() || (c.priority >= 80 && responders.size() < 3)) responders.push_back(c.id);
    if (!candidates.empty() && candidates.front().priority >= 80 && responders.size() < 3)
        for (const auto& c : candidates)
            if (c.priority == 40 || c.priority == 45) { responders.push_back(c.id); break; }
    for (Bot& bot : bots)
        if (match.canHear(bot.id(), speaker, channel) && !match.hidesSpeaker(bot.id(), channel))
            bot.hear(match, speaker, text, channel, std::find(responders.begin(), responders.end(), bot.id()) != responders.end(), said);
}

} // namespace ww
