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
#include "eventtype.h"
#include "gamelogic.h"
#include "serverplayer.h"

Card::Card(Suit suit, int number)
    : m_id(0)
    , m_suit(suit)
    , m_number(number)
    , m_color(NoColor)
    , m_transferable(false)
    , m_willThrow(true)
    , m_canRecast(false)
    , m_targetFixed(false)
{
}

Card *Card::clone() const
{
    const QMetaObject *metaObject = this->metaObject();
    Card *card = qobject_cast<Card *>(metaObject->newInstance(Q_ARG(Suit, suit()), Q_ARG(int, number())));
    card->m_id = m_id;
    return card;
}

uint Card::effectiveId() const
{
    if (!isVirtual())
        return m_id;

    if (m_subcards.length() == 1)
        return m_subcards.first()->effectiveId();

    return 0;
}

Card::Suit Card::suit() const
{
    if (m_subcards.isEmpty())
        return m_suit;
    else if (m_subcards.length() == 1)
        return m_subcards.first()->suit();
    else
        return NoSuit;
}

void Card::setSuitString(const QString &suit)
{
    if (suit == "spade")
        setSuit(Spade);
    else if (suit == "heart")
        setSuit(Heart);
    else if (suit == "club")
        setSuit(Club);
    else if (suit == "diamond")
        setSuit(Diamond);
    else
        setSuit(NoSuit);
}

QString Card::suitString() const
{
    if (m_suit == Spade)
        return "spade";
    else if (m_suit == Heart)
        return "heart";
    else if (m_suit == Club)
        return "club";
    else if (m_suit == Diamond)
        return "diamond";
    else
        return "no_suit";
}

int Card::number() const
{
    if (m_number > 0)
        return m_number;

    int number = 0;
    foreach (const Card *card, m_subcards)
        number += card->number();
    return number >= 13 ? 13 : number;
}

Card::Color Card::color() const
{
    if (m_suit == NoSuit)
        return m_color;
    return (m_suit == Spade || m_suit == Club) ? Black : Red;
}

void Card::setColorString(const QString &color)
{
    if (color == "black")
        setColor(Black);
    else if (color == "red")
        setColor(Red);
    else
        setColor(NoColor);
}

QString Card::colorString() const
{
    Color color = this->color();
    if (color == Black)
        return "black";
    else if (color == Red)
        return "red";
    else
        return "no_color";
}

QString Card::typeString() const
{
    if (m_type == BasicType)
        return "basic";
    else if (m_type == TrickType)
        return "trick";
    else if (m_type == EquipType)
        return "equip";
    else
        return "skill";
}

void Card::addSubcard(Card *card)
{
    m_subcards << card;
}

Card *Card::realCard()
{
    if (id() > 0)
        return this;

    if (m_subcards.length() == 1)
        return m_subcards.first()->realCard();

    return nullptr;
}

const Card *Card::realCard() const
{
    if (id() > 0)
        return this;

    if (m_subcards.length() == 1)
        return m_subcards.first()->realCard();

    return nullptr;
}

QList<Card *> Card::realCards()
{
    QList<Card *> cards;
    if (id() > 0) {
        cards << this;
    } else {
        foreach (Card *card, m_subcards)
            cards << card->realCards();
    }
    return cards;
}

QList<const Card *> Card::realCards() const
{
    QList<const Card *> cards;
    if (id() > 0) {
        cards << this;
    } else {
        foreach (const Card *card, m_subcards)
            cards << card->realCards();
    }
    return cards;
}

bool Card::targetFeasible(const QList<const Player *> &targets, const Player *self) const
{
    C_UNUSED(targets);
    C_UNUSED(self);
    return isTargetFixed();
}

bool Card::targetFilter(const QList<const Player *> &targets, const Player *toSelect, const Player *self) const
{
    C_UNUSED(targets);
    C_UNUSED(toSelect);
    C_UNUSED(self);
    return false;
}

bool Card::isAvailable(const Player *) const
{
    //@to-do: check Jilei here
    return true;
}

void Card::onUse(GameLogic *logic, CardUseStruct &use)
{
    logic->sortByActionOrder(use.to);

    QVariant useData = QVariant::fromValue(&use);
    logic->trigger(PreCardUsed, use.from, useData);

    CardsMoveStruct move;
    move.to.type = CardArea::Table;
    move.isOpen = true;
    move.cards = use.card->realCards();
    logic->moveCards(move);
}

void Card::use(GameLogic *logic, CardUseStruct &use)
{
    foreach (ServerPlayer *target, use.to) {
        CardEffectStruct effect(use);
        effect.to = target;
        logic->takeCardEffect(effect);
    }

    if (use.target) {
        CardEffectStruct effect(use);
        logic->takeCardEffect(effect);
    }

    const CardArea *table = logic->table();
    if (table->length() > 0) {
        CardsMoveStruct move;
        move.cards = table->cards();
        move.to.type = CardArea::DiscardPile;
        move.isOpen = true;
        logic->moveCards(move);
    }
}

void Card::onEffect(GameLogic *, CardEffectStruct &)
{
}

void Card::effect(GameLogic *, CardEffectStruct &)
{
}

BasicCard::BasicCard(Card::Suit suit, int number)
    : Card(suit, number)
{
    m_type = BasicType;
}


TrickCard::TrickCard(Card::Suit suit, int number)
    : Card(suit, number)
{
    m_type = TrickType;
}

void TrickCard::onEffect(GameLogic *logic, CardEffectStruct &effect)
{
    if (isNullifiable(effect)) {
        QList<ServerPlayer *> players = logic->allPlayers();
        //@to-do: do not ask if no player can use nullification
        foreach (ServerPlayer *player, players) {
            if (effect.from) {
                if (effect.to)
                    player->showPrompt("trick-nullification-1", effect.from, effect.to, effect.use.card);
                else
                    player->showPrompt("trick-nullification-2", effect.from, effect.use.card);
            } else if (effect.to) {
                player->showPrompt("trick-nullification-3", effect.to, effect.use.card);
            } else {
                player->showPrompt("trick-nullification-4", effect.use.card);
            }
            Card *card = player->askForCard("Nullification");
            if (card) {
                CardUseStruct use;
                use.from = player;
                use.card = card;
                use.target = effect.use.card;
                use.extra = QVariant::fromValue(&effect);
                logic->useCard(use);
                break;
            }
        }
    }
}

bool TrickCard::isNullifiable(const CardEffectStruct &) const
{
    return true;
}

EquipCard::EquipCard(Card::Suit suit, int number)
    : Card(suit, number)
    , m_skill(nullptr)
{
    m_type = EquipType;
    m_targetFixed = true;
}

void EquipCard::onUse(GameLogic *logic, CardUseStruct &use)
{
    ServerPlayer *player = use.from;
    if (use.to.isEmpty())
        use.to << player;

    QVariant data = QVariant::fromValue(&use);
    logic->trigger(PreCardUsed, player, data);
}

void EquipCard::use(GameLogic *logic, CardUseStruct &use)
{
    if (use.to.isEmpty()) {
        CardsMoveStruct move;
        move.cards << this;
        move.to.type = CardArea::DiscardPile;
        move.isOpen = true;
        logic->moveCards(move);
        return;
    }

    ServerPlayer *target = use.to.first();

    //Find the existing equip
    Card *equippedCard = nullptr;
    QList<Card *> equips = target->equips()->cards();
    foreach (Card *card, equips) {
        if (card->subtype() == subtype()) {
            equippedCard = card;
            break;
        }
    }

    QList<CardsMoveStruct> moves;

    CardsMoveStruct install;
    install.cards << this;
    install.to.type = CardArea::Equip;
    install.to.owner = target;
    install.isOpen = true;
    moves << install;

    if (equippedCard != nullptr) {
        CardsMoveStruct uninstall;
        uninstall.cards << equippedCard;
        uninstall.to.type = CardArea::Table;
        uninstall.isOpen = true;
        moves << uninstall;
    }
    logic->moveCards(moves);

    if (equippedCard != nullptr) {
        const CardArea *table = logic->table();
        if (table->contains(equippedCard)) {
            CardsMoveStruct discard;
            discard.cards << equippedCard;
            discard.to.type = CardArea::DiscardPile;
            discard.isOpen = true;
            logic->moveCards(discard);
        }
    }
}

GlobalEffect::GlobalEffect(Card::Suit suit, int number)
    : TrickCard(suit, number)
{
    m_targetFixed = true;
    m_subtype = GlobalEffectType;
}

void GlobalEffect::onUse(GameLogic *logic, CardUseStruct &use)
{
    if (use.to.isEmpty())
        use.to = logic->allPlayers();
    TrickCard::onUse(logic, use);
}

AreaOfEffect::AreaOfEffect(Card::Suit suit, int number)
    : TrickCard(suit, number)
{
    m_targetFixed = true;
    m_subtype = AreaOfEffectType;
}

void AreaOfEffect::onUse(GameLogic *logic, CardUseStruct &use)
{
    if (use.to.isEmpty())
        use.to = logic->otherPlayers(use.from);
    TrickCard::onUse(logic, use);
}

SingleTargetTrick::SingleTargetTrick(Card::Suit suit, int number)
    : TrickCard(suit, number)
{
    m_subtype = SingleTargetType;
}

bool SingleTargetTrick::targetFeasible(const QList<const Player *> &targets, const Player *) const
{
    return isTargetFixed() || targets.length() == 1;
}

bool SingleTargetTrick::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() < 1;
}

DelayedTrick::DelayedTrick(Card::Suit suit, int number)
    : TrickCard(suit, number)
{
    m_subtype = DelayedType;
}

bool DelayedTrick::targetFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 1;
}

bool DelayedTrick::targetFilter(const QList<const Player *> &targets, const Player *toSelect, const Player *self) const
{
    if (!targets.isEmpty() || toSelect == self)
        return false;

    const CardArea *area = toSelect->delayedTricks();
    if (area->length() <= 0)
        return true;

    return !area->contains(metaObject()->className());
}

void DelayedTrick::onUse(GameLogic *logic, CardUseStruct &use)
{
    logic->sortByActionOrder(use.to);

    QVariant useData = QVariant::fromValue(&use);
    logic->trigger(PreCardUsed, use.from, useData);
}

void DelayedTrick::use(GameLogic *logic, CardUseStruct &use)
{
    CardsMoveStruct move;
    move.cards = use.card->realCards();
    move.isOpen = true;
    if (use.to.isEmpty()) {
        move.to.type = CardArea::DiscardPile;
    } else {
        move.to.type = CardArea::DelayedTrick;
        move.to.owner = use.to.first();
    }
    logic->moveCards(move);
}

void DelayedTrick::effect(GameLogic *logic, CardEffectStruct &effect)
{
    CardsMoveStruct move;
    move.cards << this;
    move.to.type = CardArea::Table;
    move.isOpen = true;
    logic->moveCards(move);

    JudgeStruct judge(m_judgePattern);
    judge.who = effect.to;
    logic->judge(judge);

    if (judge.matched)
        takeEffect(logic, effect);

    const CardArea *table = logic->table();
    if (table->contains(this)) {
        CardsMoveStruct move;
        move.cards << this;
        move.to.type = CardArea::DiscardPile;
        move.isOpen = true;
        logic->moveCards(move);
    }
}

MovableDelayedTrick::MovableDelayedTrick(Card::Suit suit, int number)
    : DelayedTrick(suit, number)
{
    m_targetFixed = true;
}

bool MovableDelayedTrick::targetFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.isEmpty();
}

bool MovableDelayedTrick::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
    return false;
}

void MovableDelayedTrick::onUse(GameLogic *logic, CardUseStruct &use)
{
    if (use.to.isEmpty())
        use.to << use.from;
    DelayedTrick::onUse(logic, use);
}

void MovableDelayedTrick::effect(GameLogic *logic, CardEffectStruct &effect)
{
    CardsMoveStruct move;
    move.cards << this;
    move.to.type = CardArea::Table;
    move.isOpen = true;
    logic->moveCards(move);

    JudgeStruct judge(m_judgePattern);
    judge.who = effect.to;
    logic->judge(judge);

    if (judge.matched) {
        takeEffect(logic, effect);
        const CardArea *table = logic->table();
        if (table->contains(this)) {
            CardsMoveStruct move;
            move.cards << this;
            move.to.type = CardArea::DiscardPile;
            move.isOpen = true;
            logic->moveCards(move);
        }
    } else {
        ServerPlayer *target = nullptr;
        forever {
            target = effect.to->nextAlive();
            if (!targetFilter(QList<const Player *>(), target, effect.to))
                continue;

            CardsMoveStruct move;
            move.cards << this;
            move.to.type = CardArea::DelayedTrick;
            move.to.owner = target;
            move.isOpen = true;
            logic->moveCards(move);

            CardUseStruct use;
            use.from = effect.to;
            use.card = this;
            use.to << target;

            QVariant data = QVariant::fromValue(&use);
            logic->trigger(TargetConfirming, target, data);
            if (use.to.isEmpty())
                continue;
            logic->trigger(TargetChosen, use.from, data);
            foreach (ServerPlayer *to, use.to)
                logic->trigger(TargetConfirmed, to, data);
            if (!use.to.isEmpty())
                break;
        }
    }
}

bool MovableDelayedTrick::isAvailable(const Player *player) const
{
    const char *className = metaObject()->className();
    QList<Card *> cards = player->delayedTricks()->cards();
    foreach (const Card *card, cards) {
        if (card->inherits(className))
            return false;
    }
    return DelayedTrick::isAvailable(player);
}

Weapon::Weapon(Card::Suit suit, int number)
    : EquipCard(suit, number)
    , m_attackRange(0)
{
    m_subtype = WeaponType;
}


Armor::Armor(Card::Suit suit, int number)
    : EquipCard(suit, number)
{
    m_subtype = ArmorType;
}


Horse::Horse(Card::Suit suit, int number)
    : EquipCard(suit, number)
{
}

Card *Horse::clone() const
{
    Card *card = Card::clone();
    card->setObjectName(objectName());
    return card;
}

OffensiveHorse::OffensiveHorse(Card::Suit suit, int number)
    : Horse(suit, number)
    , m_extraOutDistance(-1)
{
    m_subtype = OffensiveHorseType;
}

DefensiveHorse::DefensiveHorse(Card::Suit suit, int number)
    : Horse(suit, number)
    , m_extraInDistance(+1)
{
    m_subtype = DefensiveHorseType;
}

Treasure::Treasure(Card::Suit suit, int number)
    : EquipCard(suit, number)
{
    m_subtype = TreasureType;
}
