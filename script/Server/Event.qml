import QtQuick 2.5
import Sanguosha.GameLogic 1.0
import Sanguosha.Player 1.0

QtObject {

    //property EventType e;
    property var eventHander;

    property LogicMain m_logic;

    property ServerPlayer skillOwner;
    property ServerPlayer skillInvoker;
    property var m_targets;

    property bool isCompulsory;
    property bool triggered;

    property ServerPlayer preferredTarget;

    property var tag;

    function init(logic,skill,owner,invoker,targets,is_compulsory,preferred_target) {
        m_logic = logic;
        eventHander = skill;    // TriggerSkill

        triggered = false;
        isCompulsory = is_compulsory;

        skillInvoker = invoker;    //ServerPlayer
        skillOwner = owner;      //ServerPlayer
        preferredTarget = preferred_target; //ServerPlayer

        m_targets = targets;    //QList<ServerPlayer *>

        tag = {};               //QVariantMap
    }

    function isValid() {
        return eventHander !== null;
    }

    function sameSkillWith(other) {
        return eventHander === other.eventHander && skillOwner === other.skillOwner && skillInvoker === other.skillInvoker;
    }

    function sameTimingWith(other) {
        if (!isValid() || !other.isValid())
            return false;
        return eventHander.priority === other.eventHander.priority && eventHander.isEquipSkill === other.eventHander.isEquipSkill
    }

    function toVariant() {
        var ob = {};
        if (eventHander)
            ob["skill"] = eventHander.objectName;
        if (owner)
            ob["owner"] = skillOwner.objectName;
        if (invoker)
            ob["invoker"] = skillInvoker.objectName;
        // @to_do(Xusine):insert preferredTarget into ob.

        return ob;
    }

    function compare(other) { // a function to stand for the operator "<"
        if (!isValid() || !other.isValid())
            return false;
        if (eventHander.priority !== other.eventHander.priority)
            return eventHander.priority > other.eventHander.priority
        if (skillInvoker !== other.skillInvoker)
            return ServerPlayer.sortByActionOrder(skillInvoker,other.skillInvkoer);
        return eventHander.isEquipSkill && !other.eventHander.isEquipSkill
    }

    function toList() {
        var result = new Array;
        if (!isValid()){
            for (var i = 1; i < 4; i++)
                result.push("");
        } else {
            result.push(eventHander.objectName);
            result.push(skillOwner.objectName);
            result.push(skillInvoker.objectName);
            result.push(preferredTarget.objectName)
        }
        return result;
    }
}