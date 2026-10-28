#include "server/Bot.h"

#include <algorithm>

namespace ww {

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

int Bot::replyPriority(const Match& match, uint8_t speaker, const Text& text, ChatChannel channel) const {
    const auto* me = match.find(m_id);
    const auto* from = match.find(speaker);
    if (!me || !from || from->bot || speaker == m_id || !me->alive || !from->alive ||
        m_reply || match.clock() < m_replyAfter || match.phase() == Phase::GameOver ||
        !match.canHear(m_id, speaker, channel) || match.hidesSpeaker(m_id, channel)) return 0;
    const bool secret = channel == ChatChannel::Wolves || channel == ChatChannel::Family || channel == ChatChannel::Dead;
    if (match.chatChannel(m_id, secret) != channel) return 0;
    const auto message = understand(match, speaker, text);
    if (message.addressed != kNoPlayer) return message.addressed == m_id ? 100 : 0;
    for (const auto& c : message.claims)
        if (c.target == m_id && c.weight > 0) return 90;
    if (message.topic == m_id) return 70;
    if (message.intent == dialogue::Intent::Unknown) return 0;
    return m_lastReplyTo.player == speaker && match.clock() - m_lastReplyTo.time < 60.0 ? 20 : 10;
}

void Bot::hear(const Match& match, uint8_t speaker, const Text& text, ChatChannel channel, bool reply) {
    const auto* me = match.find(m_id);
    const auto* from = match.find(speaker);
    if (!me || !from || !me->alive || !from->alive || from->bot || speaker == m_id ||
        match.phase() == Phase::GameOver || !match.canHear(m_id, speaker, channel) || match.hidesSpeaker(m_id, channel)) return;
    // Bot replies do not become new independent witnesses. This prevents rumour feedback loops.
    auto message = understand(match, speaker, text);
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
    if (!reply || replyPriority(match, speaker, text, channel) == 0) return;
    const Text response = answer(match, speaker, message, contradiction);
    if (response.empty()) return;
    if (message.topic == kNoPlayer && (message.intent == dialogue::Intent::AskSuspicion || message.intent == dialogue::Intent::AskVote)) {
        const uint8_t discussed = message.intent == dialogue::Intent::AskVote && match.phase() == Phase::Vote && me->vote != kNoPlayer
            ? me->vote : strongestSuspect(match, 1.0f);
        if (discussed != kNoPlayer) m_topics[speaker] = {discussed, match.clock()};
    }
    m_reply = Reply{speaker, channel, match.phase(), match.day(), match.clock() + m_rng.uniform(1.2f, 2.8f),
                   tx("{}, {}", "{}, {}", from->name, response)};
}

Text Bot::explain(const Match& match, uint8_t target) const {
    const auto* p = match.find(target);
    if (!p) return {"Ich habe noch keinen begründeten Verdacht. Was hast du beobachtet?",
                    "I don't have a solid suspect yet. What did you observe?"};
    const auto view = match.viewFor(m_id);
    for (const auto& result : view.seerResults)
        if (result.player == target) return isWolfRole(result.role)
            ? tx("Ich habe {} als Seherin geprüft: Werwolf.", "I checked {} as the seer: werewolf.", p->name)
            : tx("Meine Prüfung als Seherin hat bei {} keinen Werwolf ergeben.", "My seer check did not identify {} as a werewolf.", p->name);
    if (const auto* known = view.player(target); known && known->role != Role::Unknown && !isWolfRole(known->role))
        return tx("Die Rolle von {} ist mir bekannt. Ich verdächtige diese Person nicht als Werwolf.",
                  "I know {}'s role. I don't suspect this person of being a werewolf.", p->name);
    if (auto seen = m_wolfWitness.find(target); seen != m_wolfWitness.end())
        return tx("Ich habe {} in Nacht {} selbst bei der Verwandlung erkannt.",
                  "I personally recognised {} transforming on night {}.", p->name, seen->second);
    if (const auto* source = match.find(m_dialogue.source(target, match.day())); source && m_dialogue.suspicion(target, match.day()) > 0.2f)
        return tx("{} hat {} beschuldigt. Das ist für mich ein Hinweis, noch kein Beweis. Gibt es einen weiteren Zeugen?",
                  "{} accused {}. That's a lead, not proof. Is there another witness?", source->name, p->name);
    if (suspicion(match, target) > 1.0f)
        return tx("Meine Beobachtungen im Spiel sprechen gegen {}. Ganz sicher bin ich mir noch nicht.",
                  "My observations in the game point towards {}. I'm not certain yet.", p->name);
    return tx("Gegen {} habe ich bisher keinen belastbaren Hinweis. Ein Name allein reicht mir nicht.",
              "I don't have solid evidence against {} so far. A name alone isn't enough.", p->name);
}

Text Bot::answer(const Match& match, uint8_t speaker, const dialogue::Message& message, bool contradiction) const {
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
    switch (message.intent) {
    case Intent::AskReason:
        return explain(match, topic ? topic->id : strongestSuspect(match, 1.0f));
    case Intent::AskInnocence:
        if (topic) return explain(match, topic->id);
        for (const auto& p : view.players)
            if (p.alive && p.id != m_id && p.role != Role::Unknown && !isWolfRole(p.role)) return explain(match, p.id);
        for (const auto& p : view.players)
            if (p.alive && p.id != m_id && suspicion(match, p.id) < -0.5f)
                return tx("{} wurde im Gespräch entlastet. Bewiesen ist die Unschuld damit noch nicht.",
                          "{} was defended in the conversation. That doesn't prove innocence yet.", p.name);
        return {"Ich kann bisher niemanden sicher entlasten. Hast du eine überprüfbare Beobachtung?",
                "I can't confidently clear anyone yet. Do you have an observation we can verify?"};
    case Intent::AskSuspicion:
    case Intent::AskVote: {
        const uint8_t suspect = topic ? topic->id : strongestSuspect(match, 1.0f);
        if (message.intent == Intent::AskVote && me->vote != kNoPlayer && match.phase() == Phase::Vote) {
            if (const auto* voted = match.find(me->vote))
                return tx("Meine Stimme geht an {}. {}", "My vote goes to {}. {}", voted->name, explain(match, voted->id));
        }
        return explain(match, suspect);
    }
    case Intent::AskAlibi: {
        if (topic && topic->id != m_id)
            return tx("Wo {} die ganze Nacht war, kann ich nicht belegen. Frag nach einem Zeugen.",
                      "I can't account for {}'s whole night. Ask for a witness.", topic->name);
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
        if (topic && topic->id != m_id) return explain(match, topic->id);
        if (me->role == Role::Seer && !view.seerResults.empty()) return explain(match, view.seerResults.back().player);
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
            if (c.weight < 0) {
                if (suspicion(match, c.target) >= 4.0f)
                    return tx("Deine Entlastung von {} ist angekommen. Meine eigenen Hinweise dagegen sind aber stärker. {}",
                              "I've heard your defence of {}. My own contrary evidence is stronger, though. {}",
                              accused->name, explain(match, c.target));
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
    if (!m_reply) return;
    const Reply pending = *m_reply;
    if (!me->alive || pending.phase != match.phase() || pending.day != match.day()) { m_reply.reset(); return; }
    if (match.clock() < pending.due) return;
    m_reply.reset(); // clear before say() re-enters the server's chat routing
    const auto* from = match.find(pending.speaker);
    const bool secret = pending.channel == ChatChannel::Wolves || pending.channel == ChatChannel::Family || pending.channel == ChatChannel::Dead;
    if (!from || !from->alive || match.chatChannel(m_id, secret) != pending.channel ||
        !match.canHear(pending.speaker, m_id, pending.channel) ||
        (pending.channel == ChatChannel::Nearby && me->wolfForm())) return;
    m_replyAfter = match.clock() + 5.0;
    m_lastReplyTo = {pending.speaker, match.clock()};
    m_chatTimer = -1.0f;
    say(m_id, pending.text, secret);
}

} // namespace ww
