/********************************************************************
    Copyright (c) 2013-2015 - Mogara

    This file is part of QSanguosha.

    This game engine is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

    See the LICENSE file for more details.

    Mogara
*********************************************************************/

#include "card.h"
#include "eventhandler.h"
#include "gamelogic.h"
#include "gamerule.h"
#include "general.h"
#include "package.h"
#include "player.h"
#include "protocol.h"
#include "cserveruser.h"
#include "util.h"

#include <croom.h>
#include <cserver.h>

#include <QCoreApplication>
#include <QDateTime>

GameLogic::GameLogic(CRoom *parent)
    : CAbstractGameLogic(parent)
    , m_currentPlayer(NULL)
    , m_gameRule(NULL)
    , m_skipGameRule(false)
    , m_round(0)
{

}

GameLogic::~GameLogic()
{
    foreach (Card *card, m_cards)
        delete card;
}

void GameLogic::setGameRule(const GameRule *rule) {
    if (m_gameRule) {
        foreach (EventType e, m_gameRule->events()) {
            if (m_handlers[e].contains(m_gameRule))
                m_handlers[e].removeOne(m_gameRule);
        }
    }

    m_gameRule = rule;
    if (rule) {
        foreach (EventType e, rule->events())
            m_handlers[e].append(m_gameRule);
    }
}

void GameLogic::addEventHandler(const EventHandler *handler)
{
    QList<EventType> events = handler->events();
    foreach(EventType event, events)
        m_handlers[event] << handler;
}

bool GameLogic::trigger(EventType event, Player *target)
{
    QVariant data;
    return trigger(event, target, data);
}

bool GameLogic::trigger(EventType event, Player *target, QVariant &data)
{
    QList<const EventHandler *> &handlers = m_handlers[event];

    //@todo: Resolve C++98 Incompatibility?
    qStableSort(handlers.begin(), handlers.end(), [event](const EventHandler *a, const EventHandler *b){
        return a->priority(event) > b->priority(event);
    });

    int triggerableIndex = 0;
    while (triggerableIndex < handlers.length()) {
        int currentPriority = 0;
        QMap<Player *, QList<Event>> triggerableEvents;

        //Construct triggerableEvents
        do {
            const EventHandler *handler = handlers.at(triggerableIndex);
            if (handler == m_gameRule) {
                if (triggerableEvents.isEmpty()) {
                    triggerableEvents[NULL] << handler;
                    currentPriority = handler->priority(event);
                } else if (handler->priority(event) == currentPriority) {
                    triggerableEvents[NULL] << handler;
                } else {
                    break;
                }
            } else {
                if (triggerableEvents.isEmpty() || handler->priority(event) == currentPriority) {
                    QMap<Player *, Event> events = handler->triggerable(this, event, target, data);
                    QList<Player *> players = this->players();
                    foreach (Player *p, players) {
                        if (!events.contains(p))
                            continue;

                        QList<Event> ds = events.values(p);
                        triggerableEvents[p] << ds;
                        currentPriority = ds.last().handler->priority(event);
                    }
                } else if (handler->priority(event) != currentPriority) {
                    break;
                }
            }
            triggerableIndex++;
        } while (triggerableIndex < handlers.length());

        if (!triggerableEvents.isEmpty()) {
            QList<Player *> allPlayers = this->allPlayers(true);
            foreach (Player *invoker, allPlayers) {
                if (!triggerableEvents.contains(invoker))
                    continue;

                forever {
                    QList<Event> &events = triggerableEvents[invoker];

                    bool hasCompulsory = false;
                    foreach (const Event &d, events) {
                        if (d.handler->frequency() == EventHandler::Compulsory || d.handler->frequency() == EventHandler::Wake) {
                            hasCompulsory = true;
                            break;
                        }
                    }

                    //Ask the invoker to determine the trigger order
                    Event choice;
                    if (events.length() > 1) {
                        if (!invoker->hasShownBothGenerals())
                            m_globalRequestEnabled = true;
                        choice = askForTriggerOrder(invoker, "GameRule:TriggerOrder", events, !hasCompulsory);
                        m_globalRequestEnabled = false;
                    } else {
                        choice = events.first();
                    }

                    //If the user selects "cancel"
                    if (!choice.isValid())
                        break;

                    //Ask the invoker for cost
                    if (!invoker->hasShownSkill(choice.handler))
                        m_globalRequestEnabled = true;
                    bool takeEffect = choice.handler->cost(this, event, choice.to.at(0), data, invoker);
                    if (takeEffect && !invoker->hasShownSkill(choice.handler)) {
                        //@todo: show skill here?
                    }
                    m_globalRequestEnabled = false;

                    //Take effect
                    if (takeEffect) {
                        bool broken = choice.handler->effect(this, event, choice.to.at(0), data, invoker);
                        if (broken)
                            break;
                    }

                    //Remove targets that are in front of the triggered target
                    for (int i = 0; i < events.length(); i++) {
                        Event &d = events[i];
                        if (d.handler != choice.handler)
                            continue;

                        foreach (Player *to, choice.to) {
                            int index = d.to.indexOf(to);
                            if (index == d.to.length() - 1) {
                                events.removeAt(i);
                                i--;
                            } else {
                                d.to = d.to.mid(index + 1);
                            }
                        }
                    }
                }
            }
        }
    }

    return false;
}

QList<Player *> GameLogic::players() const
{
    QList<Player *> players;
    auto abstractPlayers = this->abstractPlayers();
    foreach (CAbstractPlayer *p, abstractPlayers)
        players << qobject_cast<Player *>(p);
    return players;
}

Player *GameLogic::findPlayer(uint id) const
{
    return qobject_cast<Player *>(findAbstractPlayer(id));
}

Player *GameLogic::findPlayer(CServerAgent *agent) const
{
    return qobject_cast<Player *>(findAbstractPlayer(agent));
}

QList<Player *> GameLogic::allPlayers(bool includeDead) const
{
    QList<Player *> players = this->players();
    Player *current = currentPlayer();
    if (current == NULL)
        return players;

    int currentIndex = players.indexOf(current);
    if (currentIndex == -1)
        return players;

    QList<Player *> allPlayers;
    for (int i = currentIndex; i < players.length(); i++) {
        if (includeDead || players.at(i)->isAlive())
            allPlayers << players.at(i);
    }
    for (int i = 0; i < currentIndex; i++) {
        if (includeDead || players.at(i)->isAlive())
            allPlayers << players.at(i);
    }

    if (current->phase() == Player::NotActive && allPlayers.contains(current)) {
        allPlayers.removeOne(current);
        allPlayers.append(current);
    }

    return allPlayers;
}

Event GameLogic::askForTriggerOrder(Player *player, const QString &reason, QList<Event> &options, bool cancelable)
{
    //@todo:
    C_UNUSED(player);
    C_UNUSED(reason);
    C_UNUSED(options);
    C_UNUSED(cancelable);
    return Event();
}

CAbstractPlayer *GameLogic::createPlayer(CServerUser *user)
{
    C_UNUSED(user);
    return new Player(this);
}

CAbstractPlayer *GameLogic::createPlayer(CServerRobot *robot)
{
    C_UNUSED(robot);
    return new Player(this);
}

void GameLogic::prepareToStart()
{
    CRoom *room = this->room();

    //Arrange seats for all the players
    QList<Player *> players = this->players();
    qShuffle(players);
    for (int i = 1; i < players.length(); i++) {
        players[i - 1]->setSeat(i);
        players[i - 1]->setNext(players.at(i));
    }
    Player *lastPlayer = players.last();
    lastPlayer->setSeat(players.length());
    lastPlayer->setNext(players.first());
    setCurrentPlayer(players.first());

    QVariantList playerList;
    foreach (Player *player, players) {
        CServerAgent *agent = findAgent(player);
        QVariantMap info;
        if (agent->controlledByClient()) {
            info["userId"] = agent->id();
        } else {
            info["robotId"] = agent->id();
        }
        info["playerId"] = player->id();
        playerList << info;
    }
    room->broadcastNotification(S_COMMAND_ARRANGE_SEAT, playerList);

    //Import packages
    QList<const General *> generals;
    foreach (const Package *package, m_packages) {
        generals << package->generals();
        QList<const Card *> cards = package->cards();
        foreach (const Card *card, cards)
            m_cards << card->clone();
    }

    //Prepare cards
    QVariantList cardData;
    foreach (const Card *card, m_cards)
        cardData << card->id();
    room->broadcastNotification(S_COMMAND_PREPARE_CARDS, cardData);

    //Choose 7 random generals for each player
    //@to-do: config
    int candidateLimit = 7;
    qShuffle(generals);

    QMap<Player *, QList<const General *>> playerCandidates;

    foreach (Player *player, players) {
        QList<const General *> candidates = generals.mid((player->seat() - 1) * candidateLimit, candidateLimit);
        playerCandidates[player] = candidates;

        QVariantList candidateData;
        foreach (const General *general, candidates)
            candidateData << general->name();

        QVariantList bannedPairData;
        //@todo: load banned pairs

        QVariantList data;
        data << QVariant(candidateData);
        data << QVariant(bannedPairData);

        CServerAgent *agent = findAgent(player);
        agent->prepareRequest(S_COMMAND_CHOOSE_GENERAL, data);
    }

    //@to-do: timeout should be loaded from config
    room->broadcastRequest(room->agents(), 15000);

    foreach (Player *player, players) {
        const QList<const General *> &candidates = playerCandidates[player];
        QList<const General *> generals;

        CServerAgent *agent = findAgent(player);
        if (agent) {
            QVariantList reply = agent->waitForReply(0).toList();
            foreach (const QVariant &choice, reply) {
                QString name = choice.toString();
                foreach (const General *general, candidates) {
                    if (general->name() == name)
                        generals << general;
                }
            }
        }

        //@to-do: handle banned pairs
        if (generals.length() < 2)
            generals = candidates.mid(0, 2);

        player->setHeadGeneral(generals.at(0));
        player->setDeputyGeneral(generals.at(1));
    }
}

void GameLogic::run()
{
    qsrand((uint) QDateTime::currentMSecsSinceEpoch());

    prepareToStart();

    //@to-do: Turn broken event not into a new
    trigger(GameStart, NULL);

    forever {
        try {
            forever {
                Player *current = currentPlayer();
                if (current->seat() == 1)
                    m_round++;

                trigger(TurnStart, current);
                Player *next = current->nextAlive(1, false);
                while (!m_extraTurns.isEmpty()) {
                    Player *extra = m_extraTurns.takeFirst();
                    setCurrentPlayer(extra);
                    trigger(TurnStart, extra);
                }
                setCurrentPlayer(next);
            }
        } catch (EventType event) {
            if (event == GameFinish) {
                return;
            } else if (event == TurnBroken) {
                Player *current = currentPlayer();
                trigger(TurnBroken, current);
                Player *next = current->nextAlive(1, false);
                if (current->phase() != Player::NotActive) {
                    QVariant data;
                    m_gameRule->effect(this, PhaseEnd, current, data, current);
                    //@todo:
                    current->setPhase(Player::NotActive);
                }
                setCurrentPlayer(next);
            }
        }
    }
}
