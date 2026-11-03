#include "logic/Match.h"

#include <algorithm>

namespace ww {

bool Match::startTutorial(const std::vector<Seat>& seats, const World& world, Text& error) {
    if (seats.size() != 6 || seats.front().bot ||
        std::any_of(seats.begin() + 1, seats.end(), [](const Seat& seat) { return !seat.bot; })) {
        error = {"Der Übungsfall braucht einen Spieler und fünf Bots.", "The case needs one player and five bots."};
        return false;
    }
    Settings settings;
    settings.captain = false;
    settings.daySeconds = 50;
    settings.discussionSeconds = 32;
    settings.nightSeconds = 35;
    settings.voteSeconds = 20;
    std::vector<Role> roles(6, Role::Villager);
    roles[1] = Role::Werewolf;
    if (!startWithRoles(seats, roles, settings, world, 817, error)) return false;
    m_case = TutorialCase{seats[0].id, seats[1].id, seats[2].id, seats[3].id, seats[4].id, seats[5].id};
    setTimer(18);
    return true;
}

void Match::updateTutorial() {
    auto& c = *m_case;
    const Player& victim = *find(c.victim);
    const auto house = victim.house;
    const auto& h = m_world->houses()[house];
    auto outside = [&](uint8_t who, float x, float z) {
        const Vec2 p = m_world->houseToWorld(house, {x, h.depth * .5f + z});
        teleport(who, m_world->ground(p.x, p.y));
    };
    if (m_phase == Phase::Night) {
        if (!c.staged) {
            outside(c.learner, -2, 8);
            outside(c.wolf, 0, 1);
            outside(c.nearWitness, 2, 3);
            outside(c.passerby, 3, 4);
            outside(c.farWitness, 4, 14);
            teleport(c.victim, h.doorInside);
            m_doors[house] = {DoorLocked, 100};
            c.staged = true;
            markDirty();
        }
        if (m_phaseElapsed < 5) return;
        const auto& wolf = *find(c.wolf);
        if (wolf.transformTarget == 0) interact(c.wolf, Interaction::Transform, 1);
        if (!wolf.wolfForm() || wolf.attackCooldown > 0) return;
        if (!c.broken) {
            interact(c.wolf, Interaction::DoorBreak, house);
            if (m_doors[house].flags & DoorBroken) {
                c.broken = true;
                highlight(9, tx("{} schlug die verriegelte Tür von {} ein. Daher kam das Krachen.",
                                "{} broke down {}'s bolted door. That was the crash.", wolf.name, victim.name));
            }
        } else if (victim.alive) {
            teleport(c.wolf, h.doorInside);
            interact(c.wolf, Interaction::Attack, c.victim);
            if (!victim.alive) setTimer(4);
        }
    } else if (m_phase == Phase::Morning && !c.gathered) {
        for (size_t i = 0; i < m_players.size(); ++i)
            if (m_players[i].alive) teleport(m_players[i].id, m_world->spawnPoint(static_cast<int>(i), 6));
        c.gathered = true;
    } else if (m_phase == Phase::Vote && find(c.learner)->vote != kNoPlayer && m_timer > 4) {
        setTimer(4);
    }
}

uint8_t Match::nextTutorialWitness() const {
    if (!m_case || !m_case->broken || m_case->accounts >= 2 || m_clock < m_case->nextAccount || m_phaseElapsed < 2 ||
        m_day != 2 || (m_phase != Phase::Day && m_phase != Phase::Assembly)) return kNoPlayer;
    const auto& c = *m_case;
    const auto id = c.accounts == 0 ? c.nearWitness : c.farWitness;
    const auto ch = chatChannel(id, false);
    return ch && canHear(c.learner, id, *ch) ? id : kNoPlayer;
}

Text Match::tutorialStatement(uint8_t speaker, bool followup) const {
    if (!m_case || !m_case->broken) return {};
    const auto& c = *m_case;
    if (speaker == c.nearWitness)
        return followup ? tx("{} stand direkt neben mir. Ich sah {}, bevor die Gestalt zur Tür ging.",
                              "{} was right beside me. I saw {} before the figure went to the door.", nameOf(c.passerby), nameOf(c.wolf)) :
                          tx("Ich sah {}, gleich vor der Tür. Als es krachte, war {} noch neben mir.",
                             "I saw {} right at the door. When it crashed, {} was still beside me.", nameOf(c.wolf), nameOf(c.passerby));
    if (speaker == c.farWitness)
        return followup ? Text{"Vom Weg aus sah ich nur eine Laterne. Ein Gesicht konnte ich nicht erkennen.",
                               "From the road I only saw a lantern. I could not make out a face."} :
                          tx("Ich glaube, {} hat die Tür eingeschlagen. Ich sah dort eine Laterne.",
                             "I think {} broke down the door. I saw a lantern there.", nameOf(c.passerby));
    return {};
}

void Match::heardTutorialWitness(uint8_t speaker) {
    auto& c = *m_case;
    if (speaker != nextTutorialWitness()) return;
    ++c.accounts;
    c.nextAccount = m_clock + 5;
    if (speaker == c.farWitness)
        highlight(9, tx("{} hielt {} wegen der Laterne für den Angreifer. Die Tür hatte {} eingeschlagen.",
                        "{} mistook {} for the attacker because of the lantern. {} had broken the door.",
                        nameOf(speaker), nameOf(c.passerby), nameOf(c.wolf)));
    markDirty();
}

Text Match::tutorialHint() const {
    const auto& c = *m_case;
    switch (m_phase) {
    case Phase::Night: return {"Hör auf die Tür. Wer steht beim Krachen im Licht, wer direkt an der Tür?",
                               "Listen to the door. Who is in the light when it cracks? Who is at the door?"};
    case Phase::Morning: return {"Am Feuer warten zwei Zeugen. Gehe nach Sonnenaufgang zu ihnen.",
                                 "Two witnesses are waiting at the fire. Walk over after sunrise."};
    case Phase::Day:
        if (m_day == 1) return {"Die Tür in der Nacht · Gehe mit WASD zum Feuer. Mit der Maus siehst du dich um.",
                                "The door in the night · Walk to the fire with WASD. Use the mouse to look around."};
        return tx("Sprich am Feuer mit {} und {} (Enter). Rechtsklick auf ihre Aussagen merkt sie für dein Buch.",
                  "Speak to {} and {} at the fire (Enter). Right-click their statements to pin them.", nameOf(c.nearWitness), nameOf(c.farWitness));
    case Phase::Assembly: return {"Öffne mit J das Verdachtsbuch. Wähle beide Aussagen und frage am Feuer nach.",
                                  "Press J for the journal. Compare the statements and ask at the fire."};
    case Phase::Vote: return {"Deine Stimme entscheidet den Fall. Schau jemanden an und drücke E.",
                              "Your vote decides the case. Look at a suspect and press E."};
    default: return {};
    }
}

} // namespace ww
