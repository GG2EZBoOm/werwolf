#include "server/Bot.h"

#include <algorithm>

namespace ww {
namespace {

bool secretChannel(ChatChannel channel) {
    return channel == ChatChannel::Wolves || channel == ChatChannel::Family || channel == ChatChannel::Dead;
}

// "Mia, die Rolle …" rather than "Mia, Die Rolle …"; "Ich", "I" and player names keep their capital.
std::string lowerFirst(std::string s, const Match& match) {
    if (s.empty() || s[0] < 'A' || s[0] > 'Z') return s;
    const std::string word = s.substr(0, s.find_first_of(" ,.!?:;'"));
    if (word == "Ich" || word == "I") return s;
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

} // namespace

float Bot::suspicion(const Match& match, uint8_t target) const {
    const auto own = m_suspicion.find(target);
    return (own == m_suspicion.end() ? 0.0f : own->second) + m_dialogue.suspicion(target, match.day());
}

dialogue::Message Bot::understand(const Match& match, uint8_t speaker, const Text& text) const {
    std::vector<dialogue::Person> people;
    for (const auto& p : match.players()) people.push_back({p.id, p.name});
    uint8_t topic = kNoPlayer;
    if (auto it = m_topics.find(speaker); it != m_topics.end() && match.clock() - it->second.time < 60.0) topic = it->second.player;
    return dialogue::parse(text.de.empty() ? text.en : text.de, people, speaker, m_id, topic);
}

bool Bot::knowsFirstHand(const Match& match, const Evidence& heard) const {
    for (const auto& result : match.viewFor(m_id).seerResults)
        if (result.player == heard.target && isWolfRole(result.role) == (heard.weight > 0)) return true;
    return heard.weight > 0 && m_wolfWitness.contains(heard.target);
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
        if (heard.target == m_id) return heard.weight > 0 ? 90 : 0;
        if (knowsFirstHand(match, heard)) return 60;
        const auto view = match.viewFor(m_id);
        const auto* accused = view.player(heard.target);
        if (heard.weight > 0 && isWolfRole(view.yourRole) && accused && isWolfRole(accused->role)) return 50; // cover for the pack
        return 0;
    }
    const auto message = understand(match, speaker, text);
    const auto* addressed = match.find(message.addressed);
    const auto* topic = match.find(message.topic);
    int priority = 0;
    if (addressed && addressed->alive) {
        if (message.addressed != m_id) return 0;
        priority = 100;
    } else if (std::any_of(message.claims.begin(), message.claims.end(),
                           [&](const dialogue::Claim& c) { return c.target == m_id && c.weight > 0; })) {
        priority = 90;
    } else if (message.topic == m_id) {
        priority = 70;
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
    const Evidence* heard = from->bot && speech && speech->evidence ? &*speech->evidence : nullptr;
    if (from->bot && !heard) return;
    dialogue::Message message;
    if (heard) {
        message.intent = dialogue::Intent::Statement;
        message.claims.push_back({heard->target, heard->weight});
        message.mentions.push_back(heard->target);
        message.topic = heard->target;
    } else {
        message = understand(match, speaker, text);
    }
    std::erase_if(message.claims, [&](const dialogue::Claim& c) {
        const auto* p = match.find(c.target);
        return !p || !p->alive;
    });
    float before = 0;
    for (const auto& claim : message.claims) before += m_dialogue.suspicion(claim.target, match.day());
    const bool contradiction = m_dialogue.remember(speaker, message, match.day());
    float after = 0;
    for (const auto& claim : message.claims) after += m_dialogue.suspicion(claim.target, match.day());
    if (match.phase() == Phase::Vote && !match.election() && m_voted && match.timeLeft() > 4.0f &&
        match.clock() >= m_reconsiderAfter && std::abs(after - before) >= 0.65f) {
        m_voted = false;
        m_think = m_rng.uniform(1.5f, 3.0f);
        m_reconsiderAfter = match.clock() + 6.0;
    }
    if (message.topic != kNoPlayer) m_topics[speaker] = {message.topic, match.clock()};
    if (!reply || replyPriority(match, speaker, text, channel, speech) == 0) return;
    std::optional<Evidence> evidence;
    const Text response = heard ? react(match, *heard, &evidence) : answer(match, speaker, message, contradiction, &evidence);
    if (response.empty()) return;
    if ((message.topic == kNoPlayer || message.topic == m_id) &&
        (message.intent == dialogue::Intent::AskSuspicion || message.intent == dialogue::Intent::AskVote)) {
        const uint8_t discussed = message.intent == dialogue::Intent::AskVote && match.phase() == Phase::Vote && me->vote != kNoPlayer
            ? me->vote : strongestSuspect(match, 1.0f);
        if (discussed != kNoPlayer) m_topics[speaker] = {discussed, match.clock()};
    }
    // Questions asked in quick succession are answered one after another, like a person typing.
    const double due = std::max(match.clock() + m_rng.uniform(1.2f, 2.8f), m_nextReply);
    m_nextReply = due + m_rng.uniform(2.5f, 3.5f);
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
        cite(Kind::Seer, isWolfRole(result.role) ? 2.4f : -1.8f);
        return isWolfRole(result.role)
            ? tx("Ich habe {} als Seherin geprüft: Werwolf.", "I checked {} as the seer: werewolf.", p->name)
            : tx("Meine Prüfung als Seherin hat bei {} keinen Werwolf ergeben.", "My seer check did not identify {} as a werewolf.", p->name);
    }
    if (const auto* known = view.player(target); known && known->role != Role::Unknown && !isWolfRole(known->role)) {
        cite(Kind::KnownRole, -1.0f);
        return tx("Die Rolle von {} ist mir bekannt. Ich verdächtige diese Person nicht als Werwolf.",
                  "I know {}'s role. I don't suspect this person of being a werewolf.", p->name);
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
        return tx("Meine Beobachtungen im Spiel sprechen gegen {}. Ganz sicher bin ich mir noch nicht.",
                  "My observations in the game point towards {}. I'm not certain yet.", p->name);
    return tx("Gegen {} habe ich bisher keinen belastbaren Hinweis. Ein Name allein reicht mir nicht.",
              "I don't have solid evidence against {} so far. A name alone isn't enough.", p->name);
}

Text Bot::react(const Match& match, const Evidence& heard, std::optional<Evidence>* evidence) const {
    using Kind = Evidence::Kind;
    if (heard.target == m_id) {
        // Not every accused bot should deny with the same sentence.
        const size_t pick = static_cast<size_t>(m_id + match.day());
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
    if (knowsFirstHand(match, heard))
        return tx("Das kann ich bestätigen. {}", "I can confirm that. {}", explain(match, heard.target, evidence));
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
    for (const auto& c : message.claims) {
        if (c.target == m_id && c.weight > 0)
            return {"Ich widerspreche deiner Beschuldigung. Was genau hast du gesehen, und in welcher Nacht?",
                    "I dispute that accusation. What exactly did you see, and on which night?"};
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
    case Intent::AskReason:
        return explain(match, other ? other->id : strongestSuspect(match, 1.0f), evidence);
    case Intent::AskInnocence:
        if (aboutMe) return {"Ja. Ich bin kein Werwolf, auch wenn ich das nicht beweisen kann.",
                             "Yes. I'm not a werewolf, even if I can't prove it."};
        if (other) return explain(match, other->id, evidence);
        for (const auto& p : view.players)
            if (p.alive && p.id != m_id && p.role != Role::Unknown && !isWolfRole(p.role)) return explain(match, p.id, evidence);
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
        if (isWolfRole(me->role)) return {"Ich war unterwegs. Ein lückenloses Alibi habe ich nicht.",
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
            if (isWolfRole(me->role) && known && isWolfRole(known->role))
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
    m_speaking = std::move(speech);
    say(m_id, text, secret);
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
    int priority = 0;
    std::vector<uint8_t> responders;
    for (const Bot& bot : bots) {
        const int candidate = bot.replyPriority(match, speaker, text, channel, said);
        if (candidate > priority) { responders.clear(); priority = candidate; }
        if (candidate > 0 && candidate == priority) responders.push_back(bot.id());
    }
    const uint8_t responder = responders.empty() ? kNoPlayer : rng.pick(responders);
    for (Bot& bot : bots)
        if (match.canHear(bot.id(), speaker, channel) && !match.hidesSpeaker(bot.id(), channel))
            bot.hear(match, speaker, text, channel, bot.id() == responder, said);
}

} // namespace ww
