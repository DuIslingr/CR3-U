#include "viewer/viewer.hpp"

namespace cr3_viewer {

// the rules' party: the genes each wears (their levels, slots, the items' additions) and their
// equipment (the save's - CrxCharacter's), for a battle and the camp's numbers
void Viewer::cr3PartyRules() {
    cr3Rules.partyGenes.clear(); // (the party's genes: what each wears)
    if (game.cr3Db)
        for (const auto& [ch, uid] : game.cr3GeneOf) {
            const int k = game.wornGene(ch);
            if (k < 0) continue;
            const cr3::Cr3Gene& g = game.cr3Genes[size_t(k)];
            cr3::Cr3BattleRules::PartyGene pg;
            pg.master = g.master, pg.level = game.cr3Db->geneLevel(g);
            for (const auto& sl : g.slots)
                if (sl[1] >= 0) pg.slots.push_back({sl[0], sl[1]});
            for (int a = 0; a < 4; ++a) pg.adds[a] = g.adds[a];
            cr3Rules.partyGenes[ch] = pg;
        }
    cr3Rules.teamLevel = game.cr3TeamLevel;
    cr3Rules.attachments.clear();
    for (const auto& [equipment, a] : game.cr3Attachments)
        if (a.first > 0) cr3Rules.attachments[equipment] = a.first;
    cr3Rules.partyEquip.clear();
    for (int slot = 0; slot < 3; ++slot) {
        const std::map<int, int>& worn = slot == 0 ? game.cr3Weapon : slot == 1 ? game.cr3Armour : game.cr3Accessory;
        for (const auto& [ch, id] : worn) {
            auto it = cr3Rules.partyEquip.find(ch);
            if (it == cr3Rules.partyEquip.end()) it = cr3Rules.partyEquip.emplace(ch, std::array<int, 3>{-1, -1, -1}).first;
            it->second[size_t(slot)] = std::max(id, 0);
        }
    }
    if (cr3BattleTablesLoaded) // (no armour put on: the new game's - Cr3BattleTables::defaultArmour)
        for (const int ch : game.cr3Party) {
            if (ch <= 0 || game.cr3Armour.count(ch)) continue;
            auto it = cr3Rules.partyEquip.find(ch);
            if (it == cr3Rules.partyEquip.end()) it = cr3Rules.partyEquip.emplace(ch, std::array<int, 3>{-1, -1, -1}).first;
            it->second[1] = cr3BattleTables.defaultArmour(ch);
        }
}

// a party member's MP now (what its last battle left, else its most)
int Viewer::cr3CharaMp(int ch, int most) const {
    const auto it = game.cr3Mp.find(ch);
    return it != game.cr3Mp.end() ? std::clamp(it->second, 0, most) : most;
}

// a party member's numbers now (Cr3BattleRules::playerStats: HP, MP - the most -, attack ..)
bool Viewer::cr3CharaStats(int ch, cr3::Cr3Fighter& f) {
    if (!cr3TablesReady()) return false;
    cr3PartyRules();
    return cr3Rules.playerStats(cr3BattleTables, ch, f);
}

bool Viewer::cr3TablesReady() {
    std::string err;
    if (!cr3BattleTablesLoaded && !(cr3BattleTablesLoaded = cr3BattleTables.load(archive, &err))) SDL_Log("tables: %s", err.c_str());
    return cr3BattleTablesLoaded;
}

// a hero's parts model's body (j001 -> j001b02): its armour's (CrxModel::GetBodyModelNo)
std::string Viewer::cr3PartsBody(const std::string& model) {
    const int ch = model.size() == 4 && model[0] == 'j' ? std::atoi(model.c_str() + 1) : 0;
    if (ch <= 0 || !cr3TablesReady()) return model + "b01";
    const auto it = game.cr3Armour.find(ch);
    return cr3BattleTables.body(model, it != game.cr3Armour.end() ? it->second : cr3BattleTables.defaultArmour(ch));
}

bool Viewer::runCr3BattleScript(const char* fn, bool quiet) {
    cr3BattleEvents = std::make_unique<cr3::EventRuntime>(archive, game);
    cr3BattleEvents->language = optionLanguage, cr3BattleEvents->autoPage = pageSeconds, cr3BattleEvents->askYesNo = true;
    // (BattleStatus: the battle's units by an id - CR3-U's the fighter's index -; their status
    // effects, HP, level)
    cr3BattleEvents->extraNatives = [this](cr3::ScriptVM& vm) {
        auto nth = [this](bool player, int n) {
            for (size_t k = 0; k < cr3Rules.fighters.size(); ++k)
                if (cr3Rules.fighters[k].player == player && n-- == 0) return int(k);
            return -1;
        };
        auto fighter = [this](int id) -> const cr3::Cr3Fighter* {
            return id >= 0 && size_t(id) < cr3Rules.fighters.size() ? &cr3Rules.fighters[size_t(id)] : nullptr;
        };
        // (Battle: BtlScript::Battle - the turn - BtlResult +0, counted at each command selection -,
        // the monsters fallen, the result's EXP and money - +4 / +8 added to, +0x28 / +0x2c factors
        // multiplied -, the end forced - BtlSystem::s_ResultParam 0 won, 3 over -, the break
        // gauge as shown, the field)
        vm.setNative("Battle", "GetTurn", [this](cr3::NativeCall& c) { c.result(cr3Turn); });
        bindQuakeNatives(vm); // (a battle's Cr3System.StartQuake - BattleDeathDirection's, the bosses' -: the root's QuakeControlTask)
        // (BtlScript's BattleUnit / BattleCamera - Cr3BindBattle 0x00854b98 -: the bosses' deaths -
        // BattleDeathDirection -, the event cameras - ChangeCameraAfterLastAttack's Cam_event_start)
        auto unitOf = [this](int id) -> int {
            return id >= 0 && size_t(id) < cr3Rules.fighters.size() && cr3Rules.fighters[size_t(id)].unit >= 0 &&
                           size_t(cr3Rules.fighters[size_t(id)].unit) < cr3BattleUnits.size()
                       ? cr3Rules.fighters[size_t(id)].unit
                       : -1;
        };
        // (BattleUnit::PlayAnimation 0x009a7b5c -> BtlUnitList::PlayAnimation 0x009d802c: AnimCommon's
        // type, played from its start without a blend; at its end - BtlUnit::updateAnimationEnd
        // 0x009e10c4 - types 1, 3, 4, 5, 7 .. 10, 14 .. 17 back to the stance, the rest held. Type 6
        // the death - BtlUnitList::SetDie 0x009da470 first -: the unit stays in sight for it)
        vm.setNative("BattleUnit", "PlayAnimation", [this, unitOf](cr3::NativeCall& c) {
            const int k = unitOf(c.integer(0)), type = c.integer(1);
            const std::string suffix = cr3MotionOf(type);
            if (k < 0 || suffix.empty()) return;
            const bool back = type == 4 || (type >= 0 && type < 32 && ((0x3c7aau >> type) & 1u) != 0);
            if (type == 6) cr3BattleUnits[size_t(k)].opacity = Ramp();
            setCr3Motion(size_t(k), suffix, type == 0, !back);
            // (then BtlUnit::PlayCamera 0x9dd234: the motion's own camera - AnimDirection's - posed on the
            // unit; a script's BattleCamera.Play straight after - a death direction's - takes over)
            const std::string camera = cr3BattleTables.unitCamera(cr3Battle.units[size_t(k)].model, type);
            if (!camera.empty()) {
                const Cr3BattleUnitView& u = cr3BattleUnits[size_t(k)];
                const float step = u.meleeStep.at(cr3BattleClock);
                const cr3::Vec3 home = cr3Battle.units[size_t(k)].home;
                const cr3::Vec3 at{home[0] + u.meleeOffset[0] * step, home[1], home[2] + u.meleeOffset[2] * step};
                playCr3CasterCamera(camera, size_t(k), &at);
                SDL_Log("battle %.2f s: unit %d's motion %d camera %s", cr3BattleClock, k, type, camera.c_str());
            }
        });
        // (BattleUnit::PlayEffect 0x009a7d24 -> BtlUnit::PlayHitEffect 0x009de81c: Effect's figure
        // at the model's effect point - placed once, not following -, scaled, its own end)
        vm.setNative("BattleUnit", "PlayEffect", [this, unitOf](cr3::NativeCall& c) {
            const int k = unitOf(c.integer(0));
            if (k >= 0) spawnCr3Effect(c.integer(1), cr3EffectPoint(size_t(k), c.integer(2)), float(c.number(3, 1.0)));
        });
        // (BattleUnit::Hide 0x009a7c30 -> BtlUnit::DisableDraw 0x009e02b8: at once, no fade)
        vm.setNative("BattleUnit", "Hide", [this, unitOf](cr3::NativeCall& c) {
            if (const int k = unitOf(c.integer(0)); k >= 0) cr3BattleUnits[size_t(k)].scriptHidden = true;
        });
        // (BattleCamera::Play 0x009a65f0 -> BtlCameraSystem::Play 0x009772dc: the clip's own figure -
        // a Cam_bt one the battle's -, from the origin, not looped, held at its end until another)
        vm.setNative("BattleCamera", "Play", [this](cr3::NativeCall& c) {
            const std::string name = c.string(0);
            cr3ScriptCamera = true;
            if (name.find("Cam_bt") != std::string::npos) {
                playCr3Camera(name, std::max(cr3BattleCam.duration(), 0.1f), false, {0, 0, 0}, 0, 0);
                return;
            }
            cr3::Cr3BattleCamera camera;
            if (!camera.load(archive, name) || !camera.has(name)) {
                SDL_Log("battle camera %s: not found", name.c_str());
                return;
            }
            cr3Cam = Cr3BattleCamPlay();
            cr3Cam.name = name, cr3Cam.start = cr3BattleClock, cr3Cam.end = camera.duration();
            cr3BattleOwnCam = std::move(camera);
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: script camera %s", cr3BattleClock, name.c_str());
        });
        // (BattleCamera::SetPosition 0x009a665c -> BtlCameraSystem::SetPosition 0x009792d0: the
        // clip's origin moved there at once - world space; Play put it back at 0)
        vm.setNative("BattleCamera", "SetPosition", [this](cr3::NativeCall& c) {
            cr3Cam.actor = {float(c.number(0)), float(c.number(1)), float(c.number(2))};
        });
        // (Battle::StopAuto 0x009a82f4 -> BtlInterfaceSystem::StopAuto 0x009a4028: the player's
        // Auto Battle / Mass Attack off - battle_0000's BattleEventStartSet, before a scripted event)
        vm.setNative("Battle", "StopAuto", [this](cr3::NativeCall&) { stopCr3Auto("the script"); });
        // (BattleUnit::GetAttachId 0x009a8080 -> BtlUnitList::GetAttachId 0x009da9a8: the id of the
        // unit it holds - BtlUnit +0x238's +0xe0 -, -1 none: battle 7's every direction end)
        vm.setNative("BattleUnit", "GetAttachId", [this](cr3::NativeCall& c) {
            const auto it = cr3Held.find(c.integer(0, -1));
            c.result(it != cr3Held.end() ? it->second.first : -1);
        });
        // (BattleUnit::Attach 0x009a7ca0 -> BtlUnitList::Attach 0x009d9e70 -> BtlUnit::Attach
        // 0x009dfdd8: the holder's +0x238 the held, +0x240 the effect point it hangs on)
        vm.setNative("BattleUnit", "Attach", [this, unitOf](cr3::NativeCall& c) {
            const int holder = c.integer(0, -1), held = c.integer(1, -1);
            if (unitOf(holder) >= 0 && unitOf(held) >= 0) cr3Held[holder] = {held, c.integer(2, 0)};
        });
        // (BattleDirection::InsertRelease 0x009a4d98 / InsertWaitDie 0x009a4dc0 ->
        // BtlDirectionManager::Insert 0x009915a4: a chunk of the unit's with no command, its
        // direction - 0x27 BtlReleaseDirection, 0x28 BtlWaitDieDirection - after the current one,
        // in the order inserted)
        auto insertDirection = [this](int id, int kind) {
            if (id < 0 || size_t(id) >= cr3Rules.fighters.size()) return;
            size_t at = std::min(cr3ActionAt, cr3Actions.size());
            while (at < cr3Actions.size() && (cr3Actions[at].command == kCr3ReleaseDirection || cr3Actions[at].command == kCr3WaitDieDirection)) ++at;
            cr3::Cr3Action a;
            a.source = id, a.command = kind, a.gaugeAfter = cr3GaugeShown;
            cr3Actions.insert(cr3Actions.begin() + long(at), a);
        };
        vm.setNative("BattleDirection", "InsertRelease", [insertDirection](cr3::NativeCall& c) { insertDirection(c.integer(0, -1), kCr3ReleaseDirection); });
        vm.setNative("BattleDirection", "InsertWaitDie", [insertDirection](cr3::NativeCall& c) { insertDirection(c.integer(0, -1), kCr3WaitDieDirection); });
        // (BattleDirection::GetNextCommandId 0x009a4de8 -> BtlDirectionManager::GetNextCommandId
        // 0x009922ac: the direction after the current one's command, -1 none - a guard has no
        // direction; an inserted one, or one unable to act, no command)
        vm.setNative("BattleDirection", "GetNextCommandId", [this](cr3::NativeCall& c) {
            size_t at = cr3ActionAt;
            while (at < cr3Actions.size() && cr3Actions[at].command == -1) ++at;
            c.result(at < cr3Actions.size() && cr3Actions[at].command >= 0 ? cr3Actions[at].command : -1);
        });
        // (BattleUnit::SetHomePosition 0x009a7fec: BtlUnit::SetHomePosition 0x009e021c, then
        // ReturnHome 0x009dc5c8 - placed there at once)
        vm.setNative("BattleUnit", "SetHomePosition", [this, unitOf](cr3::NativeCall& c) {
            const int k = unitOf(c.integer(0, -1));
            if (k < 0) return;
            cr3Battle.units[size_t(k)].home = {float(c.number(1)), float(c.number(2)), float(c.number(3))};
            cr3BattleUnits[size_t(k)].meleeOffset = {}, cr3BattleUnits[size_t(k)].meleeStep = Ramp{0, 0};
        });
        // (BattleUnit::HideMesh 0x009a7c68 -> BtlUnit::HideMesh 0x009e013c: Figure::SetVisible of
        // the Model row's +0x10 name, false - m222's "m222_sub" alone, which no figure or joint in
        // the release has: nothing to hide)
        vm.setNative("BattleUnit", "HideMesh", [](cr3::NativeCall&) {});
        // (the addition units - BtlUnit::PlayEffectToAdditionUnit / PlayHitEffectToAdditionUnit /
        // PauseAdditionUnit / ResumeAdditionUnit, 0x009a7dbc .. 0x009a7fb4; DeleteEffect 0x009a7af8
        // kills what PlayEffectToAdditionUnit left in its slot: battle 54's cards - CR3-U has no
        // addition units, so none of these has anything to act on)
        for (const char* fn : {"PlayEffectToAdditionUnit", "PlayHitEffectToAdditionUnit", "PauseAdditionUnit", "ResumeAdditionUnit", "DeleteEffect"})
            vm.setNative("BattleUnit", fn, [](cr3::NativeCall&) {});
        // (BtlScript::BattleModel 0x009a6960 .. 0x009a6c6c: a model of the script's own in a slot -
        // Create, a CrxModel by name; PlayAnimation its <name>_<anim>, looped; SetPosition;
        // SetRotation in degrees - battles 15 and 29's guest, c104 / c106; PlayEffect: an Effect
        // at a point, scale 1)
        vm.setNative("BattleModel", "Create", [this](cr3::NativeCall& c) {
            Cr3ScriptModel& m = cr3ScriptModels[c.integer(0)];
            unload(m.figure), m.figure = ViewFigure();
            std::string err;
            if (!loadFigure(c.string(1) + ".geom", m.figure, &err)) SDL_Log("battle model %s: %s", c.string(1).c_str(), err.c_str());
        });
        vm.setNative("BattleModel", "PlayAnimation", [this](cr3::NativeCall& c) {
            const auto it = cr3ScriptModels.find(c.integer(0));
            if (it == cr3ScriptModels.end() || it->second.figure.name.size() < 6) return;
            ViewFigure& f = it->second.figure;
            const std::string want = f.name.substr(0, f.name.size() - 5) + "_" + c.string(1) + ".anim";
            const auto a = std::find(f.anims.begin(), f.anims.end(), want);
            if (a != f.anims.end()) loadAnim(f, size_t(a - f.anims.begin()));
            else SDL_Log("battle model: no %s", want.c_str());
        });
        vm.setNative("BattleModel", "SetPosition", [this](cr3::NativeCall& c) {
            cr3ScriptModels[c.integer(0)].at = {float(c.number(1)), float(c.number(2)), float(c.number(3))};
        });
        vm.setNative("BattleModel", "SetRotation", [this](cr3::NativeCall& c) {
            cr3ScriptModels[c.integer(0)].rotation = {float(c.number(1)), float(c.number(2)), float(c.number(3))};
        });
        vm.setNative("BattleModel", "PlayEffect", [this](cr3::NativeCall& c) { // (its rotation not followed: CR3-U's effects drawn upright)
            spawnCr3Effect(c.integer(0), {float(c.number(1)), float(c.number(2)), float(c.number(3))}, 1.0f);
        });
        // (Battle::AddFieldObject 0x009a82c8 -> BtlField::AddObject 0x0099c80c -> BtlFieldObject::
        // Initialize 0x0099bb4c: the figure and its animator, looped, on the field figure's joint -
        // the final battle's planets)
        vm.setNative("Battle", "AddFieldObject", [this](cr3::NativeCall& c) {
            Cr3FieldObject o;
            std::string err;
            const std::string name = c.string(0);
            if (!loadFigure(name + ".geom", o.figure, &err)) return (void)SDL_Log("battle field object %s: %s", name.c_str(), err.c_str());
            const auto a = std::find(o.figure.anims.begin(), o.figure.anims.end(), name + ".anim");
            if (a != o.figure.anims.end()) loadAnim(o.figure, size_t(a - o.figure.anims.begin()));
            o.joint = c.string(1);
            cr3FieldObjects.push_back(std::move(o));
        });
        // (Battle::SetFieldAnimatorEndTime 0x009a8198 -> BtlField::SetEndTime 0x0099c928: the field
        // animator's +0x7c; ResetFieldAnimatorLooping 0x009a81bc -> BtlField::ResetLooping
        // 0x0099c968: its +100 bit 0 off whatever the argument - played on to the end time, held)
        vm.setNative("Battle", "SetFieldAnimatorEndTime", [this](cr3::NativeCall& c) { cr3FieldEnd = float(c.number(0)); });
        vm.setNative("Battle", "ResetFieldAnimatorLooping", [this](cr3::NativeCall&) { cr3FieldLoop = false; });
        // (Battle::OpenUI 0x009a8290 / CloseUI 0x009a82ac: BtlBreakGaugeUI and BtlStatusUI opened /
        // closed)
        vm.setNative("Battle", "OpenUI", [this](cr3::NativeCall&) { cr3BattleUiClosed = false; });
        vm.setNative("Battle", "CloseUI", [this](cr3::NativeCall&) { cr3BattleUiClosed = true; });
        // (Battle::IsWildCard 0x009a8414: in a VS battle - GameMain +0x24b -, the VS tournament's
        // current row's +0x6c; CR3-U: its tournament deals wildcards)
        vm.setNative("Battle", "IsWildCard", [this](cr3::NativeCall& c) { c.result(vs.screen == 5 && game.cr3Db && !game.cr3Db->wildcards().empty()); });
        // (BattleCommand::SetStart 0x009a6044: +0x18 / +0x1c - BtlSysStateSelect::phaseGetSetStart
        // 0x009d3b2c opens with that unit's command: no menu, no opening - battle 68's)
        vm.setNative("BattleCommand", "SetStart", [this](cr3::NativeCall& c) { cr3StartCommand = {c.integer(0, -1), c.integer(1, -1)}; });
        vm.setNative("Battle", "GetNumberOfKilledNonPlayer", [this](cr3::NativeCall& c) {
            int n = 0;
            for (const cr3::Cr3Fighter& f : cr3Rules.fighters) n += !f.player && f.dead();
            c.result(n);
        });
        vm.setNative("Battle", "AddExperience", [this](cr3::NativeCall& c) { cr3ResultAdd[0] += c.integer(0); });
        vm.setNative("Battle", "AddMoney", [this](cr3::NativeCall& c) { cr3ResultAdd[1] += c.integer(0); });
        vm.setNative("Battle", "SetExperienceFactor", [this](cr3::NativeCall& c) { cr3ResultFactor[0] *= c.integer(0, 1); });
        vm.setNative("Battle", "SetMoneyFactor", [this](cr3::NativeCall& c) { cr3ResultFactor[1] *= c.integer(0, 1); });
        vm.setNative("Battle", "ForceVictory", [this](cr3::NativeCall&) { cr3ForcedEnd = 0; });
        vm.setNative("Battle", "ForceEnd", [this](cr3::NativeCall&) { cr3ForcedEnd = 3; });
        vm.setNative("Battle", "HasAdvantageToPlayer", [this](cr3::NativeCall& c) { c.result(cr3GaugeShown > 0); });
        vm.setNative("Battle", "HasAdvantageToEnemy", [this](cr3::NativeCall& c) { c.result(cr3GaugeShown < 0); });
        vm.setNative("Battle", "AddBreakPoint", [this](cr3::NativeCall& c) { cr3Rules.addBreak(c.integer(0)), cr3GaugeShown = cr3Rules.gauge; });
        vm.setNative("Battle", "GetBreakPoint", [this](cr3::NativeCall& c) { c.result(cr3GaugeShown); });
        vm.setNative("Battle", "GetFiledId", [this](cr3::NativeCall& c) { c.result(cr3BattleFieldId); });
        vm.setNative("Battle", "AddFieldStatusEffect", [this](cr3::NativeCall& c) { cr3FieldEffects.push_back(c.integer(0)); });
        // (Cr3System.SetVistaFuncName in a battle - Fld2SetVistaFuncName: Cr3ScriptWork's reserve 3 -
        // the vista after it: the final battle's stages, s484_t01_01501_evt at its ForceEnd)
        vm.setNative("Cr3System", "SetVistaFuncName", [this](cr3::NativeCall& c) {
            game.next.vistaFunction = c.string(0), game.next.vista = true, cr3BattleVista = true;
        });
        vm.setNative("Battle", "SetFieldVisualEffect", [this](cr3::NativeCall& c) { // (BtlField::SetVisualEffect: fe201am10, the lab's rain)
            const std::string model = c.string(0);
            dropSpark(cr3BattleSpark), cr3BattleSpark.fx.reset(), cr3BattleSparkClock = cr3BattleClock;
            if (const cr3::SparkAsset* a = sparkAsset(model)) cr3BattleSpark.fx = std::make_unique<cr3::SparkEffect>(a, uint32_t(SDL_GetTicks()) | 1u);
            else SDL_Log("battle %d: visual effect %s (no particles)", cr3Battle.coupling, model.c_str());
        });
        // (BattleArgument - BtlScript::BattleArgument: the source set, the damage chunk's command,
        // whether it downed - WasDown -, was a cooperation - IsCooperation)
        auto argAction = [this]() -> const cr3::Cr3Action* {
            return cr3ArgAction >= 0 && size_t(cr3ArgAction) < cr3Actions.size() ? &cr3Actions[size_t(cr3ArgAction)] : nullptr;
        };
        vm.setNative("BattleArgument", "GetSource", [this](cr3::NativeCall& c) { c.result(cr3ArgSource); });
        vm.setNative("BattleArgument", "GetCommand", [argAction](cr3::NativeCall& c) { const auto* a = argAction(); c.result(a ? a->command : -1); });
        // (BattleArgument::GetActionType 0x009a4d38 -> BtlDamageChunk::GetActionType 0x0097e450:
        // the chunk's - else the last direction's - command's MbCommandInfo +0x18 row's +0x04: its
        // CommandDirection row's kind (MbCommandInfo::CreateDirection 0x00912438 files it there;
        // the kind BtlDirectionManager::CreateDirection 0x00990d98 switches on), -1 without one;
        // 5..8 and 22 read 0x20..0x24 when the chunk's +0x20 is set - not modelled: the scripts ask
        // only after 9, 10, 20, 30 and 39 - ChangeCameraAfterLastAttack's Cam_event_start)
        vm.setNative("BattleArgument", "GetActionType", [this, argAction](cr3::NativeCall& c) {
            const auto* a = argAction();
            cr3::Cr3Command cmd;
            c.result(a && a->command >= 0 && cr3Rules.command(a->command, cmd) ? cmd.direction : -1);
        });
        // (BattleArgument::IsCombo 0x009a4ccc: the chunk's +0x9e; WasBreak 0x009a4d0c: +0xa8 -
        // BtlDamageChunk::VUpdate's target-list events 2 and 3; WasHitStatusEffect 0x009a4cf8 ->
        // BtlDamageChunk::WasHitStatusEffect 0x0097e774: a status effect landed on any target;
        // GetElement 0x009a4ce4 -> BtlDamageChunk::GetElement 0x0097e7e8: +0x24, its command's +0x24)
        vm.setNative("BattleArgument", "IsCombo", [argAction](cr3::NativeCall& c) { const auto* a = argAction(); c.result(a && a->combo); });
        vm.setNative("BattleArgument", "WasBreak", [argAction](cr3::NativeCall& c) { const auto* a = argAction(); c.result(a && a->broke); });
        vm.setNative("BattleArgument", "WasHitStatusEffect", [argAction](cr3::NativeCall& c) {
            bool hit = false;
            if (const auto* a = argAction())
                for (const cr3::Cr3Hit& th : a->targets) hit |= !th.effectsGiven.empty();
            c.result(hit);
        });
        vm.setNative("BattleArgument", "GetElement", [this, argAction](cr3::NativeCall& c) {
            const auto* a = argAction();
            cr3::Cr3Command cmd;
            c.result(a && a->command >= 0 && cr3Rules.command(a->command, cmd) ? cmd.element : 0);
        });
        vm.setNative("BattleArgument", "WasCooperated", [argAction](cr3::NativeCall& c) { const auto* a = argAction(); c.result(a && a->cooperated()); });
        vm.setNative("BattleArgument", "WasDown", [argAction](cr3::NativeCall& c) {
            bool down = false;
            if (const auto* a = argAction())
                for (const cr3::Cr3Hit& th : a->targets)
                    for (uint8_t fl : th.flags) down |= (fl & 2) != 0;
            c.result(down);
        });
        // (BattleCommand.Add(unit, actions, command) - BtlStatusList::AddCommand: its command this
        // turn; SetId / SetIdAndTarget - commands of no unit's, played after the defeat's or the
        // turn's end script as actions with their own directions: Cr3BattleRules::scriptCommands, endCr3Actions)
        vm.setNative("BattleCommand", "Add", [this](cr3::NativeCall& c) {
            if (c.integer(0, -1) >= 0) cr3Rules.scripted[c.integer(0)].push_back({c.integer(1, 1), c.integer(2)});
        });
        // (BattleStatus.AddEnemyOrderPoint(type, n) 0x009c2658: the monsters' think points of a select type)
        vm.setNative("BattleStatus", "AddEnemyOrderPoint", [this](cr3::NativeCall& c) { cr3Rules.addEnemyOrderPoint(c.integer(0, -1), c.integer(1)); });
        vm.setNative("BattleCoupling", "SetId", [this](cr3::NativeCall& c) { cr3NextCoupling = c.integer(0, -1); });
        vm.setNative("BattleCommand", "SetId", [this](cr3::NativeCall& c) {
            cr3ScriptCommands.push_back({c.integer(0), -1});
            SDL_Log("battle: the script queued command %d", c.integer(0));
        });
        vm.setNative("BattleCommand", "SetIdAndTarget", [this](cr3::NativeCall& c) {
            cr3ScriptCommands.push_back({c.integer(0), c.integer(1, -1)});
            SDL_Log("battle: the script queued command %d on %d", c.integer(0), c.integer(1, -1));
        });
        vm.setNative("BattleStatus", "GetIdByPlayerIndex", [nth](cr3::NativeCall& c) { c.result(nth(true, c.integer(0))); });
        vm.setNative("BattleStatus", "GetIdByNonPlayerIndex", [nth](cr3::NativeCall& c) { c.result(nth(false, c.integer(0))); });
        vm.setNative("BattleStatus", "GetIdByCharId", [this](cr3::NativeCall& c) {
            int id = -1;
            for (size_t k = 0; k < cr3Rules.fighters.size(); ++k)
                if (cr3Rules.fighters[k].player && cr3Rules.fighters[k].id == c.integer(0)) id = int(k);
            c.result(id);
        });
        vm.setNative("BattleStatus", "AddExtraSkill", [this](cr3::NativeCall& c) { // (a skill for the battle: the guest hero's 430)
            if (c.integer(0, -1) >= 0 && size_t(c.integer(0, -1)) < cr3Rules.fighters.size())
                cr3Rules.fighters[size_t(c.integer(0))].skills.push_back(c.integer(1));
        });
        vm.setNative("BattleStatus", "GiveStatusEffect", [this](cr3::NativeCall& c) { cr3Rules.giveEffect(c.integer(0, -1), c.integer(1)); });
        vm.setNative("BattleStatus", "RemovesStatusEffect", [this](cr3::NativeCall& c) { cr3Rules.removeEffect(c.integer(0, -1), c.integer(1)); });
        // (BtlStatusList::HaveStatusEffectToDirection: the direction's effects - 1, KO, taken as a
        // unit falls - BtlStatus::TriggerSkillToDirection's trigger 7 -, CountKilledMonster's)
        vm.setNative("BattleStatus", "RequestModeChange", [this](cr3::NativeCall& c) {
            if (c.integer(0, -1) >= 0) cr3Rules.modeRequests.push_back({c.integer(0), c.integer(1)});
        });
        vm.setNative("BattleStatus", "HaveStatusEffect", [this, fighter](cr3::NativeCall& c) {
            const int id = c.integer(0, -1), effect = c.integer(1);
            const auto* f = fighter(id);
            c.result(cr3Rules.hasEffect(id, effect) || (effect == 1 && f && f->dead()));
        });
        vm.setNative("BattleStatus", "GetIdByStatusEffect", [this](cr3::NativeCall& c) { // (the first unit with it; -1 none)
            int id = -1;
            for (size_t k = 0; k < cr3Rules.fighters.size() && id < 0; ++k)
                if (cr3Rules.hasEffect(int(k), c.integer(0))) id = int(k);
            c.result(id);
        });
        vm.setNative("BattleStatus", "GetStatusEffectTurn", [this](cr3::NativeCall& c) {
            int turns = 0;
            if (c.integer(0, -1) >= 0 && size_t(c.integer(0, -1)) < cr3Rules.fighters.size())
                for (const auto& [e, t] : cr3Rules.fighters[size_t(c.integer(0))].effects)
                    if (e == c.integer(1)) turns = t;
            c.result(turns);
        });
        // (GetHp: the HP as shown - the direction's, BtlStatus +0x88: the turn's sums are ahead of it)
        vm.setNative("BattleStatus", "GetHp", [this](cr3::NativeCall& c) {
            const int id = c.integer(0, -1);
            c.result(id >= 0 && size_t(id) < cr3HpShown.size() ? cr3HpShown[size_t(id)] : 0);
        });
        // (BattleStatus::GetMode 0x009a6434: BtlStatus's VGetMode - BtlMonsterStatus 0x009c7464 its
        // +0x1fc, VChangeMode's; BtlCharacterStatus 0x009b2c9c 0)
        vm.setNative("BattleStatus", "GetMode", [fighter](cr3::NativeCall& c) {
            const auto* f = fighter(c.integer(0, -1));
            c.result(f ? (f->player ? 0 : f->mode) : -1);
        });
        // (BattleStatus::HasEffect 0x009a64b0: an effect of the kind - BtlStatus::HasEffect 0x009b6af0,
        // its status effects' -, else its skills' - BtlSkillList::HasEnableEffect with its side's
        // gauge advantage)
        vm.setNative("BattleStatus", "HasEffect", [this, fighter](cr3::NativeCall& c) {
            const auto* f = fighter(c.integer(0, -1));
            c.result(f && (cr3Rules.hasKind(*f, c.integer(1)) || cr3Rules.hasSkillKind(*f, c.integer(1))));
        });
        // (BattleStatus::AddMp 0x009a6260: its MP + n - BtlStatus::SetMpToCalculation and
        // SetMpToDirection: the rules' and the shown at once)
        vm.setNative("BattleStatus", "AddMp", [this](cr3::NativeCall& c) {
            const int id = c.integer(0, -1);
            if (id < 0 || size_t(id) >= cr3Rules.fighters.size()) return;
            cr3::Cr3Fighter& f = cr3Rules.fighters[size_t(id)];
            f.mp = std::clamp(f.mp + c.integer(1), 0, f.maxMp);
            if (size_t(id) < cr3MpShown.size()) cr3MpShown[size_t(id)] = f.mp;
        });
        vm.setNative("BattleStatus", "GetLevel", [fighter](cr3::NativeCall& c) { const auto* f = fighter(c.integer(0, -1)); c.result(f ? f->level : 0); });
        vm.setNative("BattleStatus", "GetMonsterId", [fighter](cr3::NativeCall& c) { const auto* f = fighter(c.integer(0, -1)); c.result(f && !f->player ? f->id : -1); });
        vm.setNative("BattleStatus", "GetElement", [fighter](cr3::NativeCall& c) { const auto* f = fighter(c.integer(0, -1)); c.result(f ? f->element : 0); });
        // (LowHp(id, share) - BtlStatus::IsDirectionHpLow, BtlBasicStatus::IsLowHp: the HP as shown /
        // the most at or under the share - battle_0007's 0.4)
        vm.setNative("BattleStatus", "LowHp", [this, fighter](cr3::NativeCall& c) {
            const int id = c.integer(0, -1);
            const auto* f = fighter(id);
            const int hp = id >= 0 && size_t(id) < cr3HpShown.size() ? cr3HpShown[size_t(id)] : f ? f->hp : 0;
            c.result(f && f->maxHp > 0 && float(hp) / float(f->maxHp) <= c.number(1, 0.2f));
        });
    };
    cr3BattleEventClock = 0;
    if (!cr3BattleEvents->start(cr3Battle.script(), fn)) {
        if (!quiet) SDL_Log("battle script %s: %s", fn, cr3BattleEvents->error().c_str());
        return false;
    }
    SDL_Log("battle script %s:%s", cr3Battle.script().c_str(), fn);
    return true;
}

// BtlCameraSystem::Load / changeMotion: a Cam_bt camera, else a figure of its own
void Viewer::playCr3Camera(const std::string& name, float end, bool loop, const std::array<float, 3>& actor, float yaw, float shift) {
    cr3Cam = Cr3BattleCamPlay();
    cr3Cam.name = name.substr(0, name.find('\n')); // (a list: the first - the game picks by the attack)
    cr3Cam.start = cr3BattleClock, cr3Cam.end = end, cr3Cam.loop = loop;
    cr3Cam.actor = actor, cr3Cam.actorYaw = yaw, cr3Cam.screenShift = shift;
    if (cr3Cam.name.find("Cam_bt") == std::string::npos && !cr3BattleOwnCam.load(archive, cr3Cam.name))
        SDL_Log("battle camera %s: not found", cr3Cam.name.c_str());
}

void Viewer::requestCr3Battle(int coupling, int fieldId) { cr3BattleRequest.active = true, cr3BattleRequest.coupling = coupling, cr3BattleRequest.field = fieldId; }

void Viewer::startRequestedCr3Battle(int fw, int fh) {
    cr3BattleRequest.active = false;
    bool caught = false;
    if (game.battleInColorSet) {
        game.battleInColorSet = false;
        cr3::Image img;
        img.width = img.height = 8;
        const uint8_t rgba[4] = {
            uint8_t(std::clamp(game.battleInColor[0], 0.0f, 1.0f) * 255.0f + 0.5f),
            uint8_t(std::clamp(game.battleInColor[1], 0.0f, 1.0f) * 255.0f + 0.5f),
            uint8_t(std::clamp(game.battleInColor[2], 0.0f, 1.0f) * 255.0f + 0.5f), 255};
        img.rgba.resize(8 * 8 * 4);
        for (size_t k = 0; k < img.rgba.size(); k += 4) std::memcpy(&img.rgba[k], rgba, 4);
        if (cr3GlassTexture.id) dev->destroy(cr3GlassTexture);
        cr3GlassTexture = dev->upload(img);
        caught = cr3GlassTexture.id != 0;
    } else {
        draw(fw, fh); // (the frame as it was)
        dev->endFrame(false);
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        caught = dev->readPixels(px, w, h) && frameW > 0 && frameH > 0 && frameX + frameW <= w && frameY + frameH <= h;
        if (caught) {
            cr3::Image img;
            img.width = frameW, img.height = frameH;
            img.rgba.resize(size_t(frameW) * frameH * 4);
            for (int y = 0; y < frameH; ++y) { // (bottom-up: a render target's v = 0 its bottom)
                const uint8_t* row = &px[(size_t(frameY + frameH - 1 - y) * w + frameX) * 4];
                std::memcpy(&img.rgba[size_t(y) * frameW * 4], row, size_t(frameW) * 4);
            }
            for (size_t k = 3; k < img.rgba.size(); k += 4) img.rgba[k] = 255;
            if (cr3GlassTexture.id) dev->destroy(cr3GlassTexture);
            cr3GlassTexture = dev->upload(img);
        }
    }
    if (!startCr3Battle(cr3BattleRequest.coupling, cr3BattleRequest.field)) return;
    eventMode = areaMode = false;
    if (game.next.battleMusic >= 0) audio.playBgm(game.next.battleMusic, 0); // (BtlSystem's start: the boot's music)
    if (caught) {
        int hw = 0, hh = 0;
        glassSize(hw, hh);
        cr3GlassRng.seed(uint32_t(SDL_GetTicks()));
        cr3Glass.build(hw, hh, cr3GlassRng, game.glassPattern);
        game.glassPattern = 1; // (PostEffectGlassFragment::Setup: resets GameSystem +0xb4 to 1)
        cr3GlassStart = cr3BattleClock;
        audio.playSe(0x253, false); // (GameMain::StartGlassEffect)
        SDL_Log("the screen breaks away: %zu shards", cr3Glass.shards.size());
    }
}

void Viewer::drawCr3Glass() {
    if (!cr3Glass.running() || !cr3GlassTexture.id) return;
    Mat4 proj = cr3::mat4Identity();
    proj[0] = 1.0f / float(cr3Glass.w), proj[5] = 1.0f / float(cr3Glass.h);
    const float rgba[4] = {1, 1, 1, cr3Glass.alpha()};
    for (const cr3::GlassShard& sh : cr3Glass.shards) {
        float v[24];
        for (int k = 0; k < 6; ++k) { // (the triangle, then its last corner thrice: nothing)
            const int c = k < 3 ? k : 2;
            v[k * 4 + 0] = sh.x[c], v[k * 4 + 1] = sh.y[c], v[k * 4 + 2] = sh.u[c], v[k * 4 + 3] = sh.v[c];
        }
        dev->drawQuad(cr3GlassTexture, v, proj.data(), rgba);
    }
}

// an announcement up (BtlAnnounceUI: its seconds, below 0 until the next), the last one going
void Viewer::setCr3Announce(const std::string& text, float seconds) {
    cr3Announce.text = text, cr3Announce.at = cr3BattleClock, cr3Announce.seconds = seconds < 0 ? 1e9f : seconds;
}

// (a name's "{source}" - BtlDamageChunk::OpenAnnounce: the actor; "{assistant}" is OpenAssist's -
// cr3AssistantName)
std::string Viewer::cr3FighterName(const cr3::Cr3Fighter& f) {
    return f.player ? cr3NameOf("char_name.mbe", "CharName", f.id) : cr3NameOf("mon_name.mbe", "MonsterName", f.id);
}

// the status effects an action gave (BtlDamageTargetList::openStatusEffectAnnounce): those with
// lines (StatusEffect +0x74 / +0x78 - a monster's, a character's -, -1 none); one on one target its
// line, "{target}" the target; several on one the command's (+0xe8), else 99 (a monster) / 0x6a;
// one alike on several targets its +0x76 / +0x7a; else as several
void Viewer::announceCr3Effects(const cr3::Cr3Action& a) {
    showCr3ActionState(a); // (BtlStatusEffectList::Add on the direction side: the icons as the effects land)
    auto lines = [&](int id, int& single, int& multi, bool player) {
        cr3::MbeFile::Row r;
        if (!cr3BattleTables.skill.find("StatusEffect", id, "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiii", r)) return false;
        const int s74 = int16_t(uint32_t(r.i(29)) & 0xffff), s76 = int16_t(uint32_t(r.i(29)) >> 16);
        const int s78 = int16_t(uint32_t(r.i(30)) & 0xffff), s7a = int16_t(uint32_t(r.i(30)) >> 16);
        if (s74 == -1 || s78 == -1) return false;
        single = player ? s78 : s74, multi = player ? s7a : s76;
        return true;
    };
    std::vector<std::pair<int, std::vector<std::pair<int, int>>>> got; // (target, its listed effects' (single, multi))
    for (const cr3::Cr3Hit& th : a.targets) {
        std::vector<std::pair<int, int>> listed;
        for (int e : th.effectsGiven) {
            int single = -1, multi = -1;
            if (lines(e, single, multi, cr3Rules.fighters[size_t(th.target)].player)) listed.push_back({single, multi});
        }
        if (!listed.empty()) got.push_back({th.target, listed});
    }
    if (got.empty()) return;
    const cr3::Cr3Fighter& first = cr3Rules.fighters[size_t(got[0].first)];
    cr3::Cr3Command c;
    cr3Rules.command(a.command, c);
    const int several = c.effectsAnnounce > 0 ? c.effectsAnnounce : first.player ? 0x6a : 99;
    int id = several;
    if (got.size() == 1 && got[0].second.size() == 1) id = got[0].second[0].first;
    else if (got.size() > 1 && std::all_of(got.begin(), got.end(), [&](const auto& g) { return g.second.size() == 1 && g.second[0] == got[0].second[0]; }))
        id = got[0].second[0].second;
    std::string text = announceText("AnnounceBattle", id);
    for (const char* token : {"{target}", "{source}"}) // (AnnounceStatusEffect: the target's name)
        if (const size_t at = text.find(token); at != std::string::npos) text.replace(at, std::strlen(token), cr3FighterName(first));
    if (!text.empty()) setCr3Announce(text, 1.0f);
    SDL_Log("battle %.2f s: status effects on %zu - line %d", cr3BattleClock, got.size(), id);
}

// the panels' state before a turn's calculation - the rules run it whole up front (BtlCommandManager::Calculate)
void Viewer::freezeCr3Shown() {
    cr3EffectsShown.assign(cr3Rules.fighters.size(), {}), cr3ElementShown.assign(cr3Rules.fighters.size(), 0);
    for (size_t k = 0; k < cr3Rules.fighters.size(); ++k) {
        for (const auto& e : cr3Rules.fighters[k].effects) cr3EffectsShown[k].push_back(e.first);
        cr3ElementShown[k] = cr3Rules.fighters[k].element;
    }
}

// an action's effects and elements shown (BtlStatus::UpdateInterface 0x009b3a3c's Update{.. +0xb8 the element},
// BtlStatusUI::AddStatusEffectIcon / RemoveStatusEffectIcon 0x009a36d8 / 0x009a370c)
void Viewer::showCr3ActionState(const cr3::Cr3Action& a) {
    if (a.effectsAfter.empty() || cr3EffectsShown.empty()) return; // (a script's inserted direction: none; choosing: the rules' own)
    cr3EffectsShown = a.effectsAfter, cr3ElementShown = a.elementsAfter;
}

// btStPanel::SetAbnormalityStatusIcon 0x00a0f0b4 / Step 0x00a0e860: the icon a party panel shows now, -1 none
int Viewer::cr3StatusIcon(size_t f) {
    if (f >= cr3Rules.fighters.size()) return -1;
    if (cr3StIcons.size() != cr3Rules.fighters.size()) cr3StIcons.assign(cr3Rules.fighters.size(), {});
    Cr3StIcon& st = cr3StIcons[f];
    uint64_t mask = 0;
    auto add = [&](int id) { // (MbStatusEffectInfo +0x08; the loop's 0 .. 0x38)
        const int icon = cr3Rules.effectIcon(id);
        if (icon >= 0 && icon < 57) mask |= uint64_t(1) << icon;
    };
    if (f < cr3EffectsShown.size())
        for (int id : cr3EffectsShown[f]) add(id);
    else
        for (const auto& e : cr3Rules.fighters[f].effects) add(e.first);
    if (mask & (uint64_t(1) << 9)) mask = uint64_t(1) << 9; // (icon 9 - ids 1, 51: no action - the others cleared while it's on)
    if (st.last < 0 || cr3BattleClock < st.last) st.last = cr3BattleClock;
    st.timer += cr3BattleClock - st.last, st.last = cr3BattleClock; // (+0x194: never reset - it runs on)
    if (mask != st.mask) { // (SetAbnormalityStatusIcon: the highest set)
        st.mask = mask, st.shown = -1;
        for (int i = 56; i >= 0 && st.shown < 0; --i)
            if (mask & (uint64_t(1) << i)) st.shown = i;
        if (mask) SDL_Log("battle: fighter %zu's status icon %d (mask 0x%llx)", f, st.shown, static_cast<unsigned long long>(mask));
    }
    if (!mask) return st.timer = 0, -1;
    while (st.timer >= 1.0f) { // (Step: every 1.0 s the next set bit, (i + 1) % 57)
        st.timer -= 1.0f;
        for (int k = 1; k <= 57; ++k) {
            const int i = (st.shown + k) % 57;
            if (mask & (uint64_t(1) << i)) {
                st.shown = i;
                break;
            }
        }
    }
    return st.shown;
}

std::vector<int> Viewer::cr3TargetIcons(size_t f) const {
    std::vector<int> ids;
    if (f < cr3EffectsShown.size()) ids = cr3EffectsShown[f];
    else if (f < cr3Rules.fighters.size())
        for (const auto& e : cr3Rules.fighters[f].effects) ids.push_back(e.first);
    std::vector<std::pair<int, int>> slots; // (StatusEffectList::Add 0x9b75c8: its fixed array, by StatusEffect +0x04 - the group)
    for (int id : ids)
        if (cr3::MbeFile::Row r; cr3BattleTables.skill.find("StatusEffect", id, "ii", r)) slots.emplace_back(r.i(1), id);
    std::stable_sort(slots.begin(), slots.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<int> icons;
    for (const auto& [group, id] : slots) { // (GetIcon 0x9c7e3c - +0x08 -, -1 passed over, eight at most; AddTarget passes seven)
        const int icon = cr3Rules.effectIcon(id);
        if (icon == 9) return {9}; // (KO or no action: alone)
        if (icon >= 0 && icons.size() < 8) icons.push_back(icon);
    }
    if (icons.size() > 7) icons.resize(7);
    return icons;
}

int Viewer::cr3TargetIcon(size_t f) {
    const std::vector<int> icons = cr3TargetIcons(f);
    Cr3TargetIconCycle& c = cr3TargetIconCycles[int(f)];
    if (icons.empty()) return -1;
    if (c.opened != cr3MenuStart) { // (SetParameter: +0x1d8 0)
        c = Cr3TargetIconCycle(), c.opened = cr3MenuStart, c.last = cr3BattleClock;
        std::string list;
        for (int icon : icons) list += " " + std::to_string(icon);
        SDL_Log("battle: fighter %zu's target icons%s", f, list.c_str());
    }
    c.timer -= std::max(cr3BattleClock - c.last, 0.0f), c.last = cr3BattleClock;
    if (c.slot >= int(icons.size())) c.slot = 0;
    if (c.timer <= 0) { // (Step: 1.0 s, the shown one hidden, the next on - mod 7 - shown)
        c.timer = 1.0f;
        c.slot = (c.slot + 1) % int(icons.size());
        SDL_Log("battle: fighter %zu's target icon %d (%d of %zu)", f, icons[size_t(c.slot)], c.slot, icons.size());
    }
    return icons[size_t(c.slot)];
}

// BtlTotalDamageUI::AddTotalDamage 0x009a48bc (CR3-U's sign: damage above 0): the unit's slot - addComboCharaDataView
// 0x009a497c -, raw += the damage where it is damage, actual += the HP lost, its hits + 1
void Viewer::addCr3Total(int fighter, int damage, int hpBefore, int hpAfter) {
    Cr3TotalSlot* slot = nullptr;
    for (Cr3TotalSlot& sl : cr3Totals)
        if (sl.fighter == fighter) slot = &sl;
    if (!slot) {
        for (Cr3TotalSlot& sl : cr3Totals)
            if (!slot && sl.fighter < 0) slot = &sl;
        if (!slot) slot = &cr3Totals[size_t(cr3TotalNext)], cr3TotalNext = (cr3TotalNext + 1) % 3; // (full: round-robin)
        *slot = Cr3TotalSlot();
        slot->fighter = fighter, slot->startHp = hpBefore;
    }
    if (damage > 0) slot->raw += damage;
    slot->actual += hpBefore - hpAfter;
    ++slot->hits;
    slot->numbers = damage > 0 || slot->hits > 1;
    if (cr3ComboSince < 0) cr3ComboSince = cr3BattleClock;
    SDL_Log("battle %.2f s: total damage - fighter %d, slot %d, %d hits, %d", cr3BattleClock, fighter, int(slot - cr3Totals.data()), slot->hits, slot->raw);
}

// BtlAnalyzeDirection's end (VUpdate 0x0098b5b0 -> BtlMonsterStatus::VAnalyze 0x009c6804): false while its lines
// are up; a character's VAnalyze (0x009b2e24) is empty
bool Viewer::stepCr3Analyze(const cr3::Cr3Action& a) {
    if (cr3AnalyzeDone || a.command < 0) return true;
    cr3::Cr3Command c;
    if (!cr3Rules.command(a.command, c) || c.direction != 0x16) return true;
    if (cr3AnalyzeLines.empty()) {
        const int ti = a.targets.empty() ? a.target : a.targets.front().target;
        if (ti < 0 || size_t(ti) >= cr3Rules.fighters.size() || cr3Rules.fighters[size_t(ti)].player) return cr3AnalyzeDone = true;
        const cr3::Cr3Fighter& m = cr3Rules.fighters[size_t(ti)];
        std::string hp = announceText("AnnounceBattle", 32); // (OpenHpAndMaxHp 0x0099e594)
        const int shownHp = size_t(ti) < cr3HpShown.size() ? cr3HpShown[size_t(ti)] : m.hp;
        if (const size_t at = hp.find("{hp}"); at != std::string::npos) hp.replace(at, 4, std::to_string(shownHp));
        if (const size_t at = hp.find("{max_hp}"); at != std::string::npos) hp.replace(at, 8, std::to_string(m.maxHp));
        cr3AnalyzeLines = {cr3FighterName(m), hp, cr3NameOf("mon_help.mbe", "MonsterHelp", m.id)}; // (vGetName, the HP, vGetHelp)
        cr3AnalyzeAt = 0, cr3AnalyzeSince = cr3BattleClock;
        cr3Analyzed.insert(m.id); // (BtlMonsterAnalyzation::Add: its species)
        setCr3Announce(cr3AnalyzeLines[0], -1);
        SDL_Log("battle %.2f s: analyze - monster %d: \"%s\" / \"%s\" / \"%s\"", cr3BattleClock, m.id, cr3AnalyzeLines[0].c_str(),
                cr3AnalyzeLines[1].c_str(), cr3AnalyzeLines[2].c_str());
        return false;
    }
    // (BattleAnnouncementMenu::Update: the next queued line once its timer reaches 10 s; the last until a tap -
    // --input runs, nobody to tap, it too after 10 s)
    const bool last = cr3AnalyzeAt + 1 >= cr3AnalyzeLines.size();
    if (cr3BattleClock - cr3AnalyzeSince >= 10.0f && (!last || autoBattle || manualCommands)) tapCr3Analyze();
    return cr3AnalyzeLines.empty();
}

// a tap while the Analyze's lines are up (TouchGestureTap 0x00a0ada0, the skip flag on): the next line, after the
// last the menu closed and the direction over
bool Viewer::tapCr3Analyze() {
    if (cr3AnalyzeLines.empty() || cr3BattleStage != Cr3BattleStage::Action) return false;
    if (++cr3AnalyzeAt < cr3AnalyzeLines.size()) {
        cr3AnalyzeSince = cr3BattleClock;
        setCr3Announce(cr3AnalyzeLines[cr3AnalyzeAt], -1);
        SDL_Log("battle %.2f s: analyze line %zu", cr3BattleClock, cr3AnalyzeAt);
    } else {
        cr3AnalyzeLines.clear(), cr3AnalyzeDone = true;
        cr3Announce = Cr3Announce(); // (BtlAnnounceUI::Close)
        SDL_Log("battle %.2f s: analyze closed", cr3BattleClock);
    }
    return true;
}

// BtlMonsterStatus::vDrop (0x9c75dc): each monster's drop rolled once, as its wave is made - two slots {kind, id, rate}
// (Monster +0x70 / +0x7c), one roll of 256 against slot 0's rate plus the drop bonus (BtlStatusList::GetDropRateCorrection
// 0x9b01e0: the largest skill-85 constant of any unit, then), then slot 1's; a status effect of kind 0x83 on any unit
// takes slot 1 outright. Kind 8 (a VS season's) below. Announced only when the monster falls
void Viewer::rollCr3Drops() {
    int dropBoost = 0;
    bool rare = false;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters) {
        if (f.dead()) continue;
        for (int c : cr3Rules.skillConstants(f, 85)) dropBoost = std::max(dropBoost, c);
        rare |= cr3Rules.hasKind(f, 0x83);
    }
    for (size_t fi = 0; fi < cr3Rules.fighters.size(); ++fi) {
        const cr3::Cr3Fighter& f = cr3Rules.fighters[fi];
        if (f.player || cr3Drops.count(int(fi))) continue;
        cr3Drops[int(fi)] = 0;
        cr3::MbeFile::Row r;
        if (!cr3BattleTables.monster.find("Monster", f.id, "iipiiiiiiiiiiiiiiiiiiiiiiiiiiiiii", r)) continue;
        const int kind0 = r.i(27), kind1 = r.i(30);
        if (kind0 == 8 || kind1 == 8) { // (a season slot - vDrop: chosen with no roll iff the tournament may drop the season's
            // token - its +0xa0 -, else nothing, slot 0 never; announced under the row's +0x9c item, taken by no one - the
            // result's season line gives it, BtlResult::DisplayDrop finding no item for its -1)
            const auto& list = game.cr3Db ? game.cr3Db->vsTournaments() : std::vector<cr3::Cr3OfflineDb::VsTournament>();
            if (vs.screen == 5 && vs.chosen >= 0 && size_t(vs.chosen) < list.size() && game.cr3Vs.seasonDrop && list[size_t(vs.chosen)].seasonItem > 0)
                cr3Drops[int(fi)] = -list[size_t(vs.chosen)].seasonItem;
            continue;
        }
        int pick = -1;
        if (rare && kind1 > 0) pick = 1;
        else {
            const int c0 = kind0 > 0 && r.i(29) > 0 ? std::max(r.i(29) + dropBoost, 0) : 0;
            const int c1 = kind1 > 0 && r.i(32) > 0 ? r.i(32) : 0;
            const int roll = int(SDL_rand(256));
            pick = roll < c0 ? 0 : roll < c0 + c1 ? 1 : -1;
        }
        if (pick < 0) continue;
        const int kind = pick ? kind1 : kind0, id = pick ? r.i(31) : r.i(28);
        if (kind < 1 || kind > 7 || id <= 0) continue;
        cr3Drops[int(fi)] = (kind - 1) * 100000 + id;
    }
}

void Viewer::rollCr3Drop(int fi) {
    const auto it = cr3Drops.find(fi);
    if (it == cr3Drops.end() || it->second == 0 || cr3DropsAnnounced.count(fi)) return; // (below 0: a season token's - announced, not taken)
    cr3DropsAnnounced.insert(fi);
    std::string text = announceText("AnnounceBattle", 0x1d);
    if (const size_t at = text.find("{item.name}"); at != std::string::npos) text.replace(at, 11, cr3ItemText(std::abs(it->second), false));
    setCr3Announce(text, 1.0f);
    audio.playSe("com_013", false);
    SDL_Log("battle %.2f s: fighter %d drops item %d", cr3BattleClock, fi, it->second);
}

float Viewer::cr3GameOverLength() {
    UiFig& u = uiFig("btgameovermenu");
    return u.fig.animLoaded ? u.fig.anim.duration() : 3.0f;
}

void Viewer::drawCr3GameOver(int w, int h) {
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    const float t = cr3BattleClock - cr3GameOverAt;
    UiFig& pos = cr3UiPut("btgameoverpositon", "", 0, 0, 0, view);
    float x, y;
    if (cr3UiCall(pos, "call_btGameOverMenu_fit0", x, y, w, h)) cr3UiPut("btgameovermenu", "", std::min(t, cr3GameOverLength()), x, y, view);
    const float fade = std::clamp((t - cr3GameOverLength()) / 0.5f, 0.0f, 1.0f); // (FadeOut 0.5)
    if (fade > 0) {
        static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        const Mat4 id = cr3::mat4Identity();
        const float black[4] = {0, 0, 0, fade};
        fullFrameQuad(white, kScreen, black);
    }
}

int Viewer::cr3Jems() const {
    const auto it = game.cr3Items.find(kJemItem);
    return it == game.cr3Items.end() ? 0 : std::max(it->second, 0);
}

void Viewer::openCr3Continue() {
    cr3Ctn = Cr3Continue(), cr3Ctn.stage = 1, cr3Ctn.since = cr3BattleClock;
    audio.playBgm(0x2f, 0);
    SDL_Log("battle %d: continue? (%d jems)", cr3Battle.coupling, cr3Jems());
}

void Viewer::takeAutoBattleToken() {
    while (!inputScript.empty() && (inputScript.front() == "auto" || inputScript.front() == "auto:gene" || inputScript.front() == "manual")) {
        autoBattle = inputScript.front() != "manual", autoGene = inputScript.front() == "auto:gene";
        inputScript.pop_front();
        SDL_Log("battles: %s", autoBattle ? (autoGene ? "auto, genes first" : "auto") : "manual");
    }
    if (!inputScript.empty() && inputScript.front() == "guard:party") // (a test aid: the party takes no damage - every battle after)
        inputScript.pop_front(), testPartyUnhurt = true, SDL_Log("battles: the party takes no damage (test)");
    // (--input foe:<turn>:<fighter>:<command>: tests - from that turn's choosing on, the monster given the command as a
    // script's BattleCommand.Add would - one action -, its target getCommandTarget's)
    while (!inputScript.empty() && inputScript.front().compare(0, 4, "foe:") == 0 && cr3BattleMode) {
        int turn = 0, fighter = -1, command = -1;
        if (std::sscanf(inputScript.front().c_str() + 4, "%d:%d:%d", &turn, &fighter, &command) != 3) {
            inputScript.pop_front();
            continue;
        }
        if (cr3Turn < turn) break;
        inputScript.pop_front();
        if (fighter >= 0 && size_t(fighter) < cr3Rules.fighters.size() && !cr3Rules.fighters[size_t(fighter)].player)
            cr3Rules.scripted[fighter].push_back({1, command}), SDL_Log("battle: fighter %d given command %d on turn %d (test)", fighter, command, cr3Turn);
    }
    cr3Rules.partyUnhurt = testPartyUnhurt;
}

bool Viewer::isCr3BattleToken(const std::string& t) {
    return t == "battle" || t == "escape" || t == "options" || t == "attack" || t == "guard" ||
           t.compare(0, 4, "gene") == 0 || t.compare(0, 4, "item") == 0 || (t.size() > 1 && t[0] == 't' && std::isdigit(static_cast<unsigned char>(t[1])));
}

void Viewer::stepCr3Continue() {
    // (--input ctn:giveup / jem / yes / no / ok: tests - the battle's leftover choices dropped; ctn:hold none)
    while (!inputScript.empty() && (inputScript.front() == "attack" || inputScript.front() == "guard" || inputScript.front() == "battle" ||
                                    (inputScript.front().size() > 1 && inputScript.front()[0] == 't' && std::isdigit(static_cast<unsigned char>(inputScript.front()[1])))))
        inputScript.pop_front();
    if (!inputScript.empty() && inputScript.front() == "ctn:hold") return; // (tests: left to the keys / a controller)
    if (!inputScript.empty() && inputScript.front().compare(0, 4, "ctn:") == 0 && cr3BattleClock - cr3Ctn.since > 0.6f) {
        static const std::map<std::string, int> kCtn = {{"giveup", kCtnGiveUp}, {"jem", kCtnJem}, {"yes", kCtnYes}, {"no", kCtnNo}, {"ok", kCtnOk}};
        const auto it = kCtn.find(inputScript.front().substr(4));
        inputScript.pop_front();
        if (it != kCtn.end()) chooseCr3Continue(it->second);
    } else if (autoBattle && (inputScript.empty() || inputScript.front().compare(0, 4, "ctn:") != 0) && cr3BattleClock - cr3Ctn.since > 0.6f) {
        // (--input auto: a jem spent where one is held, else given up)
        const int stage = cr3Ctn.stage;
        chooseCr3Continue(stage == 1 ? (cr3Jems() > 0 ? kCtnJem : kCtnGiveUp) : stage == 4 ? kCtnOk : kCtnYes);
    }
}

// The Vita's buttons over it (ContinueMenu::Update 0x8113c03a: FUN_8128d1ea's cursor over its two
// contBtns - ↑ / ↓, round, com_002 -, the one under it TapON - FUN_8113b80c: its text's tapped
// look -, ○ pressing it; the question YNChoiceButton's - FUN_812818ca: ↑ / ↓ between Yes and No,
// com_002, ○ the one under it, × No -; the told's ○ its OK). With the Vita copy as the game
bool Viewer::cr3ContinueKey(SDL_Keycode k) {
    if (!vitaButtons() || cr3Ctn.stage == 0 || cr3BattleClock - cr3Ctn.since < 0.2f) return false;
    const bool move = k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT, enter = k == SDLK_RETURN || k == SDLK_KP_ENTER;
    if (cr3Ctn.stage == 1) {
        if (move && (k == SDLK_UP || k == SDLK_DOWN)) cr3Ctn.focus ^= 1, audio.playSe("com_002", false), SDL_Log("continue: the cursor on %d", cr3Ctn.focus);
        else if (enter) chooseCr3Continue(cr3Ctn.focus == 0 ? kCtnGiveUp : kCtnJem);
    } else if (cr3Ctn.stage == 2 || cr3Ctn.stage == 3) {
        if (move) cr3Ctn.askFocus ^= 1, audio.playSe("com_002", false);
        else if (enter) chooseCr3Continue(cr3Ctn.askFocus == 0 ? kCtnYes : kCtnNo);
        else if (k == SDLK_BACKSPACE || k == SDLK_ESCAPE) chooseCr3Continue(kCtnNo);
    } else if (cr3Ctn.stage == 4 && enter) chooseCr3Continue(kCtnOk);
    return true;
}

void Viewer::chooseCr3Continue(int id) {
    const int stage = cr3Ctn.stage;
    cr3Ctn.askFocus = 0;
    if (stage == 1 && id == kCtnGiveUp) {
        audio.playSe("com_003", false);
        cr3Ctn.stage = 2, cr3Ctn.since = cr3BattleClock;
    } else if (stage == 1 && id == kCtnJem) {
        audio.playSe("com_003", false);
        cr3Ctn.stage = cr3Jems() < 1 ? 4 : 3, cr3Ctn.since = cr3BattleClock;
    } else if ((stage == 2 || stage == 3) && id == kCtnNo) {
        audio.playSe("com_004", false);
        cr3Ctn.stage = 1, cr3Ctn.since = cr3BattleClock;
    } else if (stage == 4 && id == kCtnOk) {
        cr3Ctn.stage = 1, cr3Ctn.since = cr3BattleClock;
    } else if (stage == 2 && id == kCtnYes) { // (given up: the defeat - BtlGameOverUI, drawCr3GameOver; on as lost)
        audio.playSe("com_003", false);
        cr3Ctn.stage = 0, cr3Ctn.decided = true;
        SDL_Log("battle %d: given up", cr3Battle.coupling);
        cr3StageStart = cr3BattleClock - 2.5f;
    } else if (stage == 3 && id == kCtnYes) {
        audio.playSe("com_003", false);
        game.cr3Items[kJemItem] = cr3Jems() - 1;
        cr3Ctn.stage = 0; // (not decided: a later defeat asks again)
        continueCr3Battle();
    }
    if (cr3Ctn.stage != stage) SDL_Log("continue: %d to %d", stage, cr3Ctn.stage);
}

void Viewer::continueCr3Battle() {
    for (size_t k = 0; k < cr3Rules.fighters.size(); ++k) {
        cr3::Cr3Fighter& f = cr3Rules.fighters[k];
        if (!f.player) continue;
        if (f.dead()) f.effects.clear();
        f.hp = f.maxHp, f.mp = f.maxMp; // (AnnounceBattle 0x2b: HP and MP whole)
        cr3HpShown[k] = f.hp;
        if (k < cr3MpShown.size()) cr3MpShown[k] = f.mp;
        restoreCr3Unit(size_t(f.unit));
    }
    cr3Rules.gauge = 0, cr3GaugeShown = 0; // (BtlBreakGaugeUI::Even)
    cr3BattleLost = false;
    stopCr3Auto("continued"); // (VUpdate(BtlContinueUI) 0x9d2428: StopAuto)
    if (game.next.battleMusic >= 0) audio.playBgm(game.next.battleMusic, 0);
    SDL_Log("battle %d: continued (%d jems left)", cr3Battle.coupling, cr3Jems());
    beginCr3Turn();
}

void Viewer::drawCr3Continue(int w, int h) {
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    cr3CtnTaps.clear();
    const float t = cr3BattleClock - cr3Ctn.since, a = std::min(t / 0.1333f, 1.0f); // (SetFade 0.1333)
    UiFig& pos = cr3UiPut("btctnpositon", "", 0, 0, 0, view);
    float x, y, cx, cy;
    if (cr3UiCall(pos, "call_btHelpMenu_fit8", x, y, w, h)) {
        UiFig& help = cr3UiPut("bthelpmenu", "", 0, x, y, view, a);
        if (cr3UiCall(help, "typeM_L01", cx, cy)) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(menuAnnounce(0xbe), 26, (cx + 0.075f) / 10, (cy - 0.15f) / 10, w, h, true, a);
        }
    }
    if (cr3UiCall(pos, "call_btCtnJemCoinMenu_fit4", x, y, w, h)) {
        UiFig& jm = cr3UiPut("btctnjemcoinmenu", "", 0, x, y, view, a);
        if (cr3UiCall(jm, "call_btCtnNumJ_num00", cx, cy)) {
            UiFig& nj = cr3UiPut("btctnnumj_num00", "", 0, cx, cy, view, a);
            const int n = std::min(cr3Jems(), 99);
            for (int d = 0; d < 2; ++d) {
                float dx, dy;
                if ((d == 0 && n < 10) || !cr3UiCall(nj, "call_btCtnNum_fr00_00" + std::to_string(d + 1), dx, dy)) continue;
                cr3UiPut("btctnnum_fr00", "", float(d == 0 ? n / 10 : n % 10) / 30, dx, dy, view, a);
            }
        }
    }
    if (cr3Ctn.stage == 1 && cr3UiCall(pos, "call_btCtnBtnMenu_fit6", x, y, w, h)) { // (closed while asking - InterfaceCloseAnimeSet)
        UiFig& menu = cr3UiPut("btctnbtnmenu", "btctnbtnmenu_in", t, x, y, view);
        for (int b = 0; b < 2; ++b) {
            float px, py;
            if (!cr3UiCall(menu, "call_btCtnBtnPanel_00" + std::to_string(b + 1), px, py)) continue;
            const bool off = b == 1 && cr3Jems() < 1; // (SelectableSet(false): none held)
            cr3UiTint[0] = cr3UiTint[1] = off ? 0.549f : 1.0f, cr3UiTint[2] = off ? 0.655f : 1.0f; // (cr3UiPut's tint)
            UiFig& panel = cr3UiPut("btctnbtnpanel", "", 0, px, py, view);
            const int id = b == 0 ? 12 : 13;
            if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(b == 0 ? 8 : 1) / 30, cx, cy, view);
            if (cr3UiCall(panel, "call_comBtnIcon_fr00", cx, cy)) cr3UiPut("combtnicon_fr00", "", float(id) / 30, cx, cy, view);
            if (cr3UiCall(panel, "call_comBtnText_fr00", cx, cy))
                cr3UiPut(b == cr3Ctn.focus ? "combtntexttap_fr00" : "combtntext_fr00", "", float(id) / 30, cx, cy, view);
            if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view);
            if (b == 1 && cr3UiCall(panel, "call_btCtnbuyNum_fr00", cx, cy)) cr3UiPut("btctnbuynum_fr00", "", 0, cx, cy, view);
            float ax, ay, bx, by;
            if (cr3Ctn.stage == 1 && cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by))
                cr3CtnTaps.push_back({ax, ay, bx, by, b == 0 ? kCtnGiveUp : kCtnJem});
        }
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1.0f;
    }
    const float since = cr3BattleClock - cr3Ctn.since;
    if (cr3Ctn.stage == 2 || cr3Ctn.stage == 3)
        drawCr3Asked(announceText("AnnounceBattle", cr3Ctn.stage == 2 ? 0x2a : 0x2b), since, vitaButtons() ? cr3Ctn.askFocus : 0, true, kCtnYes, kCtnNo, w, h, view, &cr3CtnTaps);
    else if (cr3Ctn.stage == 4) {
        std::string text = menuAnnounce(0x132);
        if (const size_t at = text.find("{jem}"); at != std::string::npos) text.replace(at, 5, std::to_string(cr3Jems())); // (the count held)
        drawCr3Told(text, since, kCtnOk, w, h, view, &cr3CtnTaps);
    }
}

void Viewer::rollCr3StartMode() {
    cr3StartMode = 0, cr3Announce = Cr3Announce();
    const int first = game.next.battleFirstStrike[0], surprise = game.next.battleFirstStrike[1];
    int firstBy = 0, surpriseBy = 0; // (the best unit's kind 0x5e - its highest -, the least's 0x5f sum)
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters) {
        if (!f.player) continue;
        for (int c : cr3Rules.skillConstants(f, 0x5e)) firstBy = std::max(firstBy, c);
        int sum = 0;
        for (int c : cr3Rules.skillConstants(f, 0x5f)) sum += c;
        surpriseBy = std::min(surpriseBy, sum);
    }
    if (first != 0 || surprise != 0) SDL_Log("battle: first strike %d%% (%+d), surprise %d%% (%+d)", first + firstBy, firstBy, surprise + surpriseBy, surpriseBy);
    if (first != 0 && int(SDL_rand(100)) < first + firstBy) cr3StartMode = 1;
    else if (surprise != 0 && int(SDL_rand(100)) < surprise + surpriseBy) cr3StartMode = 2;
    if (const char* m = SDL_getenv("CR3_START_MODE")) cr3StartMode = std::atoi(m);
    if (cr3StartMode != 1 && cr3StartMode != 2) return void(cr3StartMode = 0);
    cr3Rules.addBreak(cr3StartMode == 1 ? 500 : -500);
    cr3GaugeShown = cr3Rules.gauge;
    cr3Rules.monstersWait = cr3StartMode == 1;
    int players = 0, leader = -1;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters)
        if (f.player) leader = leader < 0 ? f.id : leader, ++players;
    const int id = cr3StartMode == 2 ? 3 : players == 1 ? 2 : 1;
    std::string text = announceText("AnnounceBattle", id); // (OpenName: its "{source}" the name)
    if (const size_t at = text.find("{source}"); at != std::string::npos) text.replace(at, 8, cr3NameOf("char_name.mbe", "CharName", leader));
    setCr3Announce(text, 2.5f);
    SDL_Log("battle: %s", cr3StartMode == 1 ? "the party strikes first" : "a surprise attack");
}

// the announcement (BattleAnnouncementMenu: a OneHelpMenu - btHelpMenu at btHelpPosition's call,
// white text; ReOpen fading in over a sixth of a second, Close out over as long)
// (BtlResultMenu state 0xf, SetDropItemText 0xb1ffe4: each drop a line of its own after the tally - MnlCmp 0xfb, the
// item got, or 0xfc, the bag full -, a tap for the next; false: none left)
bool Viewer::showCr3DropLine() {
    if (cr3ResultDropLines.empty()) return false;
    const int line = cr3ResultDropLines.front();
    cr3ResultDropLines.erase(cr3ResultDropLines.begin());
    std::string text = menuAnnounce(line > 0 ? 0xfb : 0xfc);
    if (const size_t at = text.find("%s"); at != std::string::npos) text.replace(at, 2, cr3ItemText(std::abs(line), false));
    cr3ResultLine = text, cr3ResultLineAt = cr3BattleClock;
    SDL_Log("battle: result - %s", text.c_str());
    return true;
}

void Viewer::drawCr3Announce(const Mat4& view, int w, int h) {
    const float t = cr3BattleClock - cr3Announce.at;
    const float a = std::clamp(std::min(t, cr3Announce.seconds - t) * 6.0f, 0.0f, 1.0f);
    UiFig& hp = cr3UiPut("bthelpposition", "", 0, 0, 0, view);
    float hx, hy, tx, ty;
    if (!cr3UiCall(hp, "call_btHelpMenu", hx, hy, w, h)) return;
    UiFig& help = cr3UiPut("bthelpmenu", "", 0, hx, hy, view, a);
    if (cr3UiCall(help, "typeM_L01", tx, ty)) {
        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
        drawUiText(cr3Announce.text, 26, (tx + 0.075f) / 10, (ty - 0.15f) / 10, w, h, true, a);
    }
}

// the battle's party: SetBattleParty's three where a script set them (the tutorials' own
// fighters - battle 175's character 9 alone), else the party's
std::vector<int> Viewer::cr3BattlePartyIds() const {
    if (game.next.cr3BattleParty[0] == -1) return std::vector<int>(std::begin(game.cr3Party), std::end(game.cr3Party));
    std::vector<int> party(3, 0);
    for (int k = 0; k < 3; ++k) party[size_t(k)] = std::max(game.next.cr3BattleParty[k], 0);
    return party;
}

// the units' figures from `from` on (the party's, the monsters')
void Viewer::loadCr3BattleUnits(size_t from, size_t to) {
    std::string err;
    int monsters = 0;
    for (size_t k = 0; k < cr3Battle.units.size(); ++k) {
        const cr3::Cr3BattleUnit& bu = cr3Battle.units[k];
        Cr3BattleUnitView& u = cr3BattleUnits[k];
        if (!bu.player) u.phase = float(monsters++) + 0.25f; // (Standby: its slot's 0 / 1 / 2, + up to 0.5 at random)
        if (k < from || k >= to) continue;
        figureVariant = bu.variant; // (CrxModel::ChangePartTextureVariation: the monster's)
        for (int part = 0; part < 2; ++part) {
            const std::string& fig = part ? bu.head : bu.figure;
            ViewFigure& f = part ? u.head : u.body;
            if (fig.empty()) continue;
            if (!loadFigure(fig + ".geom", f, &err)) {
                SDL_Log("battle unit %s: %s", fig.c_str(), err.c_str());
                continue;
            }
            const auto it = std::find(f.anims.begin(), f.anims.end(), motionBase(fig) + "_bb01.anim"); // (a costume body: b01's motions)
            if (it != f.anims.end()) loadAnim(f, size_t(it - f.anims.begin()));
        }
        figureVariant = bu.weaponVariant;
        if (!bu.weapon.empty() && !loadFigure(bu.weapon + ".geom", u.weapon, &err)) SDL_Log("battle weapon %s: %s", bu.weapon.c_str(), err.c_str());
        figureVariant.clear();
    }
}

void Viewer::reinforceCr3Battle(int coupling) {
    acquireCr3Remuneration();
    std::vector<cr3::Cr3Fighter> party;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters)
        if (f.player) party.push_back(f);
    const int gauge = cr3Rules.gauge;
    const std::vector<int> ids = cr3BattlePartyIds();
    std::string err;
    cr3::Cr3Battle next;
    if (!next.setup(cr3BattleTables, coupling, cr3BattleFieldId, ids, uint32_t(SDL_GetTicks()) | 1u, &err, &game.cr3Armour, &game.cr3Weapon)) {
        SDL_Log("battle %d: the next wave %d: %s", cr3Battle.coupling, coupling, err.c_str());
        return;
    }
    // (the party's units stay - the same, first -; the monsters' figures anew)
    size_t players = 0;
    while (players < next.units.size() && next.units[players].player) ++players;
    for (size_t k = players; k < cr3BattleUnits.size(); ++k) unload(cr3BattleUnits[k].body), unload(cr3BattleUnits[k].head), unload(cr3BattleUnits[k].weapon);
    cr3BattleUnits.resize(std::min(players, cr3BattleUnits.size()));
    cr3BattleUnits.resize(next.units.size());
    next.field = cr3Battle.field; // (the field stays)
    cr3Battle = next;
    loadCr3BattleUnits(players);
    cr3PartyRules();
    cr3Rules.partyMpLeft.clear();
    const char* fixedSeed = SDL_getenv("CR3_BATTLE_SEED");
    const uint32_t seed = fixedSeed ? uint32_t(std::strtoul(fixedSeed, nullptr, 10)) + uint32_t(coupling) : uint32_t(SDL_GetTicks()) | 1u;
    if (!cr3Rules.setup(cr3BattleTables, cr3Battle, game.next.battleLevel, seed, &err)) SDL_Log("battle rules: %s", err.c_str());
    cr3Rules.gold = game.cr3Money; // (the party's money: a Steal Gold's to take from)
    if (gaugeArg != 0) cr3Rules.gauge = std::clamp(gaugeArg * 250, -1000, 1000), cr3Rules.gaugeStarted = true, SDL_Log("battle: the break gauge at %d (test)", cr3Rules.gauge); // (--gauge -4 .. 4: tests)
    for (size_t k = 0, p = 0; k < cr3Rules.fighters.size() && p < party.size(); ++k)
        if (cr3Rules.fighters[k].player) {
            const int unit = cr3Rules.fighters[k].unit;
            cr3Rules.fighters[k] = party[p++], cr3Rules.fighters[k].unit = unit;
        }
    cr3Rules.gauge = gauge;
    cr3HpShown.clear(), cr3MpShown.clear();
    closeCr3Totals(), cr3EffectsShown.clear(), cr3ElementShown.clear(), cr3StIcons.clear();
    cr3VictorUnit = -1, cr3VictoryDuration = 2.5f;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters) cr3HpShown.push_back(f.hp), cr3MpShown.push_back(f.mp);
    for (size_t k = 0; k < cr3Rules.fighters.size(); ++k) {
        const cr3::Cr3Fighter& f = cr3Rules.fighters[k];
        if (!f.player) SDL_Log("battle %d: fighter %zu - monster %d, level %d, HP %d", coupling, k, f.id, f.level, f.maxHp);
    }
    cr3Drops.clear(), cr3DropsAnnounced.clear(), cr3Choices.clear(), cr3Actions.clear(), cr3Popups.clear();
    rollCr3Drops();
    SDL_Log("battle %d: the next wave - battle %d, %zu units", cr3Battle.coupling, coupling, cr3Battle.units.size());
    vsGetSets = 0; // (BtlSystem::CreateMonster 0x7d9960: BtlResult +0 - the get-set count - back to 0)
    playCr3Camera(cr3Battle.cameras[1], 1.0f, false, {0, 0, 0}, 0, 0);
    cr3BattleStage = Cr3BattleStage::Action; // (phaseCallTurnEndEvent: the new script's turn's end, its commands, then the next turn)
    runCr3BattleScript("Battle_Turn_End_Base");
    freezeCr3Shown();
    cr3TurnEndStep = 3;
    if (playCr3TurnEndActions(cr3Rules.scriptCommands(std::exchange(cr3ScriptCommands, {})))) return;
    beginCr3Turn();
}

bool Viewer::startCr3Battle(int coupling, int fieldId) {
    cr3BattleLost = false, cr3Escaped = false, cr3ScriptCommands.clear(), cr3TurnEndStep = 0, cr3DefeatPlayed = false, cr3Opening = Cr3Opening(), cr3TargetIconCycles.clear(), cr3NextCoupling = -1, cr3WaveExp = cr3WaveMoney = 0, cr3WaveDrops.clear(), cr3VictoryIn = false, cr3VictorUnit = -1, cr3VictoryDuration = 2.5f, cr3Drops.clear(), cr3Ctn = Cr3Continue(), cr3GameOverAt = -1; // (BtlSystem::Init: s_ResultParam -1)
    cr3UsedApples[0].clear(), cr3UsedApples[1].clear(), cr3ResultDropLines.clear(), cr3ResultLine.clear();
    cr3GeneVoiceUsed = false;
    clearCr3Effects();
    cr3Held.clear(), cr3StartCommand = {-1, -1}, cr3BattleUiClosed = false; // (the scripts' holds, opening command, UI)
    cr3FieldLoop = true, cr3FieldEnd = -1, cr3FieldTime = cr3FieldDrawnAt = 0;
    for (Cr3FieldObject& o : cr3FieldObjects) unload(o.figure);
    for (auto& [slot, m] : cr3ScriptModels) unload(m.figure);
    cr3FieldObjects.clear(), cr3ScriptModels.clear();
    cr3Turn = 0, cr3ForcedEnd = -1, cr3BattleFieldId = fieldId, cr3FieldEffects.clear(); // (BtlResult's: 0, +4 / +8 none, factors 1)
    vsGetSets = vsGetSetTurn = 0; // (BtlResult +0: the get-set count)
    cr3AutoResult = 0, cr3TabLit = 0, cr3MarkerPressed = false, cr3AutoTaps.clear(); // (the battle's own BtlInterfaceSystem: no auto)
    cr3BattleVista = false;
    cr3ResultAdd[0] = cr3ResultAdd[1] = 0, cr3ResultFactor[0] = cr3ResultFactor[1] = 1;
    std::string err;
    if (!cr3BattleTablesLoaded && !(cr3BattleTablesLoaded = cr3BattleTables.load(archive, &err))) {
        SDL_Log("battle: %s", err.c_str());
        return false;
    }
    const std::vector<int> party = cr3BattlePartyIds();
    if (game.next.cr3BattleParty[0] != -1) SDL_Log("battle %d: the script's party %d %d %d", coupling, party[0], party[1], party[2]);
    if (!cr3Battle.setup(cr3BattleTables, coupling, fieldId, party, uint32_t(SDL_GetTicks()) | 1u, &err, &game.cr3Armour, &game.cr3Weapon)) {
        SDL_Log("battle %d: %s", coupling, err.c_str());
        return false;
    }
    unload(cr3BattleField);
    cr3BattleField = ViewFigure();
    cr3LastAttack = Cr3LastAttack(), cr3White = Ramp{0, 0}, cr3StageHidden = false;
    if (!loadFigure(cr3Battle.field + ".geom", cr3BattleField, &err)) SDL_Log("battle field %s: %s", cr3Battle.field.c_str(), err.c_str());
    cr3BattleField.absolute = true; // (BtlField::Load: its animation looped)
    if (loadLights(cr3Battle.field)) SDL_Log("lights: %s", cr3Battle.field.c_str());
    for (Cr3BattleUnitView& u : cr3BattleUnits) unload(u.body), unload(u.head), unload(u.weapon);
    cr3BattleUnits.assign(cr3Battle.units.size(), Cr3BattleUnitView());
    loadCr3BattleUnits(0);
    if (!cr3BattleCam.load(archive, "Cam_bt")) SDL_Log("battle: no Cam_bt");
    cr3BattleClock = 0, cr3BattleStage = Cr3BattleStage::In, cr3BattleStarted = false, cr3BattleInSkipped = false;
    dropSpark(cr3BattleSpark), cr3BattleSpark.fx.reset(), cr3BattleSparkClock = 0;
    // (the rules' random numbers: the clock's, or CR3_BATTLE_SEED's for a battle to replay)
    const char* fixedSeed = SDL_getenv("CR3_BATTLE_SEED");
    const uint32_t seed = fixedSeed ? uint32_t(std::strtoul(fixedSeed, nullptr, 10)) : uint32_t(SDL_GetTicks()) | 1u;
    cr3CamRng.seed(seed ^ 0x5bd1e995u), cr3LastGlobalCam.clear(); // (the global cameras' picks: from the same seed, a stream of their own)
    cr3PartyRules();
    cr3Rules.partyMpLeft = game.cr3Mp; // (the MP the last battle left)
    cr3Rules.lastAttackLevel = game.cr3LastAttackLevel, cr3Rules.finestArtsLevel = game.cr3FinestArtsLevel; // (the finishers' tiers: CrxCharacterData +0x14 / +0x18)
    cr3Rules.fieldEffects.clear();
    cr3Rules.cheer = game.cr3Cheer; // (the cheer points: calculateAssist's - crx_game_data + 0x1e77c)
    if (!cr3Rules.setup(cr3BattleTables, cr3Battle, game.next.battleLevel, seed, &err)) SDL_Log("battle rules: %s", err.c_str());
    cr3Rules.gold = game.cr3Money; // (the party's money: a Steal Gold's to take from)
    if (gaugeArg != 0) cr3Rules.gauge = std::clamp(gaugeArg * 250, -1000, 1000), cr3Rules.gaugeStarted = true, SDL_Log("battle: the break gauge at %d (test)", cr3Rules.gauge); // (--gauge -4 .. 4: tests)
    for (size_t k = 0; k < cr3Rules.fighters.size(); ++k) {
        const cr3::Cr3Fighter& f = cr3Rules.fighters[k];
        SDL_Log("battle %d: fighter %zu - %s %d, level %d, HP %d", coupling, k, f.player ? "character" : "monster", f.id, f.level, f.maxHp);
    }
    cr3HpShown.clear(), cr3MpShown.clear();
    closeCr3Totals(), cr3EffectsShown.clear(), cr3ElementShown.clear(), cr3StIcons.clear();
    cr3Analyzed.clear(), cr3AnalyzeLines.clear(), cr3AnalyzeDone = false; // (BtlMonsterAnalyzation: empty as BtlSystem is made)
    cr3VictorUnit = -1, cr3VictoryDuration = 2.5f;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters) cr3HpShown.push_back(f.hp), cr3MpShown.push_back(f.mp);
    applyVsBattleStart(); // (VS: the lifeline items, the wildcards)
    cr3GaugeShown = 0;
    rollCr3StartMode();
    cr3AnimCommon.clear();
    if (const cr3::MbeFile::Table* t = cr3BattleTables.model.table("AnimCommon"))
        for (size_t r = 0; r < t->rows.size(); ++r) {
            cr3::MbeFile::Row row;
            if (!cr3BattleTables.model.row(*t, r, "ip", row)) continue;
            if (size_t(row.i(0)) >= cr3AnimCommon.size()) cr3AnimCommon.resize(size_t(row.i(0)) + 1);
            cr3AnimCommon[size_t(row.i(0))] = row.s(1);
        }
    cr3Choices.clear(), cr3Actions.clear(), cr3Popups.clear(), cr3Taps.clear();
    cr3Chooser = -1, cr3Focus = 0;
    initEventDrawing(); // (its script's windows: the tutorials)
    if (runCr3BattleScript("Battle_Boot_Base")) // (BtlSysStateStart::phaseBoot: CallBoot, to its end)
        for (int k = 0; k < 600 && cr3BattleEvents->running(); ++k) cr3BattleEvents->tick();
    // (phaseStart - BtlStatusList::GiveFieldStatusEffect 0x9c21f4: the field's effects on the living players,
    // for good - none on one with a skill of kind 0x6a -, again at every turn's start: Cr3BattleRules::turn)
    cr3Rules.fieldEffects = cr3FieldEffects;
    cr3Rules.giveFieldEffects();
    if (!cr3FieldEffects.empty()) SDL_Log("battle %d: %zu field status effects", coupling, cr3FieldEffects.size());
    cr3DropsAnnounced.clear();
    rollCr3Drops(); // (the first wave's drops, as it's made)
    playCr3Camera(cr3Battle.cameras[0], cr3Battle.getSetEnd, false, {0, 0, 0}, 0, 0); // (PlayBattleIn)
    cr3BattleMode = true;
    // (BtlSystem::Init: the colosseum's commentators in a VS battle - BtlBootParam +0x34 -; phaseStart's
    // PlayBattleInByGhostIndex 0x9a84f8: the first battle categories 1, 4, 5 queued, the second 2, the third 3)
    if (vs.screen == 5) {
        startVsCommentary(vsCommentators[0], vsCommentators[1]);
        if (vs.battle == 0) requestVsComment(1, 1.0f), requestVsComment(4, 0.1f), requestVsComment(5, 0.1f);
        else playVsComment(vs.battle == 1 ? 2 : 3);
    } else vsCommentary = VsCommentary();
    dev->resetHistory();
    status = "battle " + std::to_string(coupling) + " on " + cr3Battle.field;
    SDL_Log("battle %d: %s, %zu units, camera size %d (%s, %.2f s)", coupling, cr3Battle.field.c_str(), cr3Battle.units.size(),
            cr3Battle.cameraSize, cr3Cam.name.c_str(), cr3Battle.getSetEnd);
    return true;
}

void Viewer::updateCr3Battle(float dt) {
    cr3BattleClock += stepCr3SpellCardClock(dt);
    if (cr3Glass.running()) cr3Glass.advanceTo(cr3BattleClock - cr3GlassStart);
    if (cr3BattleEvents) { // (at the game's updates)
        cr3BattleEventClock = std::min(cr3BattleEventClock + dt, 0.25f);
        for (; cr3BattleEventClock >= cr3::EventRuntime::kTick; cr3BattleEventClock -= cr3::EventRuntime::kTick)
            if (!cr3BattleEvents->tick()) {
                SDL_Log("battle script: %s", cr3BattleEvents->error().c_str());
                break;
            }
    }
    const bool scripted = cr3BattleEvents && cr3BattleEvents->running();
    const float camT = cr3BattleClock - cr3Cam.start;
    if (vs.wildOpen && !inputScript.empty() && inputScript.front().compare(0, 7, "vs:wild") == 0 && uiClock - vs.wildAt > 0.8f) {
        const int k = std::atoi(inputScript.front().c_str() + 7); // (--input vs:wild<n>: a wildcard, for tests)
        inputScript.pop_front();
        chooseVsWildCard(k);
    }
    // BtlSysStateStart::phaseBattleIn: the battle-in camera over (or a tap) - PlayGetSet, its
    // end held; then the battle's start script (Battle_Start_Base) and the start's announcement, the first
    // command's camera on the party's first
    if (cr3BattleStage == Cr3BattleStage::In && ((camT >= cr3Cam.end && !cr3Glass.running()) || cr3BattleInSkipped)) { // (and the glass: CheckGlassEffect)
        cr3BattleStage = Cr3BattleStage::GetSet;
        cr3Cam.held = true;
    } else if (cr3BattleStage == Cr3BattleStage::GetSet && !cr3BattleStarted) {
        cr3BattleStarted = true; // (phaseBattleIn: PlayGetSet, then CallStart)
        runCr3BattleScript("Battle_Start_Base");
    } else if (cr3BattleStage == Cr3BattleStage::GetSet && !scripted && camT >= cr3Cam.end && !vs.wildOpen) { // (VS: a wildcard first)
        // (phaseFirstStrike: a surprise attack - phaseCalculate at once, the party's commands none;
        // phaseGetSetStart 0x009d3b2c with BattleCommand.SetStart's: that command alone added, the
        // start cleared, phaseCalculate - the monsters' own too -, the opening none - isOpening)
        if (cr3StartMode == 2 || cr3StartCommand[0] >= 0) {
            cr3StartMode = 0, ++cr3Turn;
            cr3Choices.clear();
            if (cr3StartCommand[0] >= 0)
                cr3Choices.push_back({cr3StartCommand[0], cr3StartCommand[1], -1}), cr3StartCommand = {-1, -1}, cr3Rules.monstersWait = false;
            commentCr3TurnStart(), freezeCr3Shown(), cr3Actions = cr3Rules.turn(cr3Choices);
            cr3CountRecords();
            cr3ActionAt = 0;
            cr3BattleStage = Cr3BattleStage::Action, cr3StageStart = cr3BattleClock;
            startCr3Action();
        } else
            beginCr3Turn();
    } else if (cr3ForcedEnd >= 0 && !scripted && cr3BattleStage >= Cr3BattleStage::Menu && cr3BattleStage <= Cr3BattleStage::Escape) {
        endCr3Actions(); // (forced by a script while choosing - the start's, the turn end's)
    } else if (cr3BattleStage == Cr3BattleStage::Menu || cr3BattleStage == Cr3BattleStage::Command || cr3BattleStage == Cr3BattleStage::Target ||
               cr3BattleStage == Cr3BattleStage::List || (cr3BattleStage == Cr3BattleStage::Escape && cr3EscapeAsk)) {
        // (--input: "attack" / "guard", a target "t<n>" - the n-th monster alive; "escape" then
        // "yes" / "no" - once the battle's script is over, as the menu waits: a turn end's scene)
        takeAutoBattleToken();
        if (!scripted && !inputScript.empty() && inputScript.front() == "win") { // (--input win: tests - won as Battle.ForceVictory)
            inputScript.pop_front();
            SDL_Log("battle %d: won by the test input", cr3Battle.coupling);
            cr3ForcedEnd = 0;
            return;
        }
        // (--input tab:mass / tab:auto: the get-set's tabs, for tests)
        if (!scripted && cr3BattleStage == Cr3BattleStage::Menu && cr3AutoResult == 0 && !inputScript.empty() &&
            (inputScript.front() == "tab:mass" || inputScript.front() == "tab:auto") && cr3BattleClock - cr3StageStart > 0.6f) {
            const int b = inputScript.front() == "tab:mass" ? 5 : 6;
            inputScript.pop_front();
            chooseCr3GetSet(b);
            return;
        }
        if (cr3BattleStage == Cr3BattleStage::Menu && cr3AutoResult != 0) { // (the Auto Battle / Mass Attack: the turn chosen alone)
            if (!scripted) beginCr3AutoTurn();
            return;
        }
        const bool own = !inputScript.empty() && isCr3BattleToken(inputScript.front());
        if (!scripted && autoBattle && !own && cr3BattleClock - cr3StageStart > 0.6f) { // (--input auto: attack the first monster alive; auto:gene its first gene command on a foe first)
            if (cr3BattleStage == Cr3BattleStage::Menu) autoGeneSkip = -1, chooseCr3GetSet(0);
            else if (cr3BattleStage == Cr3BattleStage::Command) { // (a button a status shuts - menuEnabled - passed over: a guard at last)
                int b = autoGene && autoGeneSkip != cr3Chooser && cr3Rules.menuEnabled(cr3Chooser, 1) ? 1 : 0;
                if (b == 0 && !cr3Rules.menuEnabled(cr3Chooser, 0)) b = 3;
                chooseCr3Command(b);
            }
            else if (cr3BattleStage == Cr3BattleStage::Target) chooseCr3Target(0);
            else if (cr3BattleStage == Cr3BattleStage::List) {
                int pick = -1;
                for (size_t k = 0; k < cr3List.size() && pick < 0; ++k) {
                    cr3::Cr3Command c;
                    if (cr3List[k].usable && !cr3List[k].item && cr3Rules.command(cr3List[k].command, c) && c.damage > 0 && !c.ally()) pick = int(k);
                }
                if (pick >= 0) chooseCr3ListRow(pick);
                else autoGeneSkip = cr3Chooser, cr3BattleStage = Cr3BattleStage::Command; // (none usable: an attack)
            }
        } else if (!scripted && !inputScript.empty() && cr3BattleClock - cr3StageStart > 0.6f) {
            const std::string tok = inputScript.front();
            if (cr3BattleStage == Cr3BattleStage::Command && (tok == "attack" || tok == "guard")) {
                inputScript.pop_front();
                chooseCr3Command(tok == "attack" ? 0 : 3);
            } else if (cr3BattleStage == Cr3BattleStage::Escape && (tok == "yes" || tok == "no")) {
                inputScript.pop_front();
                chooseCr3Escape(tok == "yes" ? 0 : 1);
            } else if (cr3BattleStage == Cr3BattleStage::Menu && (tok == "battle" || tok == "escape" || tok == "options")) {
                inputScript.pop_front();
                chooseCr3GetSet(tok == "battle" ? 0 : tok == "escape" ? 1 : 2);
            } else if (cr3BattleStage == Cr3BattleStage::Command && tok.compare(0, 4, "gene") == 0) {
                chooseCr3Command(1);
            } else if (cr3BattleStage == Cr3BattleStage::Command && tok.compare(0, 4, "item") == 0) {
                chooseCr3Command(2);
            } else if (cr3BattleStage == Cr3BattleStage::List && (tok == "gene" || tok == "item")) {
                inputScript.pop_front(); // (the list opened alone: it waits)
            } else if (cr3BattleStage == Cr3BattleStage::List && (tok.compare(0, 4, "gene") == 0 || tok.compare(0, 4, "item") == 0)) {
                inputScript.pop_front();
                chooseCr3ListRow(std::atoi(tok.c_str() + 4));
                if (cr3BattleStage == Cr3BattleStage::List) cr3BattleStage = Cr3BattleStage::Command; // (not usable: back)
            } else if (cr3BattleStage == Cr3BattleStage::Menu && (tok == "attack" || tok == "guard" || tok.compare(0, 4, "gene") == 0 || tok.compare(0, 4, "item") == 0)) {
                chooseCr3GetSet(0); // (the get-set's Battle implied)
            } else if (cr3BattleStage == Cr3BattleStage::Target && tok.size() > 1 && tok[0] == 't') {
                inputScript.pop_front();
                chooseCr3Target(std::atoi(tok.c_str() + 1));
            } else if (cr3BattleStage == Cr3BattleStage::Target &&
                       (tok == "attack" || tok == "guard" || tok.compare(0, 4, "gene") == 0 || tok.compare(0, 4, "item") == 0 || tok == "result" || tok == "win")) {
                chooseCr3Target(0); // (an ally or all-target row's confirmation implied)
            }
        }
    } else if (cr3BattleStage == Cr3BattleStage::Escape) {
        stepCr3Escape();
    } else if (cr3BattleStage == Cr3BattleStage::Action) {
        if (!scripted) stepCr3Action(); // (a battle script's direction - Battle_Direction_End's cut-in - first)
    } else if (cr3BattleStage == Cr3BattleStage::Won && vs.screen == 6) { // (VS: the score up - a tap, or --input vs:next, goes on)
        while (!inputScript.empty() && inputScript.front() != "vs:next" &&
               (inputScript.front() == "battle" || inputScript.front() == "attack" || inputScript.front() == "guard" ||
                (inputScript.front().size() > 1 && inputScript.front()[0] == 't' && std::isdigit(static_cast<unsigned char>(inputScript.front()[1])))))
            inputScript.pop_front(); // (the battle's leftover choices)
        stepVsScore(dt);
        if (!inputScript.empty() && inputScript.front() == "vs:next" && (vs.scoreStage == 4 || vs.scoreStage == 6)) tapVsScore(); // (taps until it is out)
    } else if (cr3BattleStage == Cr3BattleStage::Won && !scripted && !cr3VictoryIn) {
        // (phaseVictoryEvent's end: a next wave - BattleCoupling.SetId -, else phaseVictoryIn:
        // PlayResultBgm)
        if (cr3NextCoupling >= 0) {
            const int next = cr3NextCoupling;
            cr3NextCoupling = -1;
            reinforceCr3Battle(next);
        } else {
            cr3VictoryIn = true;
            cr3StageStart = cr3BattleClock;
            cr3Popups.clear(); // (BtlSysStateAction::phaseVictoryIn -> DeleteDamage)
            int victor = -1;
            if (!cr3Actions.empty()) {
                const int last = cr3Actions.back().source;
                if (last >= 0 && size_t(last) < cr3Rules.fighters.size() && cr3Rules.fighters[size_t(last)].player && !cr3Rules.fighters[size_t(last)].dead())
                    victor = last;
            }
            for (size_t k = 0; victor < 0 && k < cr3Rules.fighters.size(); ++k)
                if (cr3Rules.fighters[k].player && !cr3Rules.fighters[k].dead()) victor = int(k);
            cr3VictorUnit = victor >= 0 ? cr3Rules.fighters[size_t(victor)].unit : -1;
            cr3VictoryDuration = 2.5f;
            if (cr3VictorUnit >= 0 && size_t(cr3VictorUnit) < cr3Battle.units.size()) {
                cr3BattleUnits[size_t(cr3VictorUnit)].scriptHidden = false; // (BtlUnit::EnableDraw: a death direction's Hide undone for the finisher)
                setCr3Motion(size_t(cr3VictorUnit), cr3MotionOf(2), false, true);
                const std::string soloCam = cr3BattleTables.victoryCamera(cr3Battle.units[size_t(cr3VictorUnit)].model);
                if (!soloCam.empty()) {
                    playCr3CasterCamera(soloCam, size_t(cr3VictorUnit));
                    if (cr3Cam.name == soloCam && cr3Cam.end > 0) cr3VictoryDuration = cr3Cam.end;
                }
            } else {
                for (const cr3::Cr3Fighter& f : cr3Rules.fighters)
                    if (f.player && !f.dead()) setCr3Motion(size_t(f.unit), cr3MotionOf(2), false, true);
            }
            if (game.next.battleMusicAfter >= 0) audio.playBgm(game.next.battleMusicAfter, 0);
            SDL_Log("battle: victory camera ready (unit %d, %.2f s)", cr3VictorUnit, cr3VictoryDuration);
        }
    } else if (cr3BattleStage == Cr3BattleStage::Won) {
        // (BtlSysStateAction::phaseVictory: CheckFinishMotion on the solo victory camera, then
        // result unless story flag 5051 - DAT_014cfb67 bit 3 - is on, as for the ship's battles)
        if (!scripted && cr3BattleClock - cr3StageStart >= cr3VictoryDuration) {
            if (vs.screen == 5) vsBattleOver(true); // (VS: its score - VsColosseumResultMenu)
            else if (game.flag(5051)) endCr3Battle();
            else enterCr3Result();
        }
    } else if (cr3BattleStage == Cr3BattleStage::Result) {
        // (a battle's leftover choices - won before they were used - are dropped up to "result")
        while (!inputScript.empty() && inputScript.front() != "result" &&
               (inputScript.front() == "battle" || inputScript.front() == "escape" || inputScript.front() == "attack" ||
                inputScript.front() == "guard" || inputScript.front().compare(0, 4, "gene") == 0 ||
                inputScript.front().compare(0, 4, "item") == 0 || (inputScript.front().size() > 1 && inputScript.front()[0] == 't' && std::isdigit(static_cast<unsigned char>(inputScript.front()[1])))))
            inputScript.pop_front();
        stepCr3Result();
        if (!inputScript.empty() && inputScript.front() == "result" && cr3BattleClock - cr3StageStart > 1.5f)
            inputScript.pop_front(), settleCr3GeneQueues(), endCr3Battle();
        else if (autoBattle && cr3BattleClock - cr3StageStart > 1.5f && (inputScript.empty() || inputScript.front() != "result:hold"))
            autoCr3Result(); // (--input auto: the windows stepped through, each drop's line a second; result:hold - left to the keys / a controller)
    } else if (cr3BattleStage == Cr3BattleStage::Lost) {
        if (!scripted && cr3ForcedEnd == 3) { // (a story's loss: its defeat script's event, then Battle.ForceEnd - over, not lost)
            cr3ForcedEnd = -1, cr3BattleLost = false;
            SDL_Log("battle %d: ended by its script", cr3Battle.coupling);
            endCr3Battle();
        } else if (!scripted && cr3Ctn.stage == 0 && !cr3Ctn.decided && (cr3Battle.couplingFlags & 0x20) && vs.screen != 5) {
            openCr3Continue(); // (MbCouplingInfo::CanContinue: mon_cop +0x44 bit 5)
        } else if (cr3Ctn.stage != 0) {
            stepCr3Continue();
        } else if (!scripted && cr3BattleClock - cr3StageStart > 2.5f) {
            if (vs.screen == 5) vsBattleOver(false); // (VS: the tournament lost, no game over)
            else if (cr3GameOverAt < 0) { // (BtlGameOverUI: GameOverMenu, then a half-second fade and out)
                cr3GameOverAt = cr3BattleClock;
                SDL_Log("battle %d: game over (%.2f s)", cr3Battle.coupling, cr3BattleClock);
            } else if (cr3BattleClock - cr3GameOverAt >= cr3GameOverLength() + 0.5f)
                endCr3Battle();
        }
    }
    // the units' motions: one played out goes back to the stance (a fall's held)
    for (size_t k = 0; k < cr3BattleUnits.size(); ++k) {
        Cr3BattleUnitView& u = cr3BattleUnits[k];
        if (u.motionLoop || u.motionHold || !u.body.animLoaded) continue;
        if (cr3BattleClock - u.motionStart >= u.body.anim.duration()) setCr3Motion(k, "bb01", true);
    }
}

// My Page's records of a turn (CrxGameActivities::KillMonster - 999999 at most - and Damage:
// the party's most in one hit)
void Viewer::cr3CountRecords() {
    // (the cheer points an assist spent - crx_game_data's, kept: GameState::cr3Cheer)
    if (game.cr3Cheer != cr3Rules.cheer) SDL_Log("battle: an assist - cheer points %d -> %d", game.cr3Cheer, cr3Rules.cheer);
    game.cr3Cheer = cr3Rules.cheer;
    cr3::GameState::Cr3Records& r = game.cr3Records;
    for (const cr3::Cr3Action& a : cr3Actions) {
        if (a.source < 0 || size_t(a.source) >= cr3Rules.fighters.size() || !cr3Rules.fighters[size_t(a.source)].player) continue;
        for (const cr3::Cr3Hit& h : a.targets) {
            if (h.target < 0 || size_t(h.target) >= cr3Rules.fighters.size() || cr3Rules.fighters[size_t(h.target)].player) continue;
            for (size_t k = 0; k < h.hits.size(); ++k)
                if (!(k < h.flags.size() && (h.flags[k] & 4))) r.maxDamage = std::max(r.maxDamage, h.hits[k]);
            if (h.killed && r.kills < 999999) ++r.kills;
        }
    }
}

void Viewer::setCr3Motion(size_t k, const std::string& suffix, bool loop, bool hold) {
    if (k >= cr3BattleUnits.size()) return;
    Cr3BattleUnitView& u = cr3BattleUnits[k];
    bool found = false;
    for (ViewFigure* part : {&u.body, &u.head}) {
        if (part->meshes.empty()) continue;
        const std::string base = motionBase(part->name.substr(0, part->name.size() - 5));
        const auto it = std::find(part->anims.begin(), part->anims.end(), base + "_" + suffix + ".anim");
        if (it == part->anims.end()) continue;
        const float keep = time;
        loadAnim(*part, size_t(it - part->anims.begin()));
        time = keep;
        found = found || part == &u.body;
    }
    if (!found) return;
    u.motion = suffix, u.motionStart = cr3BattleClock, u.motionLoop = loop, u.motionHold = hold;
}

std::string Viewer::cr3MotionOf(int type) const {
    return type >= 0 && size_t(type) < cr3AnimCommon.size() ? cr3AnimCommon[size_t(type)] : std::string();
}

void Viewer::startCr3Melee(size_t source, size_t target) {
    Cr3BattleUnitView& view = cr3BattleUnits[source];
    const cr3::Cr3BattleUnit& actor = cr3Battle.units[source];
    const cr3::Cr3BattleUnit& foe = cr3Battle.units[target];
    const float span = cr3Battle.fieldSpan;
    const float distance = span + (actor.depth * actor.scale + foe.depth * foe.scale) * 0.5f;
    if (span <= 0 || distance <= 0 || view.motion != "bo01" || !view.body.animLoaded) return;
    const std::string back = motionBase(actor.figure) + "_bo04.anim";
    if (std::find(view.body.anims.begin(), view.body.anims.end(), back) == view.body.anims.end()) return;
    cr3::Cr3BattleTables::MotionTravel approach;
    if (!cr3BattleTables.motionTravel(actor.model, 11, approach) ||
        !cr3BattleTables.motionTravel(actor.model, 14, cr3ActionPlay.returnTravel)) return;
    // BtlUnit::ChangeTarget preserves each model's half-depth at contact.
    const float x = foe.home[0] - actor.home[0], z = foe.home[2] - actor.home[2];
    const float turn = x == 0 && z == 0 ? 0 : turnBetween(actor.yaw, std::atan2(x, z), 1) - actor.yaw;
    view.meleeOffset = {x * span / distance, 0, z * span / distance};
    view.meleeStep.begin(cr3BattleClock + approach.stepStart, 0, 1, approach.stepEnd - approach.stepStart);
    view.meleeTurn.begin(cr3BattleClock + approach.turnStart, 0, turn, approach.turnEnd - approach.turnStart);
    view.motionHold = true;
    cr3ActionPlay.melee = true;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: %s steps toward %s", cr3BattleClock, actor.name.c_str(), foe.name.c_str());
}

void Viewer::stepCr3MeleeReturn(size_t source, float actionTime) {
    if (!cr3ActionPlay.melee || cr3ActionPlay.nextMotion < cr3ActionPlay.timing.motions.size()) return;
    Cr3BattleUnitView& view = cr3BattleUnits[source];
    if (view.motion == "bo04") {
        if (cr3BattleClock < std::max(view.meleeStep.start + view.meleeStep.time, view.meleeTurn.start + view.meleeTurn.time)) return;
        cr3ActionPlay.melee = false;
        setCr3Motion(source, "bb01", true);
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: %s returns to stance", cr3BattleClock, cr3Battle.units[source].name.c_str());
        return;
    }
    if (cr3BattleClock - view.motionStart < view.body.anim.duration()) return;
    setCr3Motion(source, "bo04", false, true);
    if (view.motion != "bo04" || !view.body.animLoaded) {
        view.meleeStep = Ramp{0, 0};
        view.meleeTurn = Ramp{0, 0};
        cr3ActionPlay.melee = false;
        setCr3Motion(source, "bb01", true);
        return;
    }
    const auto& back = cr3ActionPlay.returnTravel;
    view.meleeStep.begin(cr3BattleClock + back.stepStart, view.meleeStep.at(cr3BattleClock), 0, back.stepEnd - back.stepStart);
    view.meleeTurn.begin(cr3BattleClock + back.turnStart, view.meleeTurn.at(cr3BattleClock), 0, back.turnEnd - back.turnStart);
    cr3ActionPlay.end = std::max(cr3ActionPlay.end, actionTime + std::max(back.stepEnd, back.turnEnd));
}

ViewFigure* Viewer::cr3FxFigure(const std::string& name) {
    auto& slot = cr3FxFigures[name];
    if (!slot) {
        slot = std::make_unique<ViewFigure>();
        std::string err;
        if (!loadFigure(name + ".geom", *slot, &err)) SDL_Log("effect %s: %s", name.c_str(), err.c_str());
        slot->absolute = true;
    }
    return slot->meshes.empty() ? nullptr : slot.get();
}

void Viewer::clearCr3Effects() {
    for (Cr3Fx& effect : cr3Fx) dropSpark(effect.spark);
    cr3Fx.clear();
}

auto Viewer::spawnCr3Effect(int id, const cr3::Vec3& at, float scale, float start) -> Cr3Fx* {
    cr3::MbeFile::Row r;
    if (id <= 0 || !cr3BattleTables.effect.find("Effect", id, "i_pppiff", r)) return nullptr;
    Cr3Fx fx;
    fx.figure = r.s(1);
    fx.at = at, fx.start = start < 0 ? cr3BattleClock : start, fx.scale = scale;
    fx.loop = (r.i(4) & 1) != 0;
    const ViewFigure* figure = fx.figure.empty() ? nullptr : cr3FxFigure(fx.figure);
    if (!fx.figure.empty())
        if (const cr3::SparkAsset* spark = sparkAsset(fx.figure)) fx.spark.fx = std::make_unique<cr3::SparkEffect>(spark, SDL_rand_bits());
    if (figure && figure->animLoaded) {
        const float duration = std::max(figure->anim.duration(), 0.0f);
        const float end = r.f(6) >= 0 ? std::min(r.f(6) / 30, duration) : duration;
        fx.animStart = std::clamp(r.f(5) / 30, 0.0f, end);
        fx.animLength = end - fx.animStart;
    }
    if (!r.s(2).empty()) audio.playSe(r.s(2), false);
    if (!fx.spark.fx && !figure) return nullptr;
    cr3Fx.push_back(std::move(fx));
    return &cr3Fx.back();
}

cr3::Vec3 Viewer::cr3EffectPoint(size_t unit, int index) {
    const cr3::Cr3BattleUnit& bu = cr3Battle.units[unit];
    const auto point = cr3BattleTables.effectPoint(bu.model, index);
    const Cr3BattleUnitView& view = cr3BattleUnits[unit];
    cr3::Mat34 place = view.body.place;
    const bool found = !point.joint.empty() && (jointWorld(view.body, point.joint, place) ||
                       jointWorld(view.weapon, point.joint, place) || jointWorld(view.head, point.joint, place));
    cr3::Vec3 at{place.m[3], found ? place.m[7] : 0.0f, place.m[11]};
    for (int col = 0; col < 3; ++col) {
        const float scale = std::hypot(place.m[col], place.m[col + 4], place.m[col + 8]);
        if (scale > 0)
            for (int row = 0; row < 3; ++row) at[row] += place.m[row * 4 + col] * point.offset[col] / scale;
    }
    return at;
}

void Viewer::launchCr3Projectile(size_t source, size_t target, float at) {
    const auto& shot = cr3ActionPlay.timing.projectile;
    const cr3::Vec3 from = cr3EffectPoint(source, shot.sourcePoint);
    const cr3::Vec3 to = cr3EffectPoint(target, shot.targetPoint);
    Cr3Fx* fx = spawnCr3Effect(shot.effect, from, 1, at);
    if (!fx) return;
    float x = shot.spreadX[0] + (shot.spreadX[1] - shot.spreadX[0]) * SDL_randf();
    float y = shot.spreadY[0] + (shot.spreadY[1] - shot.spreadY[0]) * SDL_randf();
    const float spread = std::hypot(x, y);
    if (spread > 0) x *= std::abs(x) * 0.1f / spread, y *= std::abs(y) * 0.1f / spread;
    const float yaw = cr3Battle.units[source].yaw;
    cr3::Vec3 via{std::cos(yaw) * x, y, -std::sin(yaw) * x};
    for (int i = 0; i < 3; ++i) via[i] += from[i] + (to[i] - from[i]) * shot.waypoint;
    fx->life = shot.duration;
    fx->path = {from, via, to, shot.speed, shot.acceleration};
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: projectile %s launched", cr3BattleClock, fx->figure.c_str());
}

void Viewer::knockOutCr3Unit(size_t k) {
    Cr3BattleUnitView& view = cr3BattleUnits[k];
    if (view.knockedOut) return;
    view.knockedOut = true;
    const cr3::Cr3BattleUnit& unit = cr3Battle.units[k];
    // BtlUnit::PlayKnockout chooses the presentation from the model's flags.
    if (unit.modelFlags & 2) {
        setCr3Motion(k, cr3MotionOf(6), false, true);
        if (unit.modelFlags & 16) {
            const float delay = std::max(cr3BattleTables.animationCueTime(unit.model, 6), 0.0f);
            view.opacity.begin(cr3BattleClock + delay, 1, 0, 1);
        }
    } else {
        view.opacity.begin(cr3BattleClock, 1, 0, 1);
        spawnCr3Effect(0xb9, unit.home, unit.effectScale * unit.scale);
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: %s knocked out", cr3BattleClock, unit.name.c_str());
}

void Viewer::restoreCr3Unit(size_t k) {
    Cr3BattleUnitView& view = cr3BattleUnits[k];
    const bool revive = view.knockedOut && (cr3Battle.units[k].modelFlags & 2);
    view.knockedOut = false;
    view.opacity = Ramp();
    setCr3Motion(k, "bb01", true);
    if (revive) setCr3Motion(k, cr3MotionOf(8), false);
}

// BtlStatusList::PlayRelease (0x009bf930): the holder lets go - BtlUnit::Detach 0x009dfdf8, the
// held back at its home -, then BtlStatus::PlayRelease 0x009b5280 on the held: hit effect 0x34 at
// its effect point 5; fallen - a kind 76 effect, KO's - knocked out at the motion's end, else
// AnimCommon 8, up again
void Viewer::releaseCr3Hold(int holder) {
    const auto it = cr3Held.find(holder);
    if (it == cr3Held.end()) return;
    const int held = it->second.first;
    cr3Held.erase(it);
    if (held < 0 || size_t(held) >= cr3Rules.fighters.size() || cr3Rules.fighters[size_t(held)].unit < 0) return;
    const size_t hu = size_t(cr3Rules.fighters[size_t(held)].unit);
    spawnCr3Effect(0x34, cr3EffectPoint(hu, 5), 1.0f);
    if (cr3Rules.fighters[size_t(held)].dead() || cr3Rules.hasKind(cr3Rules.fighters[size_t(held)], 76)) {
        knockOutCr3Unit(hu);
        cr3BattleUnits[hu].motionStart = -1e6f; // (SeekAnimEnd)
    } else {
        setCr3Motion(hu, cr3MotionOf(8), false);
    }
    SDL_Log("battle %.2f s: %d lets %d go", cr3BattleClock, holder, held);
}

// BtlDamageTarget::PlaySingleHitEffect 0x986160 / getOffsetRotation 0x985c58 / BtlUnit::PlayHitEffect 0x9de81c: one
// placement for all three effects. +0x58 bit 3 an offset round an ellipse - a whole degree a (CrxRandom % 360), x +0x60
// sin a, y +0x64 cos a, added to the point unturned -; bits 6 / 5 / 4 a random whole-degree turn about z / y / x (the
// quaternions multiplied in that order, then by the point's own turn); bit 7 the random size, bit 2 times the target's
// effect scale (BtlUnit::GetEffectScale 0x9de6a0: the Model row's +0x28 x the model's scale)
void Viewer::cr3HitEffects(int command, int element, size_t targetUnit, bool critical) {
    cr3::MbeFile::Row d;
    if (!cr3BattleTables.direction.find("CommandDirection", command, "iiiiiii_piiiiiiiiiiiiiiiii", d)) return;
    const uint32_t flags = uint32_t(d.i(20)); // (+0x58)
    const auto degrees = [] { return float(SDL_rand(360)) * 0.01745328f; };
    std::array<float, 3> at = unitPoint(targetUnit, "DummyDP01", 1.2f);
    if (flags & 8) {
        const float a = degrees();
        at[0] += d.f(22) * std::sin(a), at[1] += d.f(23) * std::cos(a); // (+0x60 / +0x64)
    }
    std::array<float, 4> turn{0, 0, 0, 1}; // (x, y, z, w)
    for (int axis = 2; axis >= 0; --axis) {
        if (!(flags & (0x10u << axis))) continue; // (bit 6 z, 5 y, 4 x)
        const float half = degrees() * 0.5f, s = std::sin(half), c = std::cos(half);
        std::array<float, 4> r{0, 0, 0, c};
        r[size_t(axis)] = s;
        const auto& q = turn;
        turn = {q[3] * r[0] + q[0] * r[3] + q[1] * r[2] - q[2] * r[1], q[3] * r[1] - q[0] * r[2] + q[1] * r[3] + q[2] * r[0],
                q[3] * r[2] + q[0] * r[1] - q[1] * r[0] + q[2] * r[3], q[3] * r[3] - q[0] * r[0] - q[1] * r[1] - q[2] * r[2]};
    }
    float scale = 1;
    if (flags & 0x80) scale = (SDL_randf() * 2 - 1) * d.f(24) + 1; // (+0x68)
    if ((flags & 4) && targetUnit < cr3Battle.units.size()) scale *= cr3Battle.units[targetUnit].scale * cr3Battle.units[targetUnit].effectScale;
    const auto spawn = [&](int id) {
        if (Cr3Fx* fx = spawnCr3Effect(id, at, scale)) fx->turn = turn;
    };
    if (cr3ActionPlay.timing.effects.empty()) spawn(d.i(9));
    if (flags & 0x100) { // (+0x59 bit 0)
        if (critical) spawn(4);
        if (element > 0) spawn(element + 4);
    }
}

void Viewer::drawCr3Effects(const cr3::Mat34& camera, const Mat4& vp) {
    for (auto it = cr3Fx.begin(); it != cr3Fx.end();) {
        ViewFigure* f = cr3FxFigure(it->figure);
        const float t = std::max(cr3BattleClock - it->start, 0.0f);
        const float duration = it->animLength;
        const float life = it->life >= 0 ? it->life : duration > 0 ? duration : 1.0f;
        if (t > life) {
            dropSpark(it->spark);
            it = cr3Fx.erase(it);
            continue;
        }
        cr3::Mat34 place;
        place.m[0] = place.m[10] = std::cos(it->yaw);
        place.m[2] = std::sin(it->yaw);
        place.m[8] = -place.m[2];
        cr3::Vec3 at = it->at;
        if (it->path.speed > 0 || it->path.acceleration > 0) {
            at = it->path.at(t);
            const cr3::Vec3 from = it->path.at(std::max(t - 0.001f, 0.0f));
            const cr3::Vec3 to = it->path.at(std::min(t + 0.001f, life));
            const float dx = from[0] - to[0], dy = from[1] - to[1], dz = from[2] - to[2];
            const float yaw = std::atan2(dx, dz), pitch = -std::atan2(dy, std::hypot(dx, dz));
            const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
            place = {{cy, sy * sp, sy * cp, 0, 0, cp, -sp, 0, -sy, cy * sp, cy * cp, 0}};
        } else if (it->followUnit >= 0 && size_t(it->followUnit) < cr3BattleUnits.size()) {
            at = cr3EffectPoint(size_t(it->followUnit), it->point);
        }
        if (it->turn[3] != 1) { // (a hit effect's random turn, after its yaw)
            const float x = it->turn[0], y = it->turn[1], z = it->turn[2], w = it->turn[3];
            const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                                2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                                2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
            cr3::Mat34 turned = place;
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    turned.m[row * 4 + col] = place.m[row * 4] * r[col] + place.m[row * 4 + 1] * r[3 + col] + place.m[row * 4 + 2] * r[6 + col];
            place = turned;
        }
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) place.m[row * 4 + col] *= it->scale;
            place.m[row * 4 + 3] = at[row];
        }
        if (f) {
            f->place = place;
            poseView = camera;
            const float animTime = it->loop && duration > 0 ? std::fmod(t, duration) : std::min(t, duration);
            pose(*f, it->animStart + animTime, false);
            drawFigure(*f, vp.data());
        }
        if (it->spark.fx) {
            it->spark.fx->step(t - it->sparkTime);
            it->sparkTime = t;
            drawSpark(it->spark, place, camera, vp.data());
        }
        ++it;
    }
}

void Viewer::stepCr3CastEffects(size_t source, float t) {
    auto& play = cr3ActionPlay;
    const auto& timing = play.timing;
    while (play.nextCastEffect < timing.castEffects.size() && t >= timing.castEffects[play.nextCastEffect].start) {
        const auto& cue = timing.castEffects[play.nextCastEffect++];
        if (Cr3Fx* fx = spawnCr3Effect(cue.effect, cr3EffectPoint(source, cue.point), 1, cr3ActionStart + cue.start)) {
            if (cue.end > cue.start) fx->life = cue.end - cue.start;
            if (cue.follow) fx->followUnit = int(source), fx->point = cue.point;
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: cast effect %s", cr3BattleClock, fx->figure.c_str());
        }
    }
}

// BtlShotCoverDirection::VUpdate 0x99acc0 at the shot's event 2 - its bullet's launch: BtlUnit::PlayCover 0x9df840 - the
// coverer from its home to (the original's x, 0, its z less both units' half depths: not traced - 0.8 here), over the
// bullet's flight (motion state 0xe unsettled: the stance kept) -, the shot aimed at it; both home at the end (stepCr3Action)
void Viewer::startCr3Cover(const cr3::Cr3Hit& hit, float flight) {
    const size_t cu = size_t(cr3Rules.fighters[size_t(hit.target)].unit), ou = size_t(cr3Rules.fighters[size_t(hit.coveredFrom)].unit);
    const cr3::Cr3BattleUnit &c = cr3Battle.units[cu], &o = cr3Battle.units[ou];
    Cr3BattleUnitView& view = cr3BattleUnits[cu];
    view.meleeOffset = {o.home[0] - c.home[0], 0, o.home[2] - 0.8f - c.home[2]};
    view.meleeStep.begin(cr3BattleClock, 0, 1, std::max(flight, 0.2f)), view.meleeTurn = Ramp{0, 0};
    SDL_Log("battle %.2f s: a cover - fighter %d takes the shot for fighter %d", cr3BattleClock, hit.target, hit.coveredFrom);
}

void Viewer::stepCr3ActionEffects(const cr3::Cr3Action& action, size_t source, float t) {
    using Placement = cr3::Cr3BattleTables::ActionTiming::Placement;
    auto& play = cr3ActionPlay;
    const auto& timing = play.timing;
    while (play.nextEffect < timing.effects.size() && t >= timing.effects[play.nextEffect].time) {
        const auto& cue = timing.effects[play.nextEffect++];
        const float start = cr3ActionStart + cue.time;
        if (cue.launch) { // (a steal's: from each target back to the caster - BtlDamageTarget::PlayStealEffect 0x986fcc)
            for (const auto& target : action.targets) {
                const size_t unit = size_t(cr3Rules.fighters[size_t(target.target)].unit);
                if (target.coveredFrom >= 0 && !timing.steal) { // (a cover: BtlShotCoverDirection)
                    startCr3Cover(target, timing.projectile.duration);
                    launchCr3Projectile(source, size_t(cr3Rules.fighters[size_t(target.coveredFrom)].unit), start);
                } else if (!timing.steal) launchCr3Projectile(source, unit, start);
                else if (!target.stealBack) launchCr3Projectile(unit, source, start);
            }
            continue;
        }
        const auto spawnImpact = [&](const cr3::Vec3& at, float scale) {
            if (timing.sizeSpread > 0) scale *= 1 + (SDL_randf() * 2 - 1) * timing.sizeSpread;
            if (Cr3Fx* fx = spawnCr3Effect(timing.hitEffect, at, scale, start)) {
                if (timing.placement != Placement::Target) fx->yaw = cr3Battle.units[source].yaw;
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: impact effect %s", cr3BattleClock, fx->figure.c_str());
                if (timing.waitEffect) play.end = std::max(play.end, cue.time + fx->animLength);
            }
        };
        if (timing.placement == Placement::Target) {
            for (const auto& target : action.targets) {
                const size_t unit = size_t(cr3Rules.fighters[size_t(target.target)].unit);
                const auto& model = cr3Battle.units[unit];
                spawnImpact(cr3EffectPoint(unit, timing.targetPoint), timing.scaleToTarget ? model.scale * model.effectScale : 1);
            }
        } else if (timing.placement == Placement::Group && action.target >= 0) {
            const bool side = cr3Rules.fighters[size_t(action.target)].player;
            float z = 0;
            int count = 0;
            for (size_t k = 0; k < cr3Battle.units.size(); ++k)
                if (cr3Battle.units[k].player == side) z += cr3EffectPoint(k, 5)[2], ++count;
            spawnImpact({0, 0, count ? z / count : 0}, 1);
        } else {
            spawnImpact({0, 0, 0}, 1);
        }
    }
}

std::array<float, 3> Viewer::unitPoint(size_t k, const char* joint, float up) {
    const cr3::Cr3BattleUnit& bu = cr3Battle.units[k];
    ViewFigure& f = cr3BattleUnits[k].body;
    std::vector<uint8_t> d;
    if (f.skinned && archive.read(bu.figure + ".name", d)) {
        const std::vector<std::string> names = cr3::parseJointNames(d);
        const auto it = std::find(names.begin(), names.end(), joint);
        const std::vector<cr3::JointTransform>& p = f.posed.empty() ? f.skeleton.basePose : f.posed;
        const std::vector<cr3::Mat34> w = f.skeleton.world(p, f.place);
        const size_t j = size_t(it - names.begin());
        if (it != names.end() && j < w.size()) return {w[j].m[3], w[j].m[7], w[j].m[11]};
    }
    return {bu.home[0], bu.home[1] + up, bu.home[2]};
}

int Viewer::cr3FighterOfUnit(size_t k) const {
    for (size_t f = 0; f < cr3Rules.fighters.size(); ++f)
        if (size_t(cr3Rules.fighters[f].unit) == k) return int(f);
    return -1;
}

namespace {

// (BtlStatus vfunc +0x150: a player's character 1 .. 5; base + it, else category 0 - no row, Play a no-op)
int cr3CommentChar(const cr3::Cr3BattleRules& r, int fighter, int base) {
    if (fighter < 0 || size_t(fighter) >= r.fighters.size()) return 0;
    const cr3::Cr3Fighter& f = r.fighters[size_t(fighter)];
    return f.player && f.id >= 1 && f.id <= 5 ? base + f.id : 0;
}

// (a command's row in a CommandInfo side table - MbGetLastAttackInfoByCommandId: LastAttack's +4 the command)
bool cr3CommandInTable(const cr3::MbeFile& m, const char* table, const char* format, int command) {
    const cr3::MbeFile::Table* t = m.table(table);
    if (!t) return false;
    cr3::MbeFile::Row r;
    for (size_t k = 0; k < t->rows.size(); ++k)
        if (m.row(*t, k, format, r) && r.i(1) == command) return true;
    return false;
}

} // namespace

// PlayGetSetByTurn 0x9a8610 (BtlSysStateSelect::phaseStart 0x9d35d4, count++ and the get-set's camera - after any opening):
// the first get-set 0x86 for the second battle's ghost, 0x87 the third's; later X - 7 to the 4th, 8 to the 7th, else 9 - or 10
// at 50 / 50, 0.3 s on, and with 10 also 0xb / 0xc / 0xd at once. Nothing cut
void Viewer::commentCr3GetSet() {
    if (cr3Turn == vsGetSetTurn) return; // (a get-set again in the same turn - the escape's no, the opening's end)
    vsGetSetTurn = cr3Turn, ++vsGetSets;
    if (!vsCommentary.on) return;
    const int n = vsGetSets;
    if (n == 1) {
        if (vs.battle == 1) requestVsComment(0x86, 0.0f);
        else if (vs.battle == 2) requestVsComment(0x87, 0.0f);
        return;
    }
    const bool ten = SDL_rand(2) != 0;
    requestVsComment(ten ? 10 : n <= 4 ? 7 : n <= 7 ? 8 : 9, 0.3f);
    if (ten) requestVsComment(n <= 4 ? 0xb : n <= 7 ? 0xc : 0xd, 0.0f);
}

// PlayBattleStartByTurn 0x9a85b4 (BtlSysStateSelect::phaseCalculate 0x9d490c, the commands chosen): the first get-set's
// alone, and only to cut a voice still speaking or queued off - cut 6
void Viewer::commentCr3TurnStart() {
    if (vsGetSets == 1 && vsCommentaryBusy()) cutInVsComment(6);
}

// PlayCooperation 0x9a8788 (BtlDamageChunk::PlayCooperation 0x97a69c, its first partner's): a player's alone - three units
// 0x47; two 0x46 or the pair's own (MbGetCooperationCommentCategory 0xf43df4, either order) at 50 / 50. Cut
void Viewer::commentCr3Cooperation(const cr3::Cr3Action& a) {
    if (!vsCommentary.on || a.source < 0 || !cr3Rules.fighters[size_t(a.source)].player || a.partners.empty() || a.partners.size() > 2) return;
    if (a.partners.size() == 2) return cutInVsComment(0x47);
    int x = cr3CommentChar(cr3Rules, a.source, 0), y = cr3CommentChar(cr3Rules, a.partners.back(), 0);
    if (x > y) std::swap(x, y);
    static constexpr int kPair[6][6] = {{0}, {0, 0, 0x48, 0x49, 0x4a, 0x4b}, {0, 0, 0, 0x4c, 0x4d, 0x4e}, {0, 0, 0, 0, 0x4f, 0x50}, {0, 0, 0, 0, 0, 0x51}, {0}};
    const int pair = x >= 1 && y <= 5 ? kPair[x][y] : 0;
    cutInVsComment(SDL_rand(2) ? 0x46 : pair);
}

// PlayCommandHit 0x9a9228 (BtlAnnounceDirection::vPlay 0x99066c: the announcement CreateDirection 0x990d98 puts first for
// action types 1, 2, 3, 5 .. 8, 0xf, 0x16, 0x17, 0x19, 0x20 .. 0x24, 0x26): a gene command's - +0xbc bit 0 - own category
// (MbGetPlayCommandCommentCategoryByCommandId), else a player's 0x51 + its character. Cut
void Viewer::commentCr3CommandHit(const cr3::Cr3Action& a, int kind) {
    static constexpr int kKinds[] = {1, 2, 3, 5, 6, 7, 8, 15, 22, 23, 25, 32, 33, 34, 35, 36, 38};
    if (!vsCommentary.on || a.source < 0 || a.command < 0 || std::find(std::begin(kKinds), std::end(kKinds), kind) == std::end(kKinds)) return;
    cr3::Cr3Command c;
    if (!cr3Rules.command(a.command, c) || !(c.flags & 1)) return;
    const int id = a.command;
    int cat = id == 0x130 ? 0x57 : id == 0x147 ? 0x6b : id == 0x158 ? 0x58 : id >= 0x169 && id <= 0x16e ? 0x59 : id == 0x181 ? 0x5a
            : id == 0x183 ? 0x5b : id == 0x184 ? 0x5c : id == 0x185 ? 0x5d : id == 0x186 || id == 0x187 ? 0x5e : id == 0x18a ? 0x5f
            : id == 0x18d ? 0x61 : id == 0x18e ? 0x62 : id == 0x38 || id == 0x39 ? 0x60 : 0;
    if (cat == 0 && cr3Rules.fighters[size_t(a.source)].player) cat = cr3CommentChar(cr3Rules, a.source, 0x51);
    cutInVsComment(cat);
}

// BtlDamageTargetList::PlayAfterDamageVoice 0x9807b8 (BtlDamageChunk::VUpdate case 4 0x97a1b0: every target list announced),
// on the last list - here the action's one: its last target T. No miss on T (IsDamage 0x98951c): a landed hit - PlayAfterDamage
// 0x988b48 -, else PlayAfterNoDamage (an enemy T 0x10, a player struck by an enemy 0x11). Then every target's given effects
// (0x989568 -> 0x9a9608): buffs on its own side, debuffs on the other's
void Viewer::commentCr3AfterDamage(const cr3::Cr3Action& a) {
    if (!vsCommentary.on) return;
    const cr3::Cr3Hit* last = nullptr;
    for (const cr3::Cr3Hit& th : a.targets)
        if (!th.stealBack) last = &th;
    if (!last || last->target < 0) return;
    const auto isPlayer = [&](int f) { return f >= 0 && size_t(f) < cr3Rules.fighters.size() && cr3Rules.fighters[size_t(f)].player; };
    const cr3::Cr3Hit& T = *last;
    const bool tPlayer = isPlayer(T.target), srcEnemy = a.source >= 0 && !isPlayer(a.source);
    bool miss = false, landed = false;
    for (uint8_t fl : T.flags) miss |= (fl & 4) != 0, landed |= !(fl & 4); // (+0x5c a miss; +0x5d landed - a blocked hit too)
    if (!miss && !landed) { // (PlayAfterNoDamage 0x9a8938)
        if (!tPlayer) cutInVsComment(0x10);
        else if (srcEnemy) cutInVsComment(0x11);
    } else if (!miss && a.command != 0x37) { // (PlayAfterDamage: ToPlayer 0x9a89a8 / ToEnemy 0x9a8da0)
        const bool finest = cr3CommandInTable(cr3BattleTables.command, "FinestArts", "iiii", a.command);
        if (!(finest && !tPlayer)) { // (ToEnemy: none for a Finest Arts - its queued 0x76 line kept)
            // (HasVictoryToDirection / HasDefeatToDirection: a side all fallen once this action's HP is shown)
            bool party = false, foes = false;
            for (size_t f = 0; f < cr3Rules.fighters.size(); ++f) {
                int hp = f < cr3HpShown.size() ? cr3HpShown[f] : cr3Rules.fighters[f].hp;
                for (const cr3::Cr3Hit& th : a.targets)
                    if (size_t(th.target) == f) hp = th.hpAfter;
                if (hp > 0) (cr3Rules.fighters[f].player ? party : foes) = true;
            }
            const uint8_t first = T.flags.empty() ? 0 : T.flags.front();
            const int color = T.colors.empty() ? 0 : int(T.colors.front());
            const bool lastAttack = cr3CommandInTable(cr3BattleTables.command, "LastAttack", "iiiii", a.command);
            int cat = 0;
            bool matched = true;
            // (the last hit's +0x76 / +0x75 rows - 0x13 / 0x12, 0x3e / 0x38 .. 0x1d / 0x1c - left out: their writer is untraced,
            // taken as false; chunk +0xa4 - 0x17 / 0x16 - and HasEffectToRemove(0x4c) - 0x21 / 0x20 - likewise)
            if (!party || !foes) cat = tPlayer ? 0xf : 0xe;
            else if (a.broke) cat = tPlayer ? 0x45 : 0x44;
            else if (lastAttack) cat = tPlayer ? 0x25 : 0x24;
            else if (a.combo) cat = tPlayer ? 0x23 : 0x22;
            else if (first & 1) cat = tPlayer ? 0x15 : 0x14;
            else if (first & 8) cat = tPlayer ? 0x19 : 0x18;
            else if (T.reflectedFrom >= 0) cat = tPlayer ? 0x1b : 0x1a;
            else if (color == 3 || color == 4) cat = tPlayer ? 0x1f : 0x1e;
            else if (color == 1) cat = tPlayer ? 0x28 : 0x26;
            else if (color == 2) cat = tPlayer ? 0x29 : 0x27;
            else if (T.hpAfter > 0) {
                const cr3::Cr3Fighter& tf = cr3Rules.fighters[size_t(T.target)];
                const bool high = tf.maxHp > 0 && float(T.hpAfter) / float(tf.maxHp) >= 0.5f;
                if (SDL_rand(2)) cat = tPlayer ? cr3CommentChar(cr3Rules, T.target, 0x32) : cr3CommentChar(cr3Rules, a.source, 0x2b);
                else cat = tPlayer ? (high ? 0x31 : 0x32) : (high ? 0x2a : 0x2b);
            } else
                matched = false;
            if (matched) cutInVsComment(cat); // (the first row that holds: cut - category 0 a cut with no line)
        }
    }
    // (the non-damage line, 0x9a9608: a buff's on its own side - MbGetBufferCommentCategory -, a debuff's on the other -
    // the table at 0xf43e84 -; two or more of that family held: 0x67 / 0x70, cut at once; else the collected shuffled, the first cut)
    const auto buff = [](int e) { return e >= 3 && e <= 6 ? 0x60 + e : e == 0x11 ? 0x68 : e == 0x12 ? 0x69 : e == 0x17 || e == 0x18 ? 0x6a : e == 0x1b ? 0x6c : 0; };
    const auto debuff = [](int e) { return e >= 7 && e <= 10 ? 0x6a + e : e >= 13 && e <= 15 ? 0x60 + e : 0; };
    std::vector<int> picks;
    for (const cr3::Cr3Hit& th : a.targets) {
        if (th.stealBack || th.effectsGiven.empty() || th.target < 0) continue;
        const bool same = a.source < 0 ? isPlayer(th.target) : isPlayer(a.source) == isPlayer(th.target);
        const auto& map = same ? buff : debuff;
        std::vector<int> got;
        for (int e : th.effectsGiven)
            if (const int c = (same ? buff(e) : debuff(e)); c != 0) got.push_back(c);
        if (got.empty()) continue;
        int held = 0;
        if (size_t(th.target) < a.effectsAfter.size())
            for (int e : a.effectsAfter[size_t(th.target)]) held += e >= 0 && e <= 255 && map(e) != 0;
        if (held >= 2) return cutInVsComment(same ? 0x67 : 0x70);
        picks.push_back(got[size_t(SDL_rand(int32_t(got.size())))]);
    }
    if (!picks.empty()) cutInVsComment(picks[size_t(SDL_rand(int32_t(picks.size())))]);
}

void Viewer::beginCr3Turn(bool countTurn) {
    cr3TurnEndStep = 0, cr3DefeatPlayed = false; // (a turn's end to come)
    if (countTurn) ++cr3Turn; // (BtlSysStateSelect::phaseStart)
    if (countTurn) cr3Rules.giveFieldEffects(); // (phaseStart: GiveFieldStatusEffect - the menu's icons; the rules' turn again, the same)
    // (phaseStart's opening - isOpening, the first turn alone; none after a surprise attack or a script's start command, nor
    // for a new wave or a summon - BtlSystem +0x140 is set by its constructor alone, CreateMonster leaves it: a wave's rules
    // start at turnNo 0 with cr3Turn above 1 -: the opening skills' commands, BtlOpeningDirection played first - stepCr3Opening)
    if (countTurn && cr3Turn == 1 && cr3Rules.turnNo == 0 && !cr3Opening.on) {
        freezeCr3Shown();
        if (startCr3Opening(cr3Rules.opening())) return;
    }
    cr3Choices.clear(), cr3TurnItems.clear();
    closeCr3Totals();
    cr3EffectsShown.clear(), cr3ElementShown.clear(); // (the rules' own while choosing)
    cr3Chooser = -1;
    cr3BattleStage = Cr3BattleStage::Menu, cr3StageStart = cr3MenuStart = cr3BattleClock, cr3Focus = 0, cr3TabLit = 0;
    playCr3Camera(cr3Battle.cameras[0], cr3Battle.getSetEnd, false, {0, 0, 0}, 0, 0);
    cr3Cam.held = true;
    if (!cr3GetSetSeen) audio.playSe("bt_001", false), cr3GetSetSeen = true;
    SDL_Log("battle %.2f s: turn %d, the get-set", cr3BattleClock, cr3Turn);
    commentCr3GetSet();
    // (with the Auto Battle / Mass Attack on - phaseGetSetStartOut 0x9d3bf0, HasAutoResult: no touch
    // waited for, GetGetSetResult its 5 or 6 again - the turn is chosen alone once no script runs:
    // updateCr3Battle)
}

// (phaseGetSetStartOut: EntryGetSet(!(Coupling +0x44 & 2) && !game flag 5064) - battle_2001 .. 2006's)
bool Viewer::cr3EscapeAllowed() const { return (cr3Battle.couplingFlags & 2) == 0 && !game.flag(5064); }

bool Viewer::cr3TabsShown() const { return !game.flag(5060); }

// a turn of the Mass Attack / Auto Battle (phaseAllAttack 0x9d3e2c / phaseAutomatic 0x9d48c8: the
// party's picks, then phaseCalculate - the monsters' own as ever)
void Viewer::beginCr3AutoTurn() {
    cr3Choices = cr3AutoResult == 5 ? cr3Rules.massAttackChoices() : cr3Rules.autoChoices(true);
    std::string picks;
    for (const auto& c : cr3Choices) picks += " " + std::to_string(c[0]) + ":" + std::to_string(c[1]) + ">" + std::to_string(c[2]);
    SDL_Log("battle %.2f s: %s - turn %d:%s", cr3BattleClock, cr3AutoResult == 5 ? "Mass Attack" : "Auto Battle", cr3Turn, picks.c_str());
    playCr3Choices();
}

void Viewer::stopCr3Auto(const char* why) {
    if (cr3AutoResult == 0) return;
    cr3AutoResult = 0, cr3MarkerPressed = false, cr3AutoTaps.clear(); // (StopAuto: +0xd4 0, CloseComButtonList)
    SDL_Log("battle: auto off (%s)", why);
}

// the get-set menu's choice: 0 Battle, 1 Escape, 2 Options; 5 Mass Attack, 6 Auto Battle (its tabs -
// phaseGetSetSelect 0x9d3cbc: SetAutoResult, then the first time's tutorial - phaseAllAttackEvent
// 0x9d3ddc / phaseAutomaticEvent 0x9d4878 -> BtlScriptSystem::CallTutorial: Battle_Tuto_061 / 062,
// shown once - flag 61 / 62 -, none under flag 5056 -, the turn once it is over)
void Viewer::chooseCr3GetSet(int b) {
    if (cr3AutoResult != 0) return; // (EntryGetSet: none while it runs)
    if (b == 1 && !cr3EscapeAllowed()) return (void)audio.playSe("com_005", false);
    if ((b == 5 || b == 6) && cr3TabsShown()) {
        audio.playSe("com_003", false); // (comBtnMenu::TouchSimpleRelease: a tab's, as Battle's)
        cr3AutoResult = b, cr3TabLit = 0;
        SDL_Log("battle: %s on", b == 5 ? "Mass Attack" : "Auto Battle");
        runCr3BattleScript(b == 5 ? "Battle_Tuto_061" : "Battle_Tuto_062"); // (its window first: updateCr3Battle waits for it)
        return;
    }
    if (b == 2) return openCr3OptionsFrom(2); // (the Options: CR3-U's - the camp's Options screen over the battle)
    if (b == 0) {
        audio.playSe(0x153, false);
        nextCr3Chooser();
    } else if (b == 1 && cr3EscapeAllowed()) {
        // (BtlSystem::Escape: sure with the gauge's advantage, else a random byte under 240 -
        // the party's level below the monsters' -, 244, or 252 twenty levels above; failed,
        // the monsters act - phaseEscapeFailure)
        audio.playSe(0x153, false);
        int partyLevel = 1, monsterLevel = 1;
        for (const cr3::Cr3Fighter& f : cr3Rules.fighters) (f.player ? partyLevel : monsterLevel) = std::max(f.player ? partyLevel : monsterLevel, f.level);
        const int diff = partyLevel - monsterLevel;
        cr3EscapeThreshold = diff < 0 ? 0xf0 : diff >= 20 ? 0xfc : 0xf4;
        // (phaseEscapeStart: AnnounceBattle 4 - the question - held, the button list's two -
        // EntrySelect: buttons 10 and 11, yes and no)
        cr3EscapeAsk = true;
        cr3BattleStage = Cr3BattleStage::Escape, cr3StageStart = cr3MenuStart = cr3BattleClock, cr3Focus = 0;
        setCr3Announce(announceText("AnnounceBattle", 4), -1);
    }
}

// phaseEscapeSelect: no - the get-set again (phase 6); yes - BtlSysState::escape: 5 for a second
// and away (phaseEscapeSuccess), else 6 for a second and the monsters' turn (phaseEscapeFailure)
void Viewer::chooseCr3Escape(int b) {
    if (!cr3EscapeAsk) return;
    cr3EscapeAsk = false;
    cr3Announce = Cr3Announce(); // (BtlAnnounceUI::Close)
    if (b != 0) return beginCr3Turn(false);
    audio.playSe(0x153, false);
    if (vs.screen == 5) { // (phaseColosseumEscapeSelect 0x9d4dac: VS - PlayGameOverCamera, PlayRetire 0x9a9434's cut 0x85, result 2)
        cutInVsComment(0x85);
        vs.retired = true;
        SDL_Log("vs: retired from battle %d", vs.battle + 1);
        cr3BattleStage = Cr3BattleStage::Lost, cr3StageStart = cr3BattleClock, cr3BattleLost = true;
        return;
    }
    bool sure = cr3Rules.advantage(true); // (BtlSystem::Escape: break advantage or skill kind 0x56)
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters)
        if (f.player && !f.dead() && !cr3Rules.skillConstants(f, 0x56).empty()) sure = true;
    cr3EscapeWorks = sure || int(SDL_rand(256)) < cr3EscapeThreshold;
    cr3StageStart = cr3BattleClock;
    setCr3Announce(announceText("AnnounceBattle", cr3EscapeWorks ? 5 : 6), 1.0f);
}

void Viewer::stepCr3Escape() {
    if (cr3EscapeAsk) return;
    if (cr3BattleClock - cr3StageStart >= 1.0f) {
        if (cr3EscapeWorks) {
            SDL_Log("battle %d: escaped", cr3Battle.coupling);
            cr3Escaped = true;
            endCr3Battle();
            return;
        }
        SDL_Log("battle %d: the escape failed", cr3Battle.coupling);
        cr3Choices.clear();
        commentCr3TurnStart(), freezeCr3Shown(), cr3Actions = cr3Rules.turn(cr3Choices);
        cr3CountRecords();
        cr3ActionAt = 0;
        cr3BattleStage = Cr3BattleStage::Action, cr3StageStart = cr3BattleClock;
        startCr3Action();
    }
}

std::string Viewer::cr3NameOf(const char* file, const char* table, int id) {
    if (std::strcmp(file, "gene_name.mbe") == 0) {
        const std::string original = cardArtwork.originalName(id, originalTextJapanese);
        if (!original.empty()) return original;
    }
    static std::map<std::string, cr3::MbeFile> files;
    static int readIn = -1; // (the language it was read in: textGeneration)
    if (readIn != textGeneration) files = std::map<std::string, cr3::MbeFile>(), readIn = textGeneration;
    cr3::MbeFile& f = files[file];
    if (!f.table(table)) {
        std::vector<uint8_t> d;
        if (archive.read(std::string("data\\") + file, d)) f.parse(d);
    }
    cr3::MbeFile::Row r;
    if (!f.find(table, id, "ipp", r)) return {};
    return optionLanguage == 0 || r.s(2).empty() ? r.s(1) : r.s(2);
}

bool Viewer::cr3CommandHasTarget(const cr3::Cr3Command& c) const {
    if (c.target == 0) return cr3Rules.targetable(cr3Chooser, &c);
    const bool own = c.target == 1 || c.target == 2 || c.target == 10 || c.target == 11 || c.target == 12 || c.target == 13 ||
                     ((c.target == 8 || c.target == 9) && (c.ally() || c.revives()));
    const bool canSwitch = c.target == 5 || c.target == 7 || c.target == 8 || c.target == 9;
    for (size_t f = 0; f < cr3Rules.fighters.size(); ++f) {
        const bool sideOk = canSwitch || cr3Rules.fighters[f].player == own;
        if (sideOk && cr3Rules.targetable(int(f), &c) && ((c.target != 12 && c.target != 13) || int(f) != cr3Chooser)) return true;
    }
    return false;
}

void Viewer::openCr3List(bool items, bool keepCategory) {
    cr3List.clear();
    cr3ListItems = items;
    if (!keepCategory) cr3ItemCategory = 0;
    const cr3::Cr3Fighter& f = cr3Rules.fighters[size_t(cr3Chooser)];
    if (!items) {
        for (int id : cr3Rules.skillCommands(cr3Chooser)) { // (its skills' commands first: kind 127)
            cr3::Cr3Command c;
            if (!cr3Rules.command(id, c)) continue;
            Cr3ListRow row;
            row.command = id, row.name = cr3NameOf("command_name.mbe", "CommandName", id), row.cost = cr3Rules.mpCost(f, c), row.icon = c.icon;
            const int hCost = cr3Rules.hpCost(f, c);
            row.usable = row.cost <= f.mp && (hCost == 0 || hCost < f.hp) && cr3CommandHasTarget(c);
            cr3List.push_back(row);
        }
        for (int id : f.geneCommands) {
            cr3::Cr3Command c;
            if (!cr3Rules.command(id, c) || ((c.type < 1 || c.type > 4) && c.direction != 0x16)) continue; // (Analyze - 393 - is type 7, listed)
            Cr3ListRow row;
            row.command = id, row.name = cr3NameOf("command_name.mbe", "CommandName", id), row.cost = cr3Rules.mpCost(f, c), row.icon = c.icon;
            const int hCost = cr3Rules.hpCost(f, c);
            row.usable = c.usable() && row.cost <= f.mp && (hCost == 0 || hCost < f.hp) && cr3CommandHasTarget(c);
            cr3List.push_back(row);
        }
    } else {
        for (const auto& [id, n] : game.cr3Items) {
            if (id <= 0 || n <= 0 || (cr3ItemCategory == 0 ? id >= 100000 : id / 100000 != 6)) continue;
            cr3::MbeFile::Row r;
            // (item_para's +0x30 its battle command - Chocolate by J's 526, a Plum's 511; -1 none:
            // a field item, the Microcommunicator, not listed. +0x0c is no command)
            if (!cr3BattleTables.item.find(cr3ItemCategory == 0 ? "Item" : "Gift", id % 100000,
                                           cr3ItemCategory == 0 ? "iiiiiiiiiiiii" : "iiiiiiiiiiiiiiiiii", r)) continue;
            const int command = r.i(cr3ItemCategory == 0 ? 12 : 15);
            if (command < 0) continue;
            Cr3ListRow row;
            row.item = id, row.command = command, row.count = n, row.name = cr3ItemText(id, false), row.icon = r.i(3);
            cr3::Cr3Command c;
            row.usable = cr3Rules.command(row.command, c) && c.usable() && cr3CommandHasTarget(c);
            cr3List.push_back(row);
        }
    }
    {
        std::string ids;
        for (const Cr3ListRow& row : cr3List) ids += " " + std::to_string(row.command) + (row.usable ? "" : "(-)");
        SDL_Log("battle %.2f s: fighter %d's %s:%s", cr3BattleClock, cr3Chooser, items ? "items" : "genes", ids.c_str());
    }
    cr3BattleStage = Cr3BattleStage::List, cr3StageStart = cr3MenuStart = cr3BattleClock, cr3Focus = 0;
}

bool Viewer::battleShoulder(bool right) {
    if (!vitaButtons() || !cr3BattleMode || cr3BattleStage != Cr3BattleStage::List || !cr3ListItems) return false;
    cr3ItemCategory = (cr3ItemCategory + (right ? 1 : -1) + 2) % 2;
    openCr3List(true, true);
    audio.playSe("com_002", false);
    SDL_Log("battle: item category %d", cr3ItemCategory);
    return true;
}

void Viewer::chooseCr3ListRow(int k) {
    if (k < 0 || size_t(k) >= cr3List.size() || !cr3List[size_t(k)].usable) {
        audio.playSe("com_005", false);
        SDL_Log("battle: unavailable list row %d", k);
        return;
    }
    const Cr3ListRow& row = cr3List[size_t(k)];
    audio.playSe(0x153, false);
    cr3ChosenCommand = row.command, cr3ChosenItem = row.item;
    cr3::Cr3Command c;
    cr3Rules.command(row.command, c);
    if (c.target == 0) { // (BtlSysStateSelect::VUpdate: only self-target 0 skips OpenTargetSelect)
        if (row.item) game.cr3Items[row.item] = std::max(game.cr3Items[row.item] - 1, 0), cr3TurnItems[cr3Chooser] = row.item;
        cr3Choices.push_back({cr3Chooser, row.command, cr3Chooser});
        nextCr3Chooser();
    } else {
        cr3BattleStage = Cr3BattleStage::Target, cr3StageStart = cr3MenuStart = cr3BattleClock, cr3Focus = 0, cr3TargetChanged = false;
        if (cr3Targets().empty() && cr3TargetCanChange()) cr3TargetChanged = true;
    }
}

void Viewer::nextCr3Chooser() {
    // (BtlStatusList::InitializeMenu: a unit with a kind 31 status effect - held, asleep - has none)
    for (size_t f = size_t(cr3Chooser + 1); f < cr3Rules.fighters.size(); ++f)
        if (cr3Rules.fighters[f].player && !cr3Rules.fighters[f].dead() && !cr3Rules.hasKind(cr3Rules.fighters[f], 31)) {
            cr3Chooser = int(f);
            cr3BattleStage = Cr3BattleStage::Command, cr3StageStart = cr3MenuStart = cr3BattleClock, cr3Focus = 0;
            SDL_Log("battle %.2f s: fighter %zu chooses", cr3BattleClock, f);
            const size_t k = size_t(cr3Rules.fighters[f].unit);
            playCr3Camera("Cam_bt_command_PC_00", 26.6667f, true, unitPoint(k, "DummyCP", 1.2f), cr3Battle.units[k].yaw, -1.0f / 3.0f);
            return;
        }
    playCr3Choices();
}

void Viewer::playCr3Choices() {
    // (all chosen: BtlSysStateSelect::phaseCalculate - BtlStatusList::CallMonsterScript, each
    // living monster's Battle_Monster_NNNN where its script has one - its BattleCommand.Add in
    // place of its own pick -, then the turn worked out and played)
    for (size_t f = 0; f < cr3Rules.fighters.size(); ++f) {
        const cr3::Cr3Fighter& m = cr3Rules.fighters[f];
        if (m.player || m.dead()) continue;
        char fn[32];
        std::snprintf(fn, sizeof fn, "Battle_Monster_%04d", m.id);
        cr3ArgSource = int(f), cr3ArgAction = -1;
        if (runCr3BattleScript(fn, true))
            for (int k = 0; k < 600 && cr3BattleEvents->running(); ++k) cr3BattleEvents->tick();
    }
    cr3ArgSource = -1;
    commentCr3TurnStart(), freezeCr3Shown(), cr3Actions = cr3Rules.turn(cr3Choices);
    cr3CountRecords();
    cr3ActionAt = 0;
    cr3BattleStage = Cr3BattleStage::Action, cr3StageStart = cr3BattleClock;
    startCr3Action();
}

void Viewer::chooseCr3Command(int button) {
    if (cr3Chooser < 0 || !cr3Rules.menuEnabled(cr3Chooser, button)) {
        audio.playSe("com_005", false);
        SDL_Log("battle: unavailable command %d", button);
        return;
    }
    audio.playSe(0x153, false);
    if (button == 3) { // (Guard)
        cr3Choices.push_back({cr3Chooser, -1, -1});
        nextCr3Chooser();
    } else if (button == 0) { // (Attack: its target next)
        cr3ChosenCommand = cr3Rules.attackCommandOf(cr3Chooser), cr3ChosenItem = 0; // (a transformation's: 8 / 9)
        cr3BattleStage = Cr3BattleStage::Target, cr3StageStart = cr3MenuStart = cr3BattleClock, cr3Focus = 0, cr3TargetChanged = false;
    } else if (button == 1 || button == 2) { // (Gene: its commands; Item)
        openCr3List(button == 2);
    }
}

std::vector<int> Viewer::cr3LivingMonsters() const {
    std::vector<int> out;
    for (size_t f = 0; f < cr3Rules.fighters.size(); ++f)
        if (!cr3Rules.fighters[f].player && cr3Rules.targetable(int(f))) out.push_back(int(f));
    return out;
}

bool Viewer::cr3TargetCanChange() {
    cr3::Cr3Command c;
    return cr3Rules.command(cr3ChosenCommand, c) && (c.target == 5 || c.target == 7 || c.target == 8 || c.target == 9);
}

std::vector<int> Viewer::cr3Targets() {
    cr3::Cr3Command c;
    cr3Rules.command(cr3ChosenCommand, c);
    const bool own = c.target == 1 || c.target == 2 || c.target == 10 || c.target == 11 || c.target == 12 || c.target == 13 ||
                     ((c.target == 8 || c.target == 9) && (c.ally() || c.revives()));
    std::vector<int> targets;
    for (size_t f = 0; f < cr3Rules.fighters.size(); ++f)
        if (cr3Rules.fighters[f].player == (own != cr3TargetChanged) && cr3Rules.targetable(int(f), &c) &&
            ((c.target != 12 && c.target != 13) || int(f) != cr3Chooser)) targets.push_back(int(f));
    return targets;
}

void Viewer::chooseCr3Target(int n) {
    const std::vector<int> alive = cr3Targets();
    if (alive.empty() || cr3Chooser < 0) return;
    audio.playSe(0x153, false);
    const int target = alive[size_t(std::clamp(n, 0, int(alive.size()) - 1))];
    if (cr3ChosenItem) game.cr3Items[cr3ChosenItem] = std::max(game.cr3Items[cr3ChosenItem] - 1, 0), cr3TurnItems[cr3Chooser] = cr3ChosenItem;
    cr3Choices.push_back({cr3Chooser, cr3ChosenCommand >= 0 ? cr3ChosenCommand : cr3Rules.attackCommandOf(cr3Chooser), target});
    nextCr3Chooser();
}

void Viewer::playCr3AttackCamera(const cr3::Cr3Action& action, size_t source) {
    if (action.target < 0) return;
    const auto& actor = cr3Battle.units[source];
    const size_t target = size_t(cr3Rules.fighters[size_t(action.target)].unit);
    const auto& foe = cr3Battle.units[target];
    const int size = actor.player ? foe.cameraSize : actor.cameraSize;
    std::string name = cr3Battle.cameras[actor.player ? 2 : 3];
    cr3::MbeFile::Row row;
    if (cr3BattleTables.model.find("Camera", size, "ifpppppppppp", row)) name = row.s(actor.player ? 4 : 5);
    const cr3::Vec3 at{foe.home[0], 0, actor.player ? 0.0f : 2.15f};
    playCr3Camera(name, cr3ActionPlay.end, false, at, 0, 0);
    cr3Cam.lookAt = unitPoint(target, "DummyCP", 1.2f);
    cr3Cam.lookWeight = actor.player ? 0.2f : 0.4f;
}

// BtlCameraSystem::PlayGlobal 0x977d4c / PlayGlobalToTarget 0x97811c -> FUN_00977e7c: a random entry of the
// Camera table's attack_ALL list (cameras[4], the battle's camera size) but the last clip either played
// (+0xff8) - the one entry where it is all there is
std::string Viewer::pickCr3GlobalCamera() {
    std::vector<std::string> all, pool;
    for (size_t at = 0; at <= cr3Battle.cameras[4].size();) {
        const size_t end = std::min(cr3Battle.cameras[4].find('\n', at), cr3Battle.cameras[4].size());
        if (end > at) all.push_back(cr3Battle.cameras[4].substr(at, end - at));
        at = end + 1;
    }
    for (const std::string& n : all)
        if (n != cr3LastGlobalCam) pool.push_back(n);
    if (pool.empty()) pool = all;
    if (pool.empty()) return std::string();
    cr3LastGlobalCam = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(cr3CamRng)];
    return cr3LastGlobalCam;
}

// BtlUnit::PlayCamera 0x9dd234 (from BtlUnitList::PlayAnimation): the motion's AnimDirection camera (+0x08),
// where the row has one, over whatever plays - posed on the unit at its home, its home turn, its scale
void Viewer::playCr3MotionCamera(size_t source, int motion) {
    const std::string camera = cr3BattleTables.unitCamera(cr3Battle.units[source].model, motion);
    if (camera.empty()) return;
    playCr3CasterCamera(camera, source);
    if (cr3Cam.name == camera) SDL_Log("battle %.2f s: motion %d's camera %s", cr3BattleClock, motion, camera.c_str());
}

// BtlRangedDirection::vPlay 0x98d9b8 / BtlShotDirection::vPlay 0x99a540 (CommandDirection kinds 2, 3):
// playCameraByType first - BtlDamageChunk::PlayCameraByType 0x97c2cc by the row's +0x0c (-1: the attack
// command's): 1 PlayRangedCamera 0x9ddcf8 and 4 PlayGlobalCamera, both PlayGlobalToTarget 0x97811c - an
// attack_ALL clip at the origin turned 0.4 toward the target -, 0 the melee's -, then PlayAnimation(0xf) and
// its PlayCamera(15) over it (the one s01 clip: Cam_s01_m316_00). No white (their unit observers take
// states 1, 2, 6 only) and the end the motion's, not the clip's. (A combo's PlayRangedComboCamera 0x9ddd1c
// not in.) A Shot whose chunk IsCover (BtlDamageChunk::IsCover 0x97e7f0 / BtlDamageTargetList::IsCover 0x982028: any
// target's +0x14 not its original +0x18 - a coverer took it) is a BtlShotCoverDirection (CreateDirection 0x990d98 kind 3:
// sequence 5, not 4): its vPlay 0x99ae54 has no playCameraByType - PlayAnimation(0xf), then PlayGlobal(5.0) over the
// motion's camera: a global clip at the origin, its end at 5 s (BtlCameraMotion::Load's end time)
bool Viewer::startCr3RangedCamera(const cr3::Cr3Action& a, size_t su) {
    cr3::MbeFile::Row dir;
    if (!cr3BattleTables.direction.find("CommandDirection", cr3ActionPlay.directionCommand, "iiii", dir)) return false;
    if (dir.i(1) != 2 && dir.i(1) != 3) return false;
    if (dir.i(1) == 3 && std::any_of(a.targets.begin(), a.targets.end(), [](const cr3::Cr3Hit& h) { return h.coveredFrom >= 0; })) {
        cr3ActionPlay.ranged = true;
        playCr3Camera(pickCr3GlobalCamera(), 5.0f, false, {}, 0, 0);
        SDL_Log("battle %.2f s: a cover shot - PlayGlobal(5.0) %s", cr3BattleClock, cr3Cam.name.c_str());
        return true;
    }
    int type = dir.i(3);
    if (cr3::MbeFile::Row attack; type < 0 &&
        cr3BattleTables.direction.find("CommandDirection", cr3Rules.fighters[size_t(a.source)].attackCommand, "iiii", attack))
        type = attack.i(3);
    if ((type == 1 || type == 4) && a.target >= 0) {
        const size_t target = size_t(cr3Rules.fighters[size_t(a.target)].unit);
        playCr3Camera(pickCr3GlobalCamera(), cr3BattleCam.duration(), false, {}, 0, 0);
        if (cr3Battle.cameraSize != 0) cr3Cam.lookAt = unitPoint(target, "DummyCP", 1.2f), cr3Cam.lookWeight = 0.4f;
    } else {
        playCr3AttackCamera(a, su);
    }
    SDL_Log("battle %.2f s: a ranged direction (kind %d, camera type %d) - %s", cr3BattleClock, dir.i(1), type, cr3Cam.name.c_str());
    cr3ActionPlay.ranged = true;
    playCr3MotionCamera(su, 15);
    return true;
}

void Viewer::playCr3CasterCamera(const std::string& name, size_t source, const cr3::Vec3* at) {
    if (name.empty()) return;
    cr3::Cr3BattleCamera camera;
    if (!camera.load(archive, name) || !camera.has(name)) return;
    const auto& caster = cr3Battle.units[source];
    cr3Cam = Cr3BattleCamPlay();
    cr3Cam.name = name;
    cr3Cam.start = cr3BattleClock;
    cr3Cam.end = camera.duration();
    cr3Cam.actor = at ? *at : caster.home;
    cr3Cam.actorYaw = caster.yaw;
    cr3Cam.scale = caster.scale;
    cr3BattleOwnCam = std::move(camera);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: caster camera %s", cr3BattleClock, name.c_str());
}

bool Viewer::startCr3LastAttack(const cr3::Cr3Action& a, size_t su) {
    cr3::MbeFile::Row dir;
    if (!cr3BattleTables.direction.find("CommandDirection", cr3ActionPlay.directionCommand, "iiiifii_piii", dir)) return false;
    const int kind = dir.i(1), ext = dir.i(2), effectPoint = dir.i(10);
    if (kind != 9 && kind != 10 && kind != 20 && kind != 30 && kind != 39 && kind != 11 && kind != 12) return false;
    const bool finestArts = kind == 11 || kind == 12;
    if (a.target < 0 || a.targets.empty()) return false;
    const cr3::Cr3BattleUnit& bu = cr3Battle.units[su];
    float white = -1; // (AnimDirection: model, motion, camera +0x08, sound, .., white-fade frame +0x24)
    const std::string camera = cr3BattleTables.unitCamera(bu.model, ext, &white);
    const size_t tu = size_t(cr3Rules.fighters[size_t(a.targets.front().target)].unit);
    const cr3::Cr3BattleUnit& foe = cr3Battle.units[tu];
    Cr3BattleUnitView& view = cr3BattleUnits[su];
    // (the position: ChangePositionToTargetEffectPoint - the target's effect point - where the row
    // gives one, a ShotLastAttack's and the rest at its front - ChangeTargetPositionToFront -, at once;
    // ChangeRotationToHome: no turn)
    cr3::Vec3 at;
    if (kind == 12 && effectPoint < 0) at = {0, bu.home[1], 0}; // (BtlUnit::ChangePositionToCenter: the world's origin)
    else if (effectPoint >= 0 && kind != 20 && kind != 10 && kind != 11) at = cr3EffectPoint(tu, effectPoint), at[1] = bu.home[1];
    else {
        const float dx = foe.home[0] - bu.home[0], dz = foe.home[2] - bu.home[2], d = std::hypot(dx, dz);
        const float contact = (bu.depth * bu.scale + foe.depth * foe.scale) * 0.5f;
        const float k = d > contact && d > 0 ? 1.0f - contact / d : 0.0f;
        at = {bu.home[0] + dx * k, bu.home[1], bu.home[2] + dz * k};
    }
    if (kind != 10) view.meleeOffset = {at[0] - bu.home[0], 0, at[2] - bu.home[2]}, view.meleeStep = Ramp{1, 1}, view.meleeTurn = Ramp{0, 0};
    else at = bu.home; // (MultiLastAttack: ChangePositionToFront - its own front, here its home)
    // (hideOtherThanTarget - BtlDamageChunk::initializeProcessingList 0x97c7f0: all but the attacker and its
    // targets; a command of target kind 6 - a random foe - leaves every foe of the attacker shown too)
    cr3::Cr3Command cc;
    const bool allFoes = cr3Rules.command(a.command, cc) && cc.target == 6;
    for (size_t k = 0; k < cr3BattleUnits.size(); ++k) {
        bool keep = k == su || (allFoes && k < cr3Battle.units.size() && cr3Battle.units[k].player != bu.player);
        for (const cr3::Cr3Hit& t : a.targets) keep |= cr3Rules.fighters[size_t(t.target)].unit == int(k);
        cr3BattleUnits[k].directionHidden = !keep;
    }
    cr3LastAttack = Cr3LastAttack();
    cr3LastAttack.on = true, cr3LastAttack.unit = su, cr3LastAttack.finestArts = finestArts, cr3LastAttack.kind = kind;
    if (kind == 12 && bu.player) cutInVsComment(0x75); // (vPlay: PlayClimax 0x9a946c - cut 0x75; PlayClimaxAfter at its first hit)
    const float baseEnd = cr3ActionPlay.end; // (the hits' end)
    if (finestArts) cr3StageHidden = true; // (BtlField::Hide: the stage's update, pose and draw stopped)
    if (!camera.empty()) {
        playCr3CasterCamera(camera, su, &at);
        if (cr3Cam.name == camera && cr3Cam.end > 0) cr3ActionPlay.end = std::max(cr3ActionPlay.end, cr3Cam.end); // (the direction's next: the clip's end)
    } else playCr3AttackCamera(a, su);
    const float motionEnd = view.body.animLoaded ? view.body.anim.duration() : std::max(white / 30.0f + 0.1f, 1.0f);
    if (white >= 0) { // (updateWhiteFade: at the frame, white by the motion's end - Cr3FadeOut2(1, frames, 0, 100))
        const float from = std::min(white / 30.0f, motionEnd);
        cr3White.begin(cr3BattleClock + from, 0, 1, std::max(motionEnd - from, 1.0f / 30.0f));
        cr3LastAttack.white = true;
    }
    if (finestArts) { // (its end: full white at the motion's end, 1 s, then the global camera's 1 s)
        cr3LastAttack.whiteEnd = cr3BattleClock + motionEnd;
        cr3ActionPlay.end = std::max(cr3ActionPlay.end, motionEnd + 2.0f);
        if (white < 0) cr3White.begin(cr3BattleClock + motionEnd, 1, 1, 0), cr3LastAttack.white = true; // (no frame in the row: white at its end)
    }
    if (kind == 30) { // (BtlHoldLastAttackDirection: the hold at the motion's change cue - its unit observer's state 1)
        cr3LastAttack.holdAt = cr3BattleTables.animationChangeTime(bu.model, ext);
        cr3LastAttack.holdUnit = tu;
    }
    if (kind == 39) {
        // (BtlChildLastAttackDirection::vPlay 0x993a10: last, PlayAnimation(first target, 0x14) - the target's
        // bt03, held on its last frame - updateAnimationEnd: types from 18 aren't sent back to the stance -,
        // and its PlayCamera(20) on the target. Where that row has a camera it replaces the actor's at once:
        // the camera observer's state 2 - VUpdate(BtlCameraMotion) 0x993cb4 - inside vPlay, the direction
        // undone (home, showOtherThanTarget, PlayStayAnimation, the unit observer gone - no white -, FadeIn
        // 0.15 over nothing white) and its end the hits'. Without one, a plain Last Attack)
        setCr3Motion(tu, cr3MotionOf(20), false, true);
        const std::string targetCamera = cr3BattleTables.unitCamera(foe.model, 20);
        if (!targetCamera.empty()) playCr3CasterCamera(targetCamera, tu);
        if (!targetCamera.empty() && cr3Cam.name == targetCamera) {
            view.meleeOffset = {}, view.meleeStep = Ramp{0, 0};
            for (Cr3BattleUnitView& u : cr3BattleUnits) u.directionHidden = false;
            setCr3Motion(su, "bb01", true);
            cr3White = Ramp{0, 0};
            cr3LastAttack = Cr3LastAttack();
            cr3ActionPlay.end = baseEnd;
            SDL_Log("battle %.2f s: a child last attack - the target's %s camera %s, the direction undone", cr3BattleClock,
                    cr3MotionOf(20).c_str(), targetCamera.c_str());
            return true;
        }
    }
    SDL_Log("battle %.2f s: a %s (kind %d, motion %d) - camera %s, white %s%s", cr3BattleClock, finestArts ? "finest arts" : "last attack", kind, ext,
            camera.empty() ? "(none)" : camera.c_str(), white >= 0 ? std::to_string(int(white)).c_str() : "none", allFoes ? ", every foe shown" : "");
    return true;
}

// (the camera motion's event 2 - the next camera -: ChangePositionToHome, showOtherThanTarget,
// PlayStayAnimation, FadeIn(0.15) - BtlFade::WhiteIn, 4 frames - where it went white)
// (the Finest Arts' after the white: BtlFinestArtsDirection's fade observer 0x9964b4 - stay, home,
// showOtherThanTarget, a 1 s timer -, then 0x996528: BtlField::Show, PlayGlobal(1.0), WhiteIn 1 s)
void Viewer::stepCr3LastAttack() {
    if (!cr3LastAttack.on || !cr3LastAttack.finestArts) return;
    Cr3BattleUnitView& view = cr3BattleUnits[cr3LastAttack.unit];
    if (!cr3LastAttack.restored && cr3BattleClock >= cr3LastAttack.whiteEnd) {
        cr3LastAttack.restored = true;
        setCr3Motion(cr3LastAttack.unit, "bb01", true);
        view.meleeOffset = {}, view.meleeStep = Ramp{0, 0};
        for (Cr3BattleUnitView& u : cr3BattleUnits) u.directionHidden = false;
        cr3White.begin(cr3BattleClock, 1, 1, 0);
    }
    if (cr3LastAttack.restored && !cr3LastAttack.shown && cr3BattleClock >= cr3LastAttack.whiteEnd + 1.0f) {
        cr3LastAttack.shown = true;
        cr3StageHidden = false;
        playCr3Camera(pickCr3GlobalCamera(), 1.0f, false, {}, 0, 0); // (PlayGlobal(1.0))
        cr3White.begin(cr3BattleClock, 1, 0, 1.0f);
        SDL_Log("battle %.2f s: the finest arts' white lifted", cr3BattleClock);
    }
}

// BtlHoldLastAttackDirection (kind 30): VUpdate(BtlUnit) 0x99456c's state 1 - the actor's change cue -
// holdTarget(): BtlDamageChunk::HoldTarget 0x97d284 -> BtlDamageTarget::Hold 0x988474, each target the hold
// (status effect 32) took on, attached on the holder's effect point 2 (BtlUnit::Attach 0x9dfdd8); the first
// target's stance and hit reactions bt07 (AnimCommon 24), locked (+0x206). Its event timer's event 3 - the
// last hit - 0x994638 lets go: unlocked, BtlStatusEffectList::ChangeAnim's stance, PlayStayAnimation,
// BtlStatusList::PlayRelease (releaseCr3Hold); nothing stays held after it
void Viewer::stepCr3HoldLastAttack(const cr3::Cr3Action& a, float t) {
    Cr3LastAttack& la = cr3LastAttack;
    if (!la.on || la.kind != 30 || la.holding || la.released || la.holdAt < 0 || t < la.holdAt) return;
    la.holding = true;
    for (const cr3::Cr3Hit& th : a.targets)
        if (std::find(th.effectsGiven.begin(), th.effectsGiven.end(), 32) != th.effectsGiven.end() || cr3Rules.hasEffect(th.target, 32)) {
            cr3Held[a.source] = {th.target, 2};
            SDL_Log("battle %.2f s: %d holds %d (a hold last attack)", cr3BattleClock, a.source, th.target);
        }
    setCr3Motion(la.holdUnit, cr3MotionOf(24), true);
    cr3BattleUnits[la.holdUnit].heldPose = true;
    SDL_Log("battle %.2f s: a hold last attack's hold - %s on %s", cr3BattleClock, cr3MotionOf(24).c_str(), cr3Battle.units[la.holdUnit].name.c_str());
}

void Viewer::endCr3LastAttack() {
    if (!cr3LastAttack.on) return;
    if (cr3LastAttack.kind == 30 && cr3LastAttack.holding && !cr3LastAttack.released) { // (cut short: let go)
        cr3BattleUnits[cr3LastAttack.holdUnit].heldPose = false;
        for (size_t f = 0; f < cr3Rules.fighters.size(); ++f)
            if (cr3Rules.fighters[f].unit == int(cr3LastAttack.unit)) releaseCr3Hold(int(f));
    }
    cr3StageHidden = false;
    if (cr3LastAttack.finestArts && cr3LastAttack.shown) return void(cr3LastAttack = Cr3LastAttack());
    Cr3BattleUnitView& view = cr3BattleUnits[cr3LastAttack.unit];
    view.meleeOffset = {}, view.meleeStep = Ramp{0, 0};
    for (Cr3BattleUnitView& u : cr3BattleUnits) u.directionHidden = false;
    if (cr3LastAttack.white) cr3White.begin(cr3BattleClock, std::max(cr3White.at(cr3BattleClock), 1.0f), 0, 4.0f / 30.0f);
    cr3LastAttack = Cr3LastAttack();
}

void Viewer::playCr3SpellReceiverCamera(const cr3::Cr3Action& action, size_t source) {
    const int camera = cr3ActionPlay.timing.receiverCamera;
    const float duration = cr3BattleCam.duration();
    if (camera == 7) {
        playCr3Camera("Cam_bt_field_00", duration, false, {}, 0, 0);
    } else {
        if (action.target < 0) return;
        const size_t target = size_t(cr3Rules.fighters[size_t(action.target)].unit);
        const auto& foe = cr3Battle.units[target];
        const auto& actor = cr3Battle.units[source];
        switch (camera) {
        case 2:
            playCr3UpCamera(target);
            break;
        case 4: // (PlayGlobalCamera -> PlayGlobalToTarget)
            playCr3Camera(pickCr3GlobalCamera(), duration, false, {}, 0, 0);
            if (cr3Battle.cameraSize != 0) {
                cr3Cam.lookAt = unitPoint(actor.player ? target : source, "DummyCP", 1.2f);
                cr3Cam.lookWeight = 0.4f;
            }
            break;
        case 5:
            playCr3Camera(cr3Battle.cameras[foe.player ? 4 : 7], duration, false, {}, 0, 0);
            break;
        case 6:
            playCr3Camera(foe.player ? "Cam_bt_all_PC_00" : cr3Battle.cameras[7], duration, false, {}, 0, 0);
            break;
        case 0: case 1: case 8:
            playCr3AttackCamera(action, source);
            break;
        default:
            return;
        }
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: receiver camera %s (kind %d)", cr3BattleClock, cr3Cam.name.c_str(), camera);
}

void Viewer::playCr3UpCamera(size_t unit) {
    const auto& foe = cr3Battle.units[unit];
    cr3::MbeFile::Row row;
    std::string name = "Cam_bt_up_PC_00";
    if (cr3Battle.cameraSize == 0) name = "Cam_bt_colosseum_00";
    else if (!foe.player && cr3BattleTables.model.find("Camera", foe.cameraSize, "ifpppppppppp", row)) name = row.s(11);
    const cr3::Vec3 point = cr3Battle.cameraSize == 0 ? cr3::Vec3{} : unitPoint(unit, "DummyCP", 1.0f);
    playCr3Camera(name, cr3BattleCam.duration(), false, point, foe.player ? 0 : foe.yaw, 0);
}

void Viewer::playCr3CameraType(int type, const cr3::Cr3Action& action, size_t source) {
    cr3ActionPlay.timing.receiverCamera = type;
    playCr3SpellReceiverCamera(action, source);
}

float Viewer::stepCr3SpellCardClock(float dt) {
    auto& play = cr3ActionPlay;
    if (cr3BattleStage != Cr3BattleStage::Action || play.spellCardAge < 0) return dt;
    const float oldAge = play.spellCardAge;
    const float duration = cr3SpellCardDuration(play.spellCards.size());
    play.spellCardAge = std::min(oldAge + dt, duration);
    const float cardStep = play.spellCardAge - oldAge;
    for (size_t i = 0; i < play.spellCards.size(); ++i)
        if (oldAge < kCr3SpellCardSoundTimes[i] && play.spellCardAge >= kCr3SpellCardSoundTimes[i]) audio.playSe("bt_007", false);
    if (!play.cardWatchesCamera) return dt;
    // BtlCardDirection pauses the scene on the camera's end notification while its UI still advances.
    const float remaining = std::max(cr3Cam.start + cr3Cam.end - cr3BattleClock, 0.0f);
    if (remaining < cardStep && !play.cardPaused) {
        play.cardPaused = true;
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: caster camera held for spell card", cr3BattleClock + remaining);
    }
    return dt - std::max(cardStep - remaining, 0.0f);
}

bool Viewer::finishCr3SpellHandoff(const cr3::Cr3Action& action, size_t source, float time) {
    auto& play = cr3ActionPlay;
    if (!play.spellHandoffPending) return true;
    if (time < play.timing.spellHandoff) return false;
    if (!play.spellCards.empty()) {
        if (play.spellCardAge < 0) {
            play.spellCardAge = 0;
            play.cardWatchesCamera = !cr3Cam.held && !cr3Cam.loop && cr3BattleClock < cr3Cam.start + cr3Cam.end;
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: spell card opened (%zu)", cr3BattleClock, play.spellCards.size());
            return false;
        }
        if (play.spellCardAge < cr3SpellCardDuration(play.spellCards.size())) return false;
        play.spellCardAge = -1;
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: spell card closed", cr3BattleClock);
    }
    play.spellHandoffPending = false;
    // The unit timer can reach a marker before the receiver subscribes. Dispatch the overdue
    // effect on the handoff and keep its flight and damage intervals relative to that dispatch.
    if (!play.timing.effects.empty()) {
        const float delay = std::max(time - play.timing.effects.front().time, 0.0f);
        for (auto& cue : play.timing.effects) cue.time += delay;
        for (float& hit : play.timing.hits) hit += delay;
        play.end = std::max(play.end, play.timing.hits.back() + (play.timing.steal ? play.timing.projectile.duration : 0.0f) + 0.9f);
    }
    playCr3SpellReceiverCamera(action, source);
    return true;
}

// BtlDirection plays the source motion and shows the hits under the attack camera.
void Viewer::startCr3Action() {
    endCr3LastAttack(); // (the next direction's camera: the last one's undone)
    if (cr3ActionAt >= cr3Actions.size() || cr3ForcedEnd >= 0) return endCr3Actions(); // (a script's forced end: after the direction)
    const cr3::Cr3Action& a = cr3Actions[cr3ActionAt];
    cr3ActionStart = cr3BattleClock;
    cr3ActionPlay = Cr3ActionPlay();
    {
        int sum = 0;
        for (const cr3::Cr3Action& x : {cr3Actions[cr3ActionAt]})
            for (int h : x.hits) sum += h;
        SDL_Log("battle %.2f s: action %zu - %d -> %d, command %d, %zu hits, %d", cr3BattleClock, cr3ActionAt, cr3Actions[cr3ActionAt].source,
                cr3Actions[cr3ActionAt].target, cr3Actions[cr3ActionAt].command, cr3Actions[cr3ActionAt].hits.size(), sum);
        if (cr3Actions[cr3ActionAt].finisher) SDL_Log("battle: a finisher - command %d (addLastAttack)", cr3Actions[cr3ActionAt].command);
    }
    if (a.source < 0) return startCr3SourcelessAction(a);
    const cr3::Cr3Fighter& s = cr3Rules.fighters[size_t(a.source)];
    if (cr3::Cr3Command ac; s.player && cr3Rules.command(a.command, ac) && (ac.extraType == 14 || ac.extraType == 15)) // (an Apple's: AddUsedItem, each once)
        if (cr3UsedApples[ac.extraType - 14].insert(a.command).second) SDL_Log("battle: %s x%d used (command %d)", ac.extraType == 14 ? "EXP" : "money", ac.extraConstants.empty() ? 1 : ac.extraConstants[0], a.command);
    if (a.command == kCr3ReleaseDirection || a.command == kCr3WaitDieDirection) {
        // (a script's inserted direction: BtlReleaseDirection::vPlay 0x009984d8 - the holder's
        // AnimCommon 0x13 -, BtlWaitDieDirection::vPlay 0x0098f4e8 - the timing of its motion playing, its
        // death's -; the release - BtlStatus::PlayRelease - at their event 3, here their end -
        // stepCr3Action -, and over at the motion's end)
        const size_t su = size_t(s.unit);
        if (a.command == kCr3ReleaseDirection) setCr3Motion(su, cr3MotionOf(0x13), false);
        const Cr3BattleUnitView& u = cr3BattleUnits[su];
        const float left = u.body.animLoaded ? u.body.anim.duration() - (cr3BattleClock - u.motionStart) : 1.0f;
        cr3ActionPlay.end = std::max(left, 0.3f);
        return;
    }
    const size_t su = size_t(s.unit);
    const cr3::Cr3BattleUnit& bu = cr3Battle.units[su];
    if (cr3::MbeFile::Row dir; a.command >= 0 && cr3BattleTables.direction.find("CommandDirection", a.command, "ii", dir)) cr3ActionPlay.kind = dir.i(1);
    // (CreateDirection 0x990d98: BtlShowDirection / BtlHideDirection - action types 0x1c / 0x1d - get no BtlAnnounceDirection)
    const bool announced = cr3ActionPlay.kind != 28 && cr3ActionPlay.kind != 29;
    if (a.command >= 0 && announced) { // (BtlDirection::openAnnounce: the command's name - an item's, used - for a second)
        auto item = cr3TurnItems.find(a.source);
        // (a command change - status kind 120 - plays another command than the item's chosen: its own name)
        if (item != cr3TurnItems.end() && std::none_of(cr3Choices.begin(), cr3Choices.end(), [&](const std::array<int, 3>& c) { return c[0] == a.source && c[1] == a.command; }))
            item = cr3TurnItems.end();
        std::string text = item != cr3TurnItems.end() ? cr3ItemText(item->second, false) : cr3NameOf("command_name.mbe", "CommandName", a.command);
        if (const size_t at = text.find("{source}"); at != std::string::npos) text.replace(at, 8, cr3FighterName(s));
        if (const size_t at = text.find("{assistant}"); at != std::string::npos) text.replace(at, 11, cr3AssistantName()); // (OpenAssist's: never a party member)
        // (BtlDamageChunk::OpenAnnounce 0x0097c924: no item, a command that can be cooperated in - +0xbc bit 7 - and
        // more than one unit in its cooperation - AddCooperation's list, the actor counted -: BtlAnnounceUI::
        // OpenAdditionNum 0x0099e490 - AnnounceBattle 0x1c + the count appended: 30 " II", 31 " III")
        if (cr3::Cr3Command ac; item == cr3TurnItems.end() && cr3Rules.command(a.command, ac) && (ac.flags & 0x80) && !a.partners.empty()) {
            text += announceText("AnnounceBattle", 0x1c + 1 + int(a.partners.size()));
            SDL_Log("battle %.2f s: cooperation name \"%s\"", cr3BattleClock, text.c_str());
        }
        if (!text.empty()) setCr3Announce(text, 1.0f);
        commentCr3CommandHit(a, cr3ActionPlay.kind); // (BtlAnnounceDirection::vPlay 0x99066c: PlayCommandHit)
    }
    if (a.command < 0) { // (Guard: bg01; -2 unable to act - a kind 31 effect -: a moment)
        if (a.command == -1) setCr3Motion(su, "bg01", false);
        cr3ActionPlay.end = a.command == -1 ? 1.0f : 0.5f;
        return;
    }
    cr3::Cr3Command c;
    cr3Rules.command(a.command, c);
    if (size_t(a.source) < cr3MpShown.size()) cr3MpShown[size_t(a.source)] = std::max(cr3MpShown[size_t(a.source)] - a.mpCost, 0);
    if (size_t(a.source) < cr3HpShown.size()) {
        const int hCost = cr3Rules.hpCost(s, c);
        if (hCost > 0) cr3HpShown[size_t(a.source)] = std::max(cr3HpShown[size_t(a.source)] - hCost, 1);
    }
    for (int pi : a.partners) {
        const cr3::Cr3Fighter& pf = cr3Rules.fighters[size_t(pi)];
        if (size_t(pi) < cr3MpShown.size()) cr3MpShown[size_t(pi)] = std::max(cr3MpShown[size_t(pi)] - cr3Rules.mpCost(pf, c), 0);
        if (size_t(pi) < cr3HpShown.size()) {
            const int hCost = cr3Rules.hpCost(pf, c);
            if (hCost > 0) cr3HpShown[size_t(pi)] = std::max(cr3HpShown[size_t(pi)] - hCost, 1);
        }
    }
    if (startCr3SpecialAction(a, su, c)) return;
    size_t mostHits = 0;
    for (const cr3::Cr3Hit& target : a.targets) mostHits = std::max(mostHits, target.hits.size());
    cr3ActionPlay.directionCommand = cr3BattleTables.directionCommand(a.command, s.attackCommand);
    auto timing = cr3BattleTables.actionTiming(bu.model, cr3ActionPlay.directionCommand, std::max(mostHits, size_t(1)), c.repeat, c.element);
    std::string motion = cr3MotionOf(timing.motions.empty() ? c.motion : timing.motions.front().type);
    if (motion.empty() || motion == "bb01") motion = "bs01"; // (a shot's: BtlShotDirection plays 0xf)
    setCr3Motion(su, motion, false);
    for (int pi : a.partners) setCr3Motion(size_t(cr3Rules.fighters[size_t(pi)].unit), motion, false); // (a cooperation's others: the same)
    if (a.cooperated()) SDL_Log("battle %.2f s: a cooperation of %zu", cr3BattleClock, a.partners.size() + 1);
    if (a.cooperated()) commentCr3Cooperation(a); // (its first partner's cooperation effect, just after the start)
    const ViewFigure& f = cr3BattleUnits[su].body;
    const float length = f.animLoaded && cr3BattleUnits[su].motion == motion ? f.anim.duration() : 1.2f;
    float fallbackTail = 0;
    if (!timing.hits.empty()) {
        if (!timing.castSound.empty()) audio.playSe(timing.castSound, false);
        cr3ActionPlay.nextMotion = 1; // the first animation was started above
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: %s native hit timing (%.3f to %.3f s)",
                     cr3BattleClock, motion.c_str(), double(timing.hits.front()), double(timing.hits.back()));
    } else {
        const float first = std::max(length * 0.45f, 0.3f);
        const float gap = mostHits > 1 ? std::min(0.15f, length * 0.4f / float(mostHits)) : 0.15f;
        for (size_t h = 0; h < std::max(mostHits, size_t(1)); ++h) timing.hits.push_back(first + gap * float(h));
        fallbackTail = mostHits > 0 ? gap : 0;
    }
    cr3ActionPlay.end = std::max(length, timing.hits.back() + 0.9f + fallbackTail);
    if (timing.steal) cr3ActionPlay.end = std::max(cr3ActionPlay.end, timing.hits.back() + timing.projectile.duration + 0.9f); // (the last bullet in)
    const bool showHide = cr3ActionPlay.kind == 28 || cr3ActionPlay.kind == 29;
    if (showHide) { // (BtlShowDirection / BtlHideDirection vPlay 0x9940e8 / 0x99b8bc: playAllDamage - every hit at once -, over at its motion's end)
        for (float& h : timing.hits) h = 0.05f;
        cr3ActionPlay.end = std::max(length, 0.1f);
    }
    cr3ActionPlay.timing = std::move(timing);
    if (cr3ActionPlay.kind == 21 || showHide) {
        // (BtlExpansionMeleeDirection::vPlay 0x997f84: openAnnounce(1.0), generateEffectTiming - the hits by its effect's groups -,
        // bt01 in place, playCameraByType - +0x0c, 0 -; at bt01's end bo04, then the stay: stepCr3Action. Show / Hide: bt01 / bt02,
        // ShowMesh / HideMesh - the Model row's +0x10 sub-mesh, m222_sub: no figure has it -, playCameraByType - 2 -, no announcement)
        cr3::MbeFile::Row dir;
        const int camera = cr3BattleTables.direction.find("CommandDirection", a.command, "iiii", dir) ? dir.i(3) : 0;
        playCr3CameraType(camera, a, su);
        SDL_Log("battle %.2f s: %s (kind %d, motion %s, camera type %d)", cr3BattleClock, showHide ? "a show / hide direction" : "an expansion melee",
                cr3ActionPlay.kind, motion.c_str(), camera);
        return;
    }
    if (cr3ActionPlay.kind == 38 && startCr3Avatar(a, su)) return;
    if (motion == "bo01" && a.targets.size() == 1 && a.partners.empty())
        startCr3Melee(su, size_t(cr3Rules.fighters[size_t(a.targets.front().target)].unit));
    if (startCr3LastAttack(a, su)) return;
    if (startCr3RangedCamera(a, su)) return;
    auto& play = cr3ActionPlay;
    if (play.timing.spellHandoff < 0 || cr3TurnItems.count(a.source)) {
        playCr3AttackCamera(a, su);
        return;
    }
    play.spellHandoffPending = true;
    playCr3CasterCamera(play.timing.castCamera, su);
    if ((bu.modelFlags & 8) && s.geneMaster >= 0) {
        play.spellCards.push_back(s.geneMaster);
        for (int partner : a.partners) {
            const int gene = cr3Rules.fighters[size_t(partner)].geneMaster;
            if (gene >= 0 && play.spellCards.size() < kCr3SpellCardSoundTimes.size()) play.spellCards.push_back(gene);
        }
    }
}

void Viewer::stepCr3Action() {
    if (cr3Opening.on) return stepCr3Opening();
    if (cr3PendingStart) {
        cr3PendingStart = false;
        return startCr3Action();
    }
    if (cr3ActionAt >= cr3Actions.size()) return endCr3Actions();
    stepCr3LastAttack();
    const cr3::Cr3Action& a = cr3Actions[cr3ActionAt];
    if (a.source < 0) return stepCr3SourcelessAction(a);
    const size_t source = size_t(cr3Rules.fighters[size_t(a.source)].unit);
    if (cr3ActionPlay.kind == 38 && !stepCr3Avatar(a, source)) return; // (its white and its movie: the direction waits)
    const float t = cr3BattleClock - cr3ActionStart;
    const auto& timing = cr3ActionPlay.timing;
    if (cr3ActionPlay.kind == 37) stepCr3Reinforcement(a, t);
    while (cr3ActionPlay.nextMotion < timing.motions.size() && t >= timing.motions[cr3ActionPlay.nextMotion].time) {
        const auto& next = timing.motions[cr3ActionPlay.nextMotion++];
        const std::string motion = cr3MotionOf(next.type);
        setCr3Motion(source, motion, false, cr3ActionPlay.melee);
        for (int partner : a.partners) setCr3Motion(size_t(cr3Rules.fighters[size_t(partner)].unit), motion, false);
        if (cr3ActionPlay.ranged) playCr3MotionCamera(source, next.type); // (onAnimationChange 0x98dc34 / 0x99a414: 16, 17 through PlayAnimation)
        if (cr3BattleUnits[source].motion == motion && cr3BattleUnits[source].body.animLoaded)
            cr3ActionPlay.end = std::max(cr3ActionPlay.end, t + cr3BattleUnits[source].body.anim.duration());
    }
    stepCr3HoldLastAttack(a, t);
    stepCr3CastEffects(source, t);
    if (!finishCr3SpellHandoff(a, source, t)) return;
    stepCr3ActionEffects(a, source, t);
    size_t mostHits = 0;
    for (const cr3::Cr3Hit& th : a.targets) mostHits = std::max(mostHits, th.hits.size());
    while (a.target >= 0 && cr3ActionPlay.hitsShown < int(mostHits) && size_t(cr3ActionPlay.hitsShown) < timing.hits.size() &&
           t >= timing.hits[size_t(cr3ActionPlay.hitsShown)]) {
        const size_t h = size_t(cr3ActionPlay.hitsShown++);
        // (BtlFinestArtsDirection::VUpdate(BtlEventTimer) 0x9963b8 event 1 - just before event 2, the first hit -: PlayClimaxAfter
        // 0x9a94cc, a player's: 0x76 or 0x76 + its character - 1 .. 5, else none - at 50 / 50, requested a second on)
        if (h == 0 && cr3LastAttack.on && cr3LastAttack.finestArts && cr3LastAttack.kind == 12 && cr3Rules.fighters[size_t(a.source)].player) {
            const int other = cr3CommentChar(cr3Rules, a.source, 0x76);
            requestVsComment(SDL_rand(2) ? 0x76 : other, 1.0f);
        }
        for (const cr3::Cr3Hit& th : a.targets) { // (each target the hit lands on at once)
            if (h >= th.hits.size() || th.stealBack) continue; // (a steal's own: as its bullet lands - stepCr3StealBack)
            const int d = th.hits[h];
            const int mpD = h < th.mpHits.size() ? th.mpHits[h] : 0;
            const uint8_t flags = h < th.flags.size() ? th.flags[h] : 0;
            if (h < th.gauge.size()) cr3GaugeShown = th.gauge[h];
            const size_t tu = size_t(cr3Rules.fighters[size_t(th.target)].unit);
            int& shown = cr3HpShown[size_t(th.target)];
            const int shownBefore = shown;
            shown = std::clamp(shown - d, 0, cr3Rules.fighters[size_t(th.target)].maxHp);
            if (size_t(th.target) < cr3MpShown.size() && mpD != 0)
                cr3MpShown[size_t(th.target)] = std::clamp(cr3MpShown[size_t(th.target)] - mpD, 0, cr3Rules.fighters[size_t(th.target)].maxMp);
            // (BtlDamageUI::Draw: a miss its word alone - kind 1 -, a critical its number and
            // kind 5)
            if (flags & 3) SDL_Log("battle %.2f s: %s%s", cr3BattleClock, (flags & 1) ? "critical " : "", (flags & 2) ? "down" : "");
            if (flags & 4) {
                SDL_Log("battle %.2f s: miss", cr3BattleClock);
                cr3Popups.push_back({-1, unitPoint(tu, "DummyDP01", 1.2f), cr3BattleClock, 1});
                continue;
            }
            if (flags & 8) { // (blocked - calculateFluctuation's +0xa7 / +0xa8: the word alone, DamageInfoMes kind 3, bt_093)
                SDL_Log("battle %.2f s: blocked", cr3BattleClock);
                cr3Popups.push_back({-1, unitPoint(tu, "DummyDP01", 1.2f), cr3BattleClock, 3});
                audio.playSe("bt_093", false);
                continue;
            }
            if (th.reflectedFrom >= 0 && h == 0) SDL_Log("battle %.2f s: reflected by %d onto %d (Mental Wall)", cr3BattleClock, th.reflectedFrom, th.target);
            cr3::Cr3Command hc;
            cr3Rules.command(a.command, hc);
            // (TakeDamageToDirection: AddTotalDamage where the damage isn't 0 and the two are on opposite sides -
            // the party struck by a monster as much as a monster by the party)
            if (d != 0 && th.reflectedFrom < 0 && cr3Rules.fighters[size_t(a.source)].player != cr3Rules.fighters[size_t(th.target)].player)
                addCr3Total(th.target, d, shownBefore, shown);
            if (th.stolen >= 0 && h == 0) { // (BtlStatus::TakeDamageToDirection 0x9b42a0: a Steal Gold hit - OpenStealGold, 1 s)
                std::string text = announceText("AnnounceBattle", th.stolen >= 1 ? 0x28 : 0x29);
                if (const size_t at = text.find("{gold}"); at != std::string::npos) text.replace(at, 6, std::to_string(th.stolen));
                if (const size_t at = text.find("{target}"); at != std::string::npos) text.replace(at, 8, cr3FighterName(cr3Rules.fighters[size_t(th.target)]));
                setCr3Announce(text, 1.0f);
                game.cr3Money = std::max(cr3Rules.gold, 0); // (taken at the calculation - TakeDamageToCalculation 0x9b41c4)
                SDL_Log("battle: %d money stolen (%d left)", th.stolen, game.cr3Money);
            }
            const cr3::Vec3 dp = unitPoint(tu, "DummyDP01", 1.2f);
            const int color = h < th.colors.size() ? int(th.colors[h]) : (d < 0 ? 3 : 0);
            if (d == 0 && mpD != 0) {
                cr3Popups.push_back({std::abs(mpD), dp, cr3BattleClock, 0, 4});
            } else {
                cr3Popups.push_back({std::abs(d), dp, cr3BattleClock, 0, color});
                if (mpD != 0) cr3Popups.push_back({std::abs(mpD), {dp[0], dp[1] - 0.35f, dp[2]}, cr3BattleClock, 0, 4});
            }
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "battle %.2f s: %d damage (%d mp) on %s", cr3BattleClock, d, mpD, cr3Battle.units[tu].name.c_str());
            if (flags & 1) cr3Popups.push_back({-1, dp, cr3BattleClock, 5});
            cr3HitEffects(cr3ActionPlay.directionCommand, hc.element, tu, flags & 1);
            if (cr3BattleUnits[tu].heldPose) setCr3Motion(tu, cr3MotionOf(24), true); // (held: ChangeDamageAnimation(0x18))
            else if (d > 0 || (d == 0 && mpD > 0) || (d == 0 && mpD == 0 && !hc.ally())) setCr3Motion(tu, "bd01", false);
            else if (shown > 0 && cr3BattleUnits[tu].knockedOut) restoreCr3Unit(tu);
        }
        audio.playSe(0x164, false);
        if (cr3ActionPlay.hitsShown == int(mostHits)) {
            if (Cr3LastAttack& la = cr3LastAttack; la.on && la.kind == 30 && la.holding && !la.released) { // (the hold's event 3)
                la.released = true;
                cr3BattleUnits[la.holdUnit].heldPose = false;
                setCr3Motion(la.holdUnit, "bb01", true);
                releaseCr3Hold(a.source);
                SDL_Log("battle %.2f s: a hold last attack's release at its last hit", cr3BattleClock);
            }
            announceCr3Effects(a);
            announceCr3FieldToggle(a, t);
            commentCr3AfterDamage(a); // (every target list announced: PlayAfterDamageVoice)
            // BtlDirection::playKnockout follows the hit sequence, including misses.
            for (const cr3::Cr3Hit& th : a.targets) {
                if (!th.killed) continue;
                // (BtlNotKnockOutSituDirection - action type 0x12, 1755's squeeze -: one held falls
                // at its release instead, BtlStatus::PlayRelease's PlayKnockout)
                bool held = false;
                for (const auto& [holder, h] : cr3Held) held |= h.first == th.target;
                if (cr3::Cr3Command kc; held && cr3Rules.command(a.command, kc) && kc.direction == 0x12) continue;
                rollCr3Drop(th.target);
                knockOutCr3Unit(size_t(cr3Rules.fighters[size_t(th.target)].unit));
            }
        }
    }
    if (mostHits == 0 && cr3ActionPlay.hitsShown == 0 && !timing.hits.empty() && t >= timing.hits.front()) { // (no hits - its effects alone)
        cr3ActionPlay.hitsShown = 1;
        announceCr3Effects(a);
        announceCr3FieldToggle(a, t);
        commentCr3AfterDamage(a);
    }
    if (timing.steal) stepCr3StealBack(a, source, t);
    stepCr3MeleeReturn(source, t);
    if (cr3ActionPlay.kind == 21 && cr3ActionPlay.stage == 0) { // (onAnimationEnd 0x997e40: bt01 over - ChangeTargetForBackStep, bo04)
        Cr3BattleUnitView& view = cr3BattleUnits[source];
        if (!view.body.animLoaded || cr3BattleClock - view.motionStart >= view.body.anim.duration()) {
            cr3ActionPlay.stage = 1;
            const std::string back = cr3MotionOf(14);
            setCr3Motion(source, back, false);
            const float len = view.motion == back && view.body.animLoaded ? view.body.anim.duration() : 0.0f;
            cr3ActionPlay.end = std::max(cr3ActionPlay.end, t + len); // (its end: PlayStayAnimation, the direction over)
            SDL_Log("battle %.2f s: an expansion melee's %s (%.2f s)", cr3BattleClock, back.c_str(), double(len));
        }
    }
    if (t >= cr3ActionPlay.end && !cr3ActionPlay.melee && !stepCr3Analyze(a)) return; // (an Analyze's lines first)
    if (t >= cr3ActionPlay.end && !cr3ActionPlay.melee) {
        for (const cr3::Cr3Hit& th : a.targets) // (a cover's coverer home again - ChangePositionToHome)
            if (th.coveredFrom >= 0) cr3BattleUnits[size_t(cr3Rules.fighters[size_t(th.target)].unit)].meleeOffset = {},
                                     cr3BattleUnits[size_t(cr3Rules.fighters[size_t(th.target)].unit)].meleeStep = Ramp{0, 0};
        showCr3ActionState(a);
        for (const cr3::Cr3Hit& th : a.targets) {
            cr3HpShown[size_t(th.target)] = th.hpAfter;
            if (size_t(th.target) < cr3MpShown.size()) cr3MpShown[size_t(th.target)] = th.mpAfter;
        }
        cr3GaugeShown = a.gaugeAfter;
        // (the holds: CreateDirection 0x00990d98 by the action type - the CommandDirection kind:
        // 0x11 BtlHoldDirection, 0x1e BtlHoldLastAttackDirection: BtlDamageChunk::HoldTarget 0x0097d284 -> BtlDamageTarget::
        // Hold 0x00988474, each target still with status effect 32 - UpdateHold 0x00988fa8 -
        // attached on effect point 2; 0x13 BtlReleaseDirection, and the scripts' inserted ones:
        // BtlStatusList::PlayRelease 0x009bf930)
        if (a.command == kCr3ReleaseDirection || a.command == kCr3WaitDieDirection) {
            releaseCr3Hold(a.source);
        } else if (cr3::Cr3Command dc; a.command >= 0 && cr3Rules.command(a.command, dc)) {
            if (dc.direction == 0x11) { // (0x1e's hold: the direction's own - stepCr3HoldLastAttack -, let go at its last hit)
                for (const cr3::Cr3Hit& th : a.targets)
                    if (std::find(th.effectsGiven.begin(), th.effectsGiven.end(), 32) != th.effectsGiven.end() || cr3Rules.hasEffect(th.target, 32)) {
                        cr3Held[a.source] = {th.target, 2};
                        SDL_Log("battle %.2f s: %d holds %d", cr3BattleClock, a.source, th.target);
                    }
            } else if (dc.direction == 0x13) {
                releaseCr3Hold(a.source);
            }
        }
        ++cr3ActionAt;
        cr3AnalyzeDone = false;
        { // (BtlDirectionManager::playSequenceNext 0x00991ae0: BtlTotalDamageUI::Close as the sequence ends, unless a
          // direction follows and the one ended chains - its chunk's +0xa5, Cr3Action::chains: an interrupt - with its
          // command's +0xbd bit 2 clear, or the next one chains)
            bool chains = false;
            if (cr3ActionAt < cr3Actions.size() && cr3Actions[cr3ActionAt].command >= 0) {
                cr3::Cr3Command ac;
                chains = (a.chains && (!cr3Rules.command(a.command, ac) || !(ac.flags & 0x400))) || cr3Actions[cr3ActionAt].chains;
            }
            if (!chains) closeCr3Totals();
            else SDL_Log("battle %.2f s: the totals kept - an interrupt chains", cr3BattleClock);
        }
        // (BtlSysStateAction: the battle's script after each direction - battle_0002's cut-in
        // once the spider endures at 1 HP - and the next action after it; BattleArgument its
        // action - CallDirectionEnd: SetSource, SetDamageChunk)
        cr3ArgAction = int(cr3ActionAt) - 1, cr3ArgSource = cr3Actions[cr3ActionAt - 1].source;
        // (a guard, or a unit unable to act, has no direction: no CallDirectionEnd - battle
        // 175's guard lesson would tell the guard off)
        const bool directed = cr3Actions[cr3ActionAt - 1].command >= 0;
        if (directed && runCr3BattleScript("Battle_Direction_End_Base") && cr3BattleEvents && cr3BattleEvents->running()) {
            cr3ActionStart = cr3BattleClock, cr3ActionPlay = Cr3ActionPlay(), cr3ActionPlay.end = 0, cr3ActionPlay.hitsShown = 1 << 20;
            cr3PendingStart = true;
            return;
        }
        startCr3Action();
    }
}

// The special directions (BtlDirectionManager::CreateDirection 0x990d98 by the CommandDirection kind): 14 BtlGeneChange-
// Direction - no Announce, no cast -, 31 BtlEscapeDirection, 37 BtlRainforcementDirection played here whole; 23
// BtlStealMagicDirection and 38 BtlAvatarDirection by the generic one with their own steps. False: not one of these
bool Viewer::startCr3SpecialAction(const cr3::Cr3Action& a, size_t su, const cr3::Cr3Command& c) {
    auto& play = cr3ActionPlay;
    if (play.kind != 14 && play.kind != 31 && play.kind != 37) return false;
    cr3::MbeFile::Row dir; // (+0x0c its camera type, +0x2c its effect, +0x34 its point)
    const bool row = cr3BattleTables.direction.find("CommandDirection", a.command, "iiiifii_piiii", dir);
    const int camera = row ? dir.i(3) : -1;
    play.directionCommand = a.command;
    play.timing.hits = {0.1f}; // (its status effects' lines: the no-hit path's)
    if (play.kind == 14) {
        // BtlGeneChangeDirection::vPlay 0x98fcbc: playCameraByType (+0x0c 2), playSingleEffect - +0x2c, 51 be005card04, on
        // itself -, BtlDamageChunk::AnnounceGeneChanged 0x97cd38: the command's name for 1.0 s (startCr3Action's), then
        // BtlVoiceUtility::Play(source, 0x39) and DisableCategory(0x39) - once a battle. No motion
        playCr3CameraType(camera, a, su);
        play.end = 1.0f;
        if (row)
            if (Cr3Fx* fx = spawnCr3Effect(dir.i(9), cr3EffectPoint(su, dir.i(11)), 1)) play.end = std::max(play.end, fx->animLength);
        if (!cr3GeneVoiceUsed) cr3GeneVoiceUsed = true, SDL_Log("battle: a gene switch's voice category 57 - once a battle (no category voices played)");
        SDL_Log("battle %.2f s: a gene switch - command %d by fighter %d (camera %d, effect %d)", cr3BattleClock, a.command, a.source, camera, row ? dir.i(9) : -1);
        return true;
    }
    Cr3BattleUnitView& view = cr3BattleUnits[su];
    const cr3::Cr3BattleUnit& bu = cr3Battle.units[su];
    if (play.kind == 31) {
        // BtlEscapeDirection::vPlay 0x993e74: openAnnounce(1.5), playAllDamage - its status effect 51 -, playCameraByType
        // (+0x0c 2), BtlUnit::PlayEscape 0x9df738 - alpha 1 to 0 over 0.8 s and a move from home 10 back, away from the
        // other side (its run motion, state 0xb, not traced: the stance kept) -; over as the name's line closes
        cr3Announce.seconds = 1.5f;
        playCr3CameraType(camera, a, su);
        view.opacity.begin(cr3BattleClock, 1, 0, 0.8f);
        view.meleeOffset = {0, 0, bu.player ? 1.0f : -1.0f}, view.meleeStep.begin(cr3BattleClock, 0, 1, 0.8f), view.meleeTurn = Ramp{0, 0};
        play.end = 1.5f;
        SDL_Log("battle %.2f s: fighter %d escapes - command %d (camera %d)", cr3BattleClock, a.source, a.command, camera);
        return true;
    }
    // BtlRainforcementDirection::vPlay 0x98bd48: openAnnounce(1.5) - the name -, BtlUnitList::PlayUpCamera on the caster, its
    // motion (+0x08: getExtensionAnimType); the rest as the line closes (stepCr3Reinforcement)
    cr3Announce.seconds = 1.5f;
    playCr3UpCamera(su);
    setCr3Motion(su, cr3MotionOf(c.motion), false);
    play.end = 3.0f;
    SDL_Log("battle %.2f s: a call for help - command %d by fighter %d on fighter %d", cr3BattleClock, a.command, a.source, a.reinforced.fighter);
    return true;
}

// BtlRainforcementDirection::VUpdate(BtlAnnounceUI) 0x98bdcc: the name's line closed - playCameraByType (+0x0c), playAllDamage,
// playKnockout -, then the first target's +0x110 bit 4: AnnounceBattle 92 "Reinforcements arrived!" - VRainforceToDirection
// 0x9c7000 - or 91 "...But no one came.", 1.5 s each; over as it closes
void Viewer::stepCr3Reinforcement(const cr3::Cr3Action& a, float t) {
    auto& play = cr3ActionPlay;
    if (play.stage != 0 || t < 1.5f) return;
    play.stage = 1;
    cr3::MbeFile::Row dir;
    const size_t su = size_t(cr3Rules.fighters[size_t(a.source)].unit);
    if (cr3BattleTables.direction.find("CommandDirection", a.command, "iiii", dir)) playCr3CameraType(dir.i(3), a, su);
    const bool came = a.reinforced.success && a.reinforced.monster >= 0 && a.reinforced.fighter >= 0;
    setCr3Announce(announceText("AnnounceBattle", came ? 92 : 91), 1.5f);
    SDL_Log("battle %.2f s: a call for help - line %d%s", cr3BattleClock, came ? 92 : 91, came ? " (reinforcements)" : " (no one came)");
    if (came) {
        reinforceCr3Slot(a.reinforced.fighter, a.reinforced.monster);
        for (const cr3::Cr3Hit& th : a.targets)
            if (th.target == a.reinforced.fighter) cr3HpShown[size_t(th.target)] = th.hpAfter, cr3MpShown[size_t(th.target)] = th.mpAfter;
    }
    play.end = std::max(play.end, t + 1.5f);
}

// VRainforceToDirection 0x9c7000 / BtlUnit::Rainforce 0x9dc3f4: the fallen's drop banked where it was announced
// (VAcquireDrop), the slot's model made anew - Cr3Battle::replaceMonster -, its figures loaded, ChangePositionToHome,
// PlayStayAnimation, BtlUnit::FadeIn 0x9dfee4 - alpha 0 to 1 over 1.0 s; its own drop rolled (whether the original keeps
// the old slot's not traced)
void Viewer::reinforceCr3Slot(int fi, int monster) {
    if (fi < 0 || size_t(fi) >= cr3Rules.fighters.size() || cr3Rules.fighters[size_t(fi)].unit < 0) return;
    const size_t su = size_t(cr3Rules.fighters[size_t(fi)].unit);
    if (const auto it = cr3Drops.find(fi); it != cr3Drops.end()) {
        if (it->second > 0 && cr3DropsAnnounced.count(fi)) cr3WaveDrops.push_back(it->second);
        cr3Drops.erase(it);
    }
    cr3DropsAnnounced.erase(fi);
    if (!cr3Battle.replaceMonster(cr3BattleTables, su, monster)) {
        SDL_Log("battle: reinforcements - monster %d has no model", monster);
        return;
    }
    Cr3BattleUnitView& old = cr3BattleUnits[su];
    unload(old.body), unload(old.head), unload(old.weapon);
    old = Cr3BattleUnitView();
    loadCr3BattleUnits(su, su + 1);
    setCr3Motion(su, "bb01", true);
    cr3BattleUnits[su].opacity.begin(cr3BattleClock, 0, 1, 1.0f);
    rollCr3Drops();
    SDL_Log("battle %.2f s: reinforcements - fighter %d is monster %d now (%s)", cr3BattleClock, fi, monster, cr3Battle.units[su].figure.c_str());
}

// BtlAvatarDirection (Create 0x99767c, vPlay 0x9976e4): after the name, the caster's motion (+0x08) and its up camera
// (PlayEnemyUp / PlayPlayerUp; motion 10 BtlCameraSystem::PlayItem 0x978efc - Cam_bt_item_PC_00 at the unit's camera point,
// turned as the unit, to its own end, no colosseum variant); at its event 2 BtlFade::WhiteOut(1.0) and
// SE ev_018; white, BtlMovie::Play - Effect(+0x28)'s file -; the movie over, WhiteIn(1.0), playCameraByType (+0x0c 5) and
// playGroupEffect (+0x2c) with the hits from its timer. The effects and hits put after the white; the movie a pause
bool Viewer::startCr3Avatar(const cr3::Cr3Action& a, size_t su) {
    auto& play = cr3ActionPlay;
    cr3::MbeFile::Row dir, fx;
    if (!cr3BattleTables.direction.find("CommandDirection", a.command, "iiiifii_pi", dir) || play.timing.hits.empty()) return false;
    const float cue = cr3BattleTables.animationCueTime(cr3Battle.units[su].model, dir.i(2));
    play.cue = cue >= 0 ? cue : 0.5f;
    play.movie = cr3BattleTables.effect.find("Effect", dir.i(8), "i_p", fx) ? fx.s(1) : std::string();
    const float from = play.timing.effects.empty() ? play.timing.hits.front() : play.timing.effects.front().time;
    const float shift = play.cue + 1.0f - from;
    for (auto& e : play.timing.effects) e.time += shift;
    for (float& h : play.timing.hits) h += shift;
    play.end = std::max(play.end, play.timing.hits.back() + 0.9f);
    if (dir.i(2) == 10) {
        const auto& unit = cr3Battle.units[su];
        playCr3Camera("Cam_bt_item_PC_00", cr3BattleCam.duration(), false, unitPoint(su, "DummyCP", 1.0f), unit.yaw, 0);
        if (!cr3BattleCam.has("Cam_bt_item_PC_00")) SDL_Log("battle: Cam_bt has no Cam_bt_item_PC_00");
    } else {
        playCr3UpCamera(su);
    }
    SDL_Log("battle %.2f s: an avatar - command %d, motion %d, its event 2 at %.2f s, movie %s", cr3BattleClock, a.command, dir.i(2), double(play.cue),
            play.movie.empty() ? "(none)" : play.movie.c_str());
    return true;
}

// false: its movie still playing (BtlMovie::OnUpdate 0x7de00c - IsPlayingMovie) - the direction's time held
bool Viewer::stepCr3Avatar(const cr3::Cr3Action& a, size_t source) {
    auto& play = cr3ActionPlay;
    const float t = cr3BattleClock - cr3ActionStart;
    if (play.stage == 0 && t >= play.cue) { // (0x99783c: the motion's event 2)
        play.stage = 1;
        cr3White.begin(cr3BattleClock, 0, 1, 1.0f);
        audio.playSe("ev_018", false);
        SDL_Log("battle %.2f s: an avatar's white-out", cr3BattleClock);
    }
    if (play.stage == 1 && t >= play.cue + 1.0f) { // (VUpdate(BtlFade) 0x9978e4: white - BtlMovie::Play 0x7de050)
        play.stage = 2, play.pausedAt = cr3BattleClock;
        if (!play.movie.empty()) startMovie(play.movie);
        if (!movie.mv) SDL_Log("battle: an avatar's movie %s - not in the release, skipped", play.movie.c_str());
    }
    if (play.stage == 2) {
        if (movie.mv && movie.name == play.movie) return false;
        play.stage = 3;
        cr3ActionStart += cr3BattleClock - play.pausedAt;
        cr3White.begin(cr3BattleClock, 1, 0, 1.0f); // (VUpdate(BtlMovie) 0x9979e8: WhiteIn(1.0), the camera, the group effect)
        if (cr3::MbeFile::Row dir; cr3BattleTables.direction.find("CommandDirection", a.command, "iiii", dir)) playCr3CameraType(dir.i(3), a, source);
        SDL_Log("battle %.2f s: an avatar's white-in - camera %s", cr3BattleClock, cr3Cam.name.c_str());
    }
    return true;
}

// BtlStealMagicDirection's VUpdate(chunk) 0x98e27c case 5 - the bullets in -: BtlDamageTargetList::PlayStealHitEffect
// 0x980288 on the last entry, the caster's - its recovery popup (green HP, blue MP), BtlDamageTarget::PlayStealHitEffect
// 0x9871ac: the hit effect +0x28 at its point +0x38, x random (+0x58 bit 7, +0x68), x its effect scale (bit 2)
void Viewer::stepCr3StealBack(const cr3::Cr3Action& a, size_t source, float t) {
    auto& play = cr3ActionPlay;
    const auto& timing = play.timing;
    const cr3::Cr3Hit* back = nullptr;
    for (const cr3::Cr3Hit& th : a.targets)
        if (th.stealBack) back = &th;
    if (!back) return;
    while (play.stealShown < back->hits.size() && play.stealShown < timing.hits.size() &&
           t >= timing.hits[play.stealShown] + timing.projectile.duration) {
        const size_t k = play.stealShown++;
        const int hp = -back->hits[k], mp = k < back->mpHits.size() ? -back->mpHits[k] : 0;
        const cr3::Cr3Fighter& f = cr3Rules.fighters[size_t(back->target)];
        int& hpShown = cr3HpShown[size_t(back->target)];
        hpShown = std::clamp(hpShown + hp, 0, f.maxHp);
        if (size_t(back->target) < cr3MpShown.size()) cr3MpShown[size_t(back->target)] = std::clamp(cr3MpShown[size_t(back->target)] + mp, 0, f.maxMp);
        const cr3::Vec3 dp = unitPoint(source, "DummyDP01", 1.2f);
        if (hp != 0) cr3Popups.push_back({hp, dp, cr3BattleClock, 0, 3});
        if (mp != 0) cr3Popups.push_back({mp, hp != 0 ? cr3::Vec3{dp[0], dp[1] - 0.35f, dp[2]} : dp, cr3BattleClock, 0, 4});
        const cr3::Cr3BattleUnit& model = cr3Battle.units[source];
        float scale = timing.scaleToTarget ? model.scale * model.effectScale : 1;
        if (timing.sizeSpread > 0) scale *= 1 + (SDL_randf() * 2 - 1) * timing.sizeSpread;
        spawnCr3Effect(timing.stealEffect, cr3EffectPoint(source, timing.stealPoint), scale);
        SDL_Log("battle %.2f s: a steal back - %d HP, %d MP to fighter %d (effect %d)", cr3BattleClock, hp, mp, back->target, timing.stealEffect);
    }
}

// BtlDamageChunk::VUpdate 0x97a1b0 case 4 - every target list played - for extraType 0xb: BtlFieldEffectManager::Add(0, 5)
// 0x7def18, then Announce 0x7df0e8 - AnnounceBattle 182 "A strange space opened up nearby!" opening it, 183 "The strange
// space returned to normal!" a recast closing it -, 1.5 s
void Viewer::announceCr3FieldToggle(const cr3::Cr3Action& a, float t) {
    if (a.fieldToggle == 0) return;
    const int line = a.fieldToggle > 0 ? 182 : 183;
    setCr3Announce(announceText("AnnounceBattle", line), 1.5f);
    cr3ActionPlay.end = std::max(cr3ActionPlay.end, t + 1.5f);
    SDL_Log("battle %.2f s: the reverse gravity %s - line %d", cr3BattleClock, a.fieldToggle > 0 ? "on" : "off", line);
}

// A command with no actor. BtlAssistDirection (CommandDirection kind 13 -> type 0x25) vPlay 0x9956a0: Pose (the targets'
// skeletons refreshed - nothing here), openAssistAnnounce - the command's name, 2.0 s (BtlAnnounceUI::OpenAssist with more
// than one target; one target's BtlStatusList::OpenAssistAnnounce fills {target}, its duration unsettled - 2.0) -,
// playCameraByType (+0x0c; -1 the chunk's command's - the same here): 6 PlayHealCamera - Cam_bt_all_PC_00 on a player
// target, else PlayMagicToEnemy -, then playSingleEffect on every target at once (+0x2c at its point +0x34: +0x58 bit 2 x
// the unit's effect scale, bit 7 a random scale +0x68). VUpdate 0x9956fc: the chunk's event 1 - the effect's hit frame,
// Effect +0x20 - playKnockout (the popups, the icons, the KOs), event 2 - the effect over - the end.
// BtlAssistWithScriptDirection (kind 40 -> 0x20, vPlay 0x998118) the same: the camera, the effect, then the name. A status
// effect's no-actor command (Regenerate's 306; 1759's kind -1 - no direction object) its name and popups alone
void Viewer::startCr3SourcelessAction(const cr3::Cr3Action& a) {
    if (a.command == 58) { // (BtlGravityInversionDirection::vPlay 0x992bfc: no name - AnnounceBattle 183 for 1.0 s -,
        // playCameraByType's 4 - PlayGlobalCamera -, over at once)
        setCr3Announce(announceText("AnnounceBattle", 183), 1.0f);
        playCr3Camera(pickCr3GlobalCamera(), cr3BattleCam.duration(), false, {}, 0, 0);
        SDL_Log("battle %.2f s: the reverse gravity over - command 58, line 183", cr3BattleClock);
        cr3ActionPlay.directionCommand = a.command;
        cr3ActionPlay.timing.hits = {0.0f};
        cr3ActionPlay.end = 0.1f;
        return;
    }
    std::string text = cr3NameOf("command_name.mbe", "CommandName", a.command);
    if (const size_t at = text.find("{assistant}"); at != std::string::npos) text.replace(at, 11, cr3AssistantName());
    if (const size_t at = text.find("{target}"); at != std::string::npos && a.target >= 0 && size_t(a.target) < cr3Rules.fighters.size())
        text.replace(at, 8, cr3FighterName(cr3Rules.fighters[size_t(a.target)]));
    cr3::MbeFile::Row dir;
    const bool row = cr3BattleTables.direction.find("CommandDirection", a.command, "iiiifii_piiiiifffffffiifff", dir);
    const int kind = row ? dir.i(1) : -1;
    cr3ActionPlay.directionCommand = a.command;
    cr3ActionPlay.kind = kind;
    if (!text.empty()) setCr3Announce(text, kind == 13 || kind == 40 ? 2.0f : 1.5f); // (InterfaceMain::AddBattleAnnouncementMenuMessage)
    float hit = 0.6f, end = 1.6f;
    if ((kind == 13 || kind == 40) && !a.targets.empty()) {
        const size_t first = size_t(cr3Rules.fighters[size_t(a.targets[0].target)].unit);
        const int camera = dir.i(3);
        if (camera == 6 || camera == 7 || camera == 5 || camera == 2) playCr3CameraType(camera, a, first); // (6: the heal camera by the first target's side)
        hit = 0, end = 0.5f;
        cr3::MbeFile::Row fx;
        if (cr3BattleTables.effect.find("Effect", dir.i(9), "i_pppiffiff", fx) && fx.i(7) > 0 && std::isfinite(fx.f(8)))
            hit = std::max(fx.f(8), 0.0f) / 30.0f; // (BtlVisualEffect::initializeEventTimer: its first hit frame - the chunk's event 1)
        const uint32_t flags = uint32_t(dir.i(20));
        for (const cr3::Cr3Hit& th : a.targets) {
            const size_t tu = size_t(cr3Rules.fighters[size_t(th.target)].unit);
            const cr3::Cr3BattleUnit& model = cr3Battle.units[tu];
            float scale = (flags & 4) ? model.scale * model.effectScale : 1;
            if (flags & 0x80) scale *= 1 + (SDL_randf() * 2 - 1) * dir.f(24);
            if (Cr3Fx* fx2 = spawnCr3Effect(dir.i(9), cr3EffectPoint(tu, dir.i(11)), scale)) end = std::max(end, fx2->animLength);
        }
        end = std::max(end, hit + 0.1f);
        SDL_Log("battle %.2f s: an assist direction (kind %d) - command %d, camera %d (%s), effect %d on %zu, its hits at %.2f s", cr3BattleClock, kind,
                a.command, camera, cr3Cam.name.c_str(), dir.i(9), a.targets.size(), double(hit));
    }
    SDL_Log("battle %.2f s: %s - command %d on %zu: \"%s\"", cr3BattleClock, a.assist ? "an assist" : "a command with no actor", a.command, a.targets.size(), text.c_str());
    cr3ActionPlay.timing.hits = {hit};
    cr3ActionPlay.end = end;
}

void Viewer::stepCr3SourcelessAction(const cr3::Cr3Action& a) {
    const float t = cr3BattleClock - cr3ActionStart;
    if (cr3ActionPlay.hitsShown == 0 && t >= cr3ActionPlay.timing.hits.front()) {
        cr3ActionPlay.hitsShown = 1;
        const bool assist = cr3ActionPlay.kind == 13 || cr3ActionPlay.kind == 40; // (their effect already up)
        for (const cr3::Cr3Hit& th : a.targets) {
            const size_t tu = size_t(cr3Rules.fighters[size_t(th.target)].unit);
            const cr3::Vec3 dp = unitPoint(tu, "DummyDP01", 1.2f);
            const int d = th.hits.empty() ? 0 : th.hits[0], mpD = th.mpHits.empty() ? 0 : th.mpHits[0];
            if (d != 0) cr3Popups.push_back({std::abs(d), dp, cr3BattleClock, 0, d < 0 ? 3 : 0});
            if (mpD != 0) cr3Popups.push_back({std::abs(mpD), d != 0 ? cr3::Vec3{dp[0], dp[1] - 0.35f, dp[2]} : dp, cr3BattleClock, 0, 4});
            if (!assist) cr3HitEffects(a.command, 0, tu, false);
            cr3HpShown[size_t(th.target)] = th.hpAfter;
            if (size_t(th.target) < cr3MpShown.size()) cr3MpShown[size_t(th.target)] = th.mpAfter;
            if (th.hpAfter > 0 && cr3BattleUnits[tu].knockedOut) restoreCr3Unit(tu); // (a revive)
            else if (th.killed && !cr3BattleUnits[tu].knockedOut) rollCr3Drop(th.target), knockOutCr3Unit(tu);
        }
        announceCr3Effects(a);
        commentCr3AfterDamage(a);
    }
    if (t < cr3ActionPlay.end) return;
    showCr3ActionState(a);
    cr3GaugeShown = a.gaugeAfter;
    ++cr3ActionAt;
    closeCr3Totals();
    // (no CallDirectionEnd: the scripts' BattleArgument source would be no unit)
    startCr3Action();
}

bool Viewer::startCr3Opening(const std::vector<std::pair<int, int>>& commands) {
    cr3Opening = Cr3Opening();
    for (int group = 0; group < 2; ++group) { // (Add: 0 the players', 1 the monsters' - 2, no status, none here)
        std::vector<std::pair<int, std::vector<int>>> units; // (fighter, its commands - one an effect: Param::Add)
        for (const auto& [fi, cmd] : commands) {
            if (fi < 0 || size_t(fi) >= cr3Rules.fighters.size() || cr3Rules.fighters[size_t(fi)].player != (group == 0)) continue;
            auto it = std::find_if(units.begin(), units.end(), [&](const auto& u) { return u.first == fi; });
            if (it == units.end()) units.push_back({fi, {}}), it = units.end() - 1;
            const auto effectOf = [&](int c) {
                cr3::MbeFile::Row d;
                return cr3BattleTables.direction.find("CommandDirection", c, "iiiifii_piiii", d) ? d.i(9) : -1;
            };
            if (std::none_of(it->second.begin(), it->second.end(), [&](int c) { return effectOf(c) == effectOf(cmd); })) it->second.push_back(cmd);
        }
        for (size_t round = 0;; ++round) {
            std::vector<std::pair<int, int>> r;
            for (const auto& [fi, cmds] : units)
                if (round < cmds.size()) r.push_back({fi, cmds[round]});
            if (r.empty()) break;
            cr3Opening.rounds.push_back(r), cr3Opening.roundGroup.push_back(group);
        }
    }
    if (cr3Opening.rounds.empty()) return false;
    cr3Opening.on = true, cr3Opening.at = 0, cr3Opening.roundEnd = -1;
    cr3Choices.clear(), cr3Actions.clear(), cr3ActionAt = 0;
    cr3BattleStage = Cr3BattleStage::Action, cr3StageStart = cr3BattleClock;
    SDL_Log("battle %.2f s: the opening - %zu commands, %zu rounds", cr3BattleClock, commands.size(), cr3Opening.rounds.size());
    return true;
}

void Viewer::stepCr3Opening() {
    Cr3Opening& o = cr3Opening;
    if (o.roundEnd >= 0 && cr3BattleClock < o.roundEnd) return;
    if (o.roundEnd >= 0) ++o.at;
    if (o.at >= o.rounds.size()) { // (End; the menu's turn)
        o = Cr3Opening();
        SDL_Log("battle %.2f s: the opening over", cr3BattleClock);
        return beginCr3Turn(false);
    }
    const int group = o.roundGroup[o.at];
    if (o.at == 0 || o.roundGroup[o.at - 1] != group) // (a group's Play: PlayMagicToPlayer / PlayMagicToEnemy)
        playCr3Camera(group == 0 ? "Cam_bt_all_PC_00" : cr3Battle.cameras[7], cr3BattleCam.duration(), false, {}, 0, 0);
    float longest = 0.5f;
    std::string line;
    for (const auto& [fi, cmd] : o.rounds[o.at]) {
        const cr3::Cr3Fighter& f = cr3Rules.fighters[size_t(fi)];
        if (f.unit < 0 || size_t(f.unit) >= cr3BattleUnits.size()) continue;
        cr3::MbeFile::Row dir;
        if (!cr3BattleTables.direction.find("CommandDirection", cmd, "iiiifii_piiiiifffffffiifffi", dir)) continue;
        if (Cr3Fx* fx = spawnCr3Effect(dir.i(9), cr3EffectPoint(size_t(f.unit), dir.i(12)), 1)) longest = std::max(longest, fx->animLength);
        if (size_t(fi) < cr3EffectsShown.size()) { // (the effects its skills gave shown with it)
            cr3EffectsShown[size_t(fi)].clear();
            for (const auto& e : f.effects) cr3EffectsShown[size_t(fi)].push_back(e.first);
        }
        line += " " + std::to_string(fi) + ":" + std::to_string(cmd) + " (effect " + std::to_string(dir.i(9)) + ", voice " + std::to_string(dir.i(25)) + ")";
    }
    o.roundEnd = cr3BattleClock + longest; // (VUpdate: every effect of the round at its event 5)
    SDL_Log("battle %.2f s: the opening's round %zu (group %d, camera %s):%s", cr3BattleClock, o.at, group, cr3Cam.name.c_str(), line.c_str());
}

std::string Viewer::cr3AssistantName() {
    std::vector<std::string> names;
    if (const cr3::MbeFile::Table* t = cr3BattleTables.battle.table("Assistant"))
        for (size_t r = 0; r < t->rows.size() && r < 8; ++r) {
            cr3::MbeFile::Row row;
            if (!cr3BattleTables.battle.row(*t, r, "i_ppi", row)) continue;
            const int flag = row.i(3); // (+0x18: Patty's -1, Marco .. Haruko 5101 .. 5105)
            if (flag != -1 && !game.flag(flag)) continue;
            names.push_back(optionLanguage == 0 || row.s(2).empty() ? row.s(1) : row.s(2));
        }
    if (names.empty()) return {};
    // (std::shuffle with uniform_int - the device's, not CrxRandom: the cameras' stream, seeded from the battle's seed)
    return names[std::uniform_int_distribution<size_t>(0, names.size() - 1)(cr3CamRng)];
}

void Viewer::endCr3Actions() {
    if (cr3ForcedEnd == 3) { // (Battle.ForceEnd: over - no victory, no result)
        SDL_Log("battle %d: ended by its script", cr3Battle.coupling);
        cr3ForcedEnd = -1;
        return endCr3Battle();
    }
    const int outcome = cr3ForcedEnd == 0 ? 1 : cr3Rules.outcome(); // (Battle.ForceVictory)
    cr3ForcedEnd = -1;
    closeCr3Totals(); // (phaseCheck 0x009cfaa0, phaseVictoryEventStart, phaseDefeatEventStart: BtlTotalDamageUI::Close)
    if (outcome != 0) stopCr3Auto(outcome == 1 ? "won" : "lost"); // (BtlSysStateAction::phaseCheck 0x9cfaa0: StopAuto)
    if (outcome != 0) // (InterfaceMain::SaveCharacterMp: what the party has left kept)
        for (const cr3::Cr3Fighter& f : cr3Rules.fighters)
            if (f.player) {
                if (f.mp >= f.maxMp) game.cr3Mp.erase(f.id);
                else game.cr3Mp[f.id] = f.mp;
            }
    if (outcome == 1) { // (BtlSysStateAction::phaseVictoryEventStart: Battle_Victory_Base)
        cr3BattleStage = Cr3BattleStage::Won, cr3StageStart = cr3BattleClock, cr3VictoryIn = false;
        cr3ScriptCamera = false;
        runCr3BattleScript("Battle_Victory_Base");
        for (size_t k = 0; k < cr3Battle.units.size() && !cr3ScriptCamera; ++k) // (a boss's death camera kept)
            if (cr3Battle.units[k].player) {
                playCr3Camera(cr3Battle.cameras[7], 4.0f, false, {0, 0, 0}, 0, 0); // (the all camera)
                break;
            }
        SDL_Log("battle %d: won", cr3Battle.coupling);
    } else if (outcome == 2) {
        // (phaseDefeatEventStart: the result 1, Battle_Defeat_Base; its commands - flag 5053's 72 - played: not lost, the
        // battle on through the turn's end; a fall again after them lost)
        if (!cr3DefeatPlayed) {
            cr3DefeatPlayed = true;
            runCr3BattleScript("Battle_Defeat_Base");
            freezeCr3Shown();
            if (playCr3TurnEndActions(cr3Rules.scriptCommands(std::exchange(cr3ScriptCommands, {})))) return;
        }
        cr3BattleStage = Cr3BattleStage::Lost, cr3StageStart = cr3BattleClock;
        cr3BattleLost = true;
        SDL_Log("battle %d: lost", cr3Battle.coupling);
    } else {
        if (cr3TurnEndStep == 3) return beginCr3Turn(); // (a new wave's script's commands played)
        if (cr3TurnEndStep == 0) { // (phaseCallTurnEndEvent: Battle_Turn_End_Base; case 0x12 - its commands played)
            cr3TurnEndStep = 1;
            runCr3BattleScript("Battle_Turn_End_Base");
            freezeCr3Shown();
            if (playCr3TurnEndActions(cr3Rules.scriptCommands(std::exchange(cr3ScriptCommands, {})))) return;
        }
        if (cr3TurnEndStep == 1) {
            cr3TurnEndStep = 2;
            // (VS: a type-1 lifeline item's command at the turn's end - 79, Format: the party's passing effects cured;
            // BtlBoostItem::AddTurnEndCommand 0x7d7364 - phaseCalculate's, its effect not seen)
            if (vs.screen == 5)
                for (const VsBoost& b : vsBoostTable())
                    if (b.type == 1 && std::find(vs.boosts.begin(), vs.boosts.end(), b.id) != vs.boosts.end())
                        for (cr3::Cr3Fighter& f : cr3Rules.fighters)
                            if (f.player)
                                f.effects.erase(std::remove_if(f.effects.begin(), f.effects.end(), [](const std::pair<int, int>& e) { return e.second >= 0; }),
                                                f.effects.end());
            // (phaseStatusEffect: the turn-end list - trigger 12's kind 100 / 101 commands - not empty: ShowByProcessingList,
            // PlayGetSet, CalculateTurnEnd - played)
            freezeCr3Shown();
            std::vector<cr3::Cr3Action> list = cr3Rules.turnEndActions();
            if (!list.empty()) {
                playCr3Camera(cr3Battle.cameras[0], cr3Battle.getSetEnd, false, {0, 0, 0}, 0, 0);
                SDL_Log("battle %.2f s: the turn-end list - %zu commands", cr3BattleClock, list.size());
                if (playCr3TurnEndActions(std::move(list))) return;
            }
        }
        // (then UpdateStatusEffectToCalculation - the turns -, end timing 11, the field effects' step: the poison's and the
        // regeneration's HP changes - command 55's - as numbers)
        cr3TurnEndStep = 0;
        for (const auto& [fi, change] : cr3Rules.endTurn()) {
            const size_t tu = size_t(cr3Rules.fighters[size_t(fi)].unit);
            cr3HpShown[size_t(fi)] = cr3Rules.fighters[size_t(fi)].hp;
            cr3Popups.push_back({std::abs(change), unitPoint(tu, "DummyDP01", 1.2f), cr3BattleClock, 0, change > 0 ? 3 : 0});
        }
        beginCr3Turn();
    }
}

bool Viewer::playCr3TurnEndActions(std::vector<cr3::Cr3Action> actions) {
    if (actions.empty()) return false;
    cr3Actions = std::move(actions), cr3ActionAt = 0;
    cr3BattleStage = Cr3BattleStage::Action, cr3StageStart = cr3BattleClock;
    startCr3Action();
    return true;
}

// the cards' values (BattleResultV2Menu, CreateBattleResultMenu 0x8a080c: before any EXP - the gene's
// level, its bar 1 - CrxGene::GetExpRate) and what each gene is to get (state 5: the first three
// slots' standing ones the whole, the rest and the fallen half); nothing given yet
void Viewer::giveCr3GeneExp() {
    cr3ResultCards.clear();
    cr3ResultPhase = 0, cr3ResultPhaseAt = cr3BattleClock, cr3ResultApplied = cr3ResultNew = false, cr3GeneUp = Cr3GeneUp();
    for (int slot = 0; slot < 5; ++slot) {
        const int ch = game.cr3Party[slot];
        if (ch <= 0) continue;
        cr3::MbeFile::Row charRow;
        if (cr3TablesReady() && cr3BattleTables.character.find("Char", ch, "iiii", charRow) && (charRow.i(3) & 1) != 0) continue; // (Guest: no result card / gene EXP)
        bool standing = false;
        for (const cr3::Cr3Fighter& f : cr3Rules.fighters)
            if (f.player && f.id == ch) standing = !f.dead();
        Cr3ResultCard card;
        card.ch = ch, card.gain = slot < 3 && standing ? cr3ResultExp : cr3ResultExp / 2;
        const int gi = game.wornGene(ch);
        const cr3::Cr3OfflineDb::GeneInfo* info = gi >= 0 && game.cr3Db ? game.cr3Db->geneInfo(game.cr3Genes[size_t(gi)].master) : nullptr;
        if (info) {
            const cr3::Cr3Gene& g = game.cr3Genes[size_t(gi)];
            card.gene = gi, card.master = g.master, card.maxLevel = info->maxLevel;
            card.oldLevel = card.level = game.cr3Db->geneLevel(g);
            const int at = game.cr3Db->geneExp(g.master, card.level), next = game.cr3Db->geneExp(g.master, card.level + 1);
            card.oldProgress = card.progress = card.level >= info->maxLevel || next <= at ? 0.0f : float(g.exp - at) / float(next - at);
        }
        cr3ResultCards.push_back(card);
    }
}

// state 5 (BattleResultV2Menu::Update): each gene's AddTemporaryExp + SaveExp - its new commands and
// skills queued, not learnt (the windows' CommandSkillLoopSub2 gives them their slots) -; no level
// up anywhere: com_022, the bars jump (SetLevelPer), on to the drops; else the cards out (state 6)
void Viewer::applyCr3GeneExp() {
    cr3ResultApplied = true;
    bool anyUp = false;
    for (Cr3ResultCard& card : cr3ResultCards) {
        if (card.gene < 0 || size_t(card.gene) >= game.cr3Genes.size() || !game.cr3Db) continue;
        cr3::Cr3Fighter oldStats;
        const bool hadStats = cr3CharaStats(card.ch, oldStats);
        cr3::Cr3Gene& g = game.cr3Genes[size_t(card.gene)];
        game.cr3Db->addGeneExp(g, card.gain, &card.learnt); // (held at its top)
        card.level = game.cr3Db->geneLevel(g);
        const int at = game.cr3Db->geneExp(g.master, card.level), next = game.cr3Db->geneExp(g.master, card.level + 1);
        card.progress = card.level >= card.maxLevel || next <= at ? 0.0f : float(g.exp - at) / float(next - at);
        card.up = card.level > card.oldLevel;
        if (card.up && hadStats && game.cr3Mp.count(card.ch)) { // (CrxCharacter::RecoverBonusMP @ 0x008f61b8)
            cr3::Cr3Fighter newStats;
            if (cr3CharaStats(card.ch, newStats) && newStats.maxMp > oldStats.maxMp) {
                const int nextMp = game.cr3Mp[card.ch] + (newStats.maxMp - oldStats.maxMp);
                if (nextMp >= newStats.maxMp) game.cr3Mp.erase(card.ch);
                else game.cr3Mp[card.ch] = nextMp;
            }
        }
        anyUp |= card.up;
        SDL_Log("battle %d: character %d's gene %d +%d EXP - level %d%s", cr3Battle.coupling, card.ch, g.master, card.gain, card.level,
                card.up ? " (up)" : "");
    }
    cr3ResultPhaseAt = cr3BattleClock;
    if (!anyUp) {
        audio.playSe("com_022", false);
        cr3ResultNew = true, cr3ResultPhase = 4;
        SDL_Log("battle: result - the EXP in, no level up");
    } else {
        cr3ResultPhase = 1; // (state 6: btRsltCharaPanel_out, btRsltCharaMenu_out, the menus faded out)
        SDL_Log("battle: result - the EXP in, the cards out");
    }
}

// a command's or skill's name (or help): MbCommandInfo / MbSkillInfo's
std::string Viewer::cr3SkillName(int type, int id, bool help) {
    if (type == 1) return help ? cr3NameOf("skill_help.mbe", "SkillHelp", id) : cr3NameOf("skill_name.mbe", "SkillName", id);
    return help ? cr3NameOf("command_help.mbe", "CommandHelp", id) : cr3NameOf("command_name.mbe", "CommandName", id);
}

// the result's states past the tally: 5 the EXP 1/3 s after card 0's btRsltCharaPanel_in (0.5667 s),
// 7 the cards' out (0.6333 s), 8 .. 0xd the windows, 0xe the cards' in again (0.5667 s)
void Viewer::stepCr3Result() {
    const float since = cr3BattleClock - cr3ResultPhaseAt;
    if (cr3ResultPhase == 0 && !cr3ResultApplied && cr3BattleClock - cr3StageStart >= cr3ResultLead() + 0.5667f + 1.0f / 3) applyCr3GeneExp();
    else if (cr3ResultPhase == 1 && since >= 0.6333f) openCr3GeneUp(0);
    else if (cr3ResultPhase == 2) stepCr3GeneUp();
    else if (cr3ResultPhase == 3 && since >= 0.5667f) {
        cr3ResultPhase = 4, cr3ResultPhaseAt = cr3BattleClock; // (state 0xe -> 0xf: the drops)
        SDL_Log("battle: result - the cards back");
    }
}

// state 8: the next levelled gene from card `fromCard` - CreateGeneLevelUpMenu, SetGeneGeneLevelUpMenu
// (its card); none left: SetResultSecondSetteing (every card its new level and bar), the cards in
void Viewer::openCr3GeneUp(int fromCard) {
    for (int k = std::max(fromCard, 0); k < int(cr3ResultCards.size()); ++k)
        if (cr3ResultCards[size_t(k)].up) {
            cr3GeneUp = Cr3GeneUp();
            cr3GeneUp.card = k, cr3GeneUp.at = cr3BattleClock;
            cr3ResultPhase = 2, cr3ResultPhaseAt = cr3BattleClock;
            SDL_Log("battle: gene level up - card %d (gene %d, level %d to %d, %zu learnt)", k, cr3ResultCards[size_t(k)].master,
                    cr3ResultCards[size_t(k)].oldLevel, cr3ResultCards[size_t(k)].level, cr3ResultCards[size_t(k)].learnt.size());
            return;
        }
    cr3GeneUp = Cr3GeneUp();
    cr3ResultNew = true, cr3ResultPhase = 3, cr3ResultPhaseAt = cr3BattleClock;
    SDL_Log("battle: result - the cards in again");
}

// GeneLvUpMenu (0xb1a740) driven by BattleResultV2Menu states 9 .. 0xd
void Viewer::stepCr3GeneUp() {
    Cr3GeneUp& u = cr3GeneUp;
    if (u.card < 0 || size_t(u.card) >= cr3ResultCards.size()) return openCr3GeneUp(0);
    Cr3ResultCard& card = cr3ResultCards[size_t(u.card)];
    if (card.gene < 0 || size_t(card.gene) >= game.cr3Genes.size()) return openCr3GeneUp(u.card + 1);
    cr3::Cr3Gene& g = game.cr3Genes[size_t(card.gene)];
    const float s = cr3BattleClock - u.at;
    const std::string geneName = cr3NameOf("gene_name.mbe", "GeneName", g.master);
    auto put = [](std::string text, const char* key, const std::string& value) {
        if (const size_t at = text.find(key); at != std::string::npos) text.replace(at, std::strlen(key), value);
        return text;
    };
    auto alert = [&](int id, std::string text) { // (CreateAlertWindwoMenu(false), Cr3UtilNPrint's lines)
        u.alert = true, u.text = std::move(text), u.alertAt = cr3BattleClock;
        SDL_Log("battle: gene alert %#x - %s", id, u.text.c_str());
    };
    const size_t n = card.learnt.size();
    switch (u.step) {
    case 0: // (state 9: the window's in-animation over - IsAnimeEnd)
        if (s < 0.1667f) break;
        {
            const int gained = card.level - card.oldLevel;
            std::string text = put(menuAnnounce(gained < 2 ? 0x135 : 0x122), "{genename}", geneName);
            text = put(put(text, "{oldlevel}", std::to_string(card.oldLevel)), "{newlevel}", std::to_string(card.level));
            alert(gained < 2 ? 0x135 : 0x122, text);
        }
        u.lvAt = cr3BattleClock, u.step = 1, u.at = cr3BattleClock; // (StartLevelUpGeneLevelUpMenu: btRsltLvUpMenu's 3.3 s, com_018)
        audio.playSe("com_018", false);
        break;
    case 1: // (state 10: its animation over and the alert tapped away)
        if (!u.alert && cr3BattleClock - u.lvAt >= 3.3f) u.step = 2, u.entry = 0, u.at = cr3BattleClock;
        break;
    case 2: // (CommandSkillLoopSub: each new one not already in slots 0 .. 5 - 0x181, StartCommandSkillUp: btRsltSkillMenu, com_021)
        if (u.alert || cr3BattleClock - u.skillAt < 3.3f) break;
        while (size_t(u.entry) < n && g.hasOwn(card.learnt[size_t(u.entry)][0], card.learnt[size_t(u.entry)][1])) ++u.entry;
        if (size_t(u.entry) < n) {
            const auto& e = card.learnt[size_t(u.entry)];
            alert(0x181, put(menuAnnounce(0x181), "{skillname}", cr3SkillName(e[0], e[1])));
            u.skillAt = cr3BattleClock, ++u.entry;
            audio.playSe("com_021", false);
        } else u.step = 3, u.entry = 0, u.at = cr3BattleClock;
        break;
    case 3: // (state 0xb, CommandSkillLoopSub2: a free own slot - AddTemporaryLearnedCommandSkill -, else the replace prompt)
        while (size_t(u.entry) < n) {
            const auto& e = card.learnt[size_t(u.entry)];
            if (g.hasOwn(e[0], e[1])) {
                ++u.entry;
                continue;
            }
            const int slot = g.freeOwnSlot();
            if (slot < 0) { // (SelectCommandSkill: GeneLvUpMenu states 1 .. 9)
                alert(0x1cc, menuAnnounce(0x1cc));
                u.step = 4, u.at = cr3BattleClock;
                return;
            }
            g.slots[slot][0] = e[0], g.slots[slot][1] = e[1];
            SDL_Log("battle: gene %d learnt %d:%d into slot %d", g.master, e[0], e[1], slot);
            ++u.entry;
        }
        u.step = 9, u.at = cr3BattleClock; // (SaveExp, SaveCommandSkill, OutDeleteGeneLevelUpMenu)
        SDL_Log("battle: gene %d's window out", g.master);
        break;
    case 4: // (the 0x1cc alert tapped: state 3's slide)
        if (!u.alert) u.step = 5, u.at = cr3BattleClock;
        break;
    case 5: // (btRsltUpPositon x -0.5, y -0.1 an update, 5 updates - SetCommmandSkillListSub)
        if (s >= 5.0f / 30) {
            u.step = 6, u.focus = 0, u.at = cr3BattleClock;
            SDL_Log("battle: gene %d - the replace list", g.master);
        }
        break;
    case 8: // (slid back: x +0.5 to 0)
        if (s >= 5.0f / 30) u.step = 3, u.at = cr3BattleClock;
        break;
    case 9: // (btRsltUpPositon_out over - CheckAlive false; state 0xc: the next gene)
        if (s >= 0.1667f) openCr3GeneUp(u.card + 1);
        break;
    default: break; // (6 / 7: the list and its question wait on a choice)
    }
}

// the replace list's row (BuildCommmandSkillList: slots 0 .. 5, the new one 10th - id 10) tapped:
// the list out, SetBasicYNChoiceWindow 0x1cd ({genename} will forget {csname})
void Viewer::chooseCr3GeneRow(int row) {
    Cr3GeneUp& u = cr3GeneUp;
    if (cr3ResultPhase != 2 || u.step != 6 || row < 0 || row > 6) return;
    u.focus = row, u.step = 7, u.askFocus = 0, u.at = cr3BattleClock;
    SDL_Log("battle: gene replace row %d - asked", row);
}

// the question's answer: No (com_004) the list again; Yes (com_003) - an old row forgotten, the new
// one into its slot (DeleteTemporaryCommandSkillId, AddTemporaryLearnedCommandSkill); the new row
// itself - not learnt (EndSelectCommandSkill 2); then the window slid back
void Viewer::answerCr3GeneAsk(bool yes) {
    Cr3GeneUp& u = cr3GeneUp;
    if (cr3ResultPhase != 2 || u.step != 7 || u.card < 0 || size_t(u.card) >= cr3ResultCards.size()) return;
    Cr3ResultCard& card = cr3ResultCards[size_t(u.card)];
    if (!yes) {
        audio.playSe("com_004", false);
        u.step = 6, u.at = cr3BattleClock;
        SDL_Log("battle: gene replace - no");
        return;
    }
    audio.playSe("com_003", false);
    if (card.gene >= 0 && size_t(card.gene) < game.cr3Genes.size() && size_t(u.entry) < card.learnt.size()) {
        cr3::Cr3Gene& g = game.cr3Genes[size_t(card.gene)];
        const auto& e = card.learnt[size_t(u.entry)];
        if (u.focus < 6) {
            SDL_Log("battle: gene %d forgot %d:%d for %d:%d (slot %d)", g.master, g.slots[u.focus][0], g.slots[u.focus][1], e[0], e[1], u.focus);
            g.slots[u.focus][0] = e[0], g.slots[u.focus][1] = e[1];
        } else SDL_Log("battle: gene %d - %d:%d not learnt", g.master, e[0], e[1]);
    }
    ++u.entry;
    u.step = 8, u.at = cr3BattleClock;
}

// the pad / keys over the window: the list's ↑ / ↓ (stopping at its ends), ○ its row; the question's
// arrows between Yes and No, ○ the one under the cursor, × No. No back on the list (state 4: no
// GetTapListBackID)
bool Viewer::cr3GeneUpKey(SDL_Keycode k) {
    Cr3GeneUp& u = cr3GeneUp;
    if (cr3ResultPhase != 2 || (u.step != 6 && u.step != 7) || cr3BattleClock - u.at < 0.2f) return false;
    const bool enter = k == SDLK_RETURN || k == SDLK_KP_ENTER;
    if (u.step == 6) {
        if (k == SDLK_UP || k == SDLK_DOWN) {
            const int to = std::clamp(u.focus + (k == SDLK_UP ? -1 : 1), 0, 6);
            if (to != u.focus) u.focus = to, audio.playSe("com_002", false), SDL_Log("battle: gene replace cursor %d", to);
        } else if (enter) chooseCr3GeneRow(u.focus);
    } else {
        if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT) u.askFocus ^= 1, audio.playSe("com_002", false);
        else if (enter) answerCr3GeneAsk(u.askFocus == 0);
        else if (k == SDLK_BACKSPACE || k == SDLK_ESCAPE) answerCr3GeneAsk(false);
    }
    return true;
}

// --input auto over the result: each alert tapped a second on (after its animation's 3.3 s), the
// replace list's new row chosen (not learnt) and Yes; then the drops' lines a second each
void Viewer::autoCr3Result() {
    if (cr3ResultPhase == 4) {
        const bool lineUp = !cr3ResultLine.empty() && cr3BattleClock - cr3ResultLineAt < 1.0f;
        if (!lineUp && !showCr3DropLine()) endCr3Battle();
        return;
    }
    if (cr3ResultPhase != 2) return;
    Cr3GeneUp& u = cr3GeneUp;
    if (u.alert && cr3BattleClock - u.alertAt > 1.0f) cr3BattleTap();
    else if (u.step == 6 && cr3BattleClock - u.at > 0.6f) chooseCr3GeneRow(6);
    else if (u.step == 7 && cr3BattleClock - u.at > 0.6f) answerCr3GeneAsk(true);
}

// the result ended before its windows (--input result): the EXP in, the rest given as they fit,
// a full gene's new ones not learnt
void Viewer::settleCr3GeneQueues() {
    if (!cr3ResultApplied) applyCr3GeneExp();
    for (size_t k = 0; k < cr3ResultCards.size(); ++k) {
        Cr3ResultCard& card = cr3ResultCards[k];
        if (card.gene < 0 || size_t(card.gene) >= game.cr3Genes.size()) continue;
        cr3::Cr3Gene& g = game.cr3Genes[size_t(card.gene)];
        size_t from = 0;
        if (cr3ResultPhase == 2 && int(k) == cr3GeneUp.card && cr3GeneUp.step >= 3) from = size_t(cr3GeneUp.entry);
        else if (cr3ResultPhase >= 3 || (cr3ResultPhase == 2 && int(k) < cr3GeneUp.card)) from = card.learnt.size();
        for (size_t e = from; e < card.learnt.size(); ++e) {
            const int type = card.learnt[e][0], id = card.learnt[e][1], slot = g.freeOwnSlot();
            if (g.hasOwn(type, id) || slot < 0) continue;
            g.slots[slot][0] = type, g.slots[slot][1] = id;
        }
        card.learnt.clear();
    }
    cr3GeneUp = Cr3GeneUp();
    cr3ResultNew = true, cr3ResultPhase = 4;
}

void Viewer::acquireCr3Remuneration() {
    // (the dead at the end - IsDead, kind 76: an escaped Mimic pays nothing, its stolen gold kept -, and the fallen a call
    // for help replaced, banked as it came - VRainforceToCalculation's VAcquireExperience / VAcquireMoney)
    std::vector<cr3::Cr3Fighter> paying = cr3Rules.fighters;
    paying.insert(paying.end(), cr3Rules.banked.begin(), cr3Rules.banked.end());
    cr3Rules.banked.clear();
    int levels = 0, monsters = 0, geneTop = 1;
    for (const cr3::Cr3Fighter& f : paying)
        if (f.player) geneTop = std::max(geneTop, f.geneLevel);
        else levels += f.level, ++monsters;
    const float gap = std::clamp(1.0f + float((monsters ? levels / monsters : 0) - geneTop) * 0.02f, 0.5f, 2.0f);
    for (const cr3::Cr3Fighter& f : paying) {
        if (f.player || !f.dead()) continue;
        cr3::MbeFile::Row r;
        if (!cr3BattleTables.monster.find("Monster", f.id, "iipiiiiiiiiiiiiii", r)) continue;
        const int exp = r.i(14) + (f.level - 1) * int(int16_t(uint32_t(r.i(16)) & 0xffff));
        if (exp >= 1) cr3WaveExp += std::max(int(gap * float(exp)), 1);
        cr3WaveMoney += r.i(15) + (f.level - 1) * int(int16_t(uint32_t(r.i(16)) >> 16));
        // (BtlMonsterStatus::VAcquireMoney 0x9c6c0c: a thief - skill kind 0x74 - gives back what it stole x
        // its constants / 100 - GetGoldRelase 0x9b0be4)
        if (const std::vector<int> back = cr3Rules.skillConstants(f, 0x74); !back.empty() && f.stolenGold > 0) {
            float k = 1;
            for (int c : back) k *= float(c) / 100;
            cr3WaveMoney += int(k * float(f.stolenGold));
        }
    }
    for (const auto& [fi, item] : cr3Drops)
        if (item > 0 && cr3DropsAnnounced.count(fi)) cr3WaveDrops.push_back(item);
    cr3Drops.clear(), cr3DropsAnnounced.clear();
}

void Viewer::enterCr3Result() {
    acquireCr3Remuneration();
    if (vs.result && game.cr3Db && vs.chosen >= 0 && size_t(vs.chosen) < game.cr3Db->vsTournaments().size()) {
        // (EntryResult 0x9a42ec, GameMain +0x24b: the tournament row's exp / prize_gold assigned - every battle's
        // monsters' not counted -; the chain after it as any result's; all its battles' drops)
        const cr3::Cr3OfflineDb::VsTournament& tt = game.cr3Db->vsTournaments()[size_t(vs.chosen)];
        cr3WaveExp = tt.exp, cr3WaveMoney = tt.prizeGold;
        cr3WaveDrops.insert(cr3WaveDrops.begin(), vs.drops.begin(), vs.drops.end());
        vs.drops.clear();
    }
    cr3ResultExp = cr3WaveExp, cr3ResultMoney = cr3WaveMoney;
    // (BtlInterfaceSystem::EntryResult: x (1 + every unit's skills' kind 0x6c constants / 100) - the
    // money's 0x6d)
    float expBy = 1, moneyBy = 1;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters) {
        for (int c : cr3Rules.skillConstants(f, 0x6c)) expBy += float(c) / 100;
        for (int c : cr3Rules.skillConstants(f, 0x6d)) moneyBy += float(c) / 100;
    }
    // (BtlResult::GetExperience: (+4 - the monsters', what scripts added) x its factor - +0x28, the
    // scripts' SetExperienceFactor; the Apples' below -; the money's likewise)
    // (BtlResult::GetExperience 0x7df710: x the Apples used - each distinct one's constant, EXP extraType 14 / money 15 -
    // x the scripts' factor; then EntryResult 0x9a42ec's skill factor; then, VIP, the result screen's vipbonus
    // exp_bonus / gold_bonus - 0x8a080c, BtlResultMenu state 3)
    int apples[2] = {1, 1};
    for (int k = 0; k < 2; ++k)
        for (int cmd : cr3UsedApples[k])
            if (cr3::Cr3Command ac; cr3Rules.command(cmd, ac) && !ac.extraConstants.empty() && ac.extraConstants[0] > 0) apples[k] *= ac.extraConstants[0];
    cr3ResultExp = (cr3ResultExp + cr3ResultAdd[0]) * apples[0] * cr3ResultFactor[0], cr3ResultMoney = (cr3ResultMoney + cr3ResultAdd[1]) * apples[1] * cr3ResultFactor[1];
    cr3ResultExp = int(expBy * float(cr3ResultExp)), cr3ResultMoney = int(moneyBy * float(cr3ResultMoney));
    cr3ResultVip = game.cr3VipDays >= 1 && game.cr3Db;
    cr3ResultBase[0] = cr3ResultExp, cr3ResultBase[1] = cr3ResultMoney;
    if (cr3ResultVip) {
        const cr3::Cr3OfflineDb::Month& m = game.cr3Db->month();
        cr3ResultVipBy[0] = m.expBonus, cr3ResultVipBy[1] = m.goldBonus;
        SDL_Log("battle: result - VIP: the base %d EXP, %d money first", cr3ResultBase[0], cr3ResultBase[1]);
        cr3ResultExp = int(float(cr3ResultExp) * m.expBonus), cr3ResultMoney = int(float(cr3ResultMoney) * m.goldBonus);
        SDL_Log("battle: VIP x%.2f EXP, x%.2f money", double(m.expBonus), double(m.goldBonus));
    }
    const int64_t goldMax = game.cr3Db ? game.cr3Db->constant("GOLD_MAX", 9999999) : 9999999;
    game.cr3Money = int(std::clamp<int64_t>(int64_t(game.cr3Money) + cr3ResultMoney, 0, goldMax));
    giveCr3GeneExp();
    cr3ResultDropLines.clear();
    for (int item : cr3WaveDrops) { // (VAcquireDrop / SetDropItemText 0xb1ffe4: one line each, up to 16 - 0xfb got, 0xfc the bag full)
        const bool full = 99 - game.cr3Items[item] < 1;
        if (cr3ResultDropLines.size() < 16) cr3ResultDropLines.push_back(full ? -item : item);
        if (!full) game.cr3Items[item] += 1;
        SDL_Log("battle %d: item %d %s", cr3Battle.coupling, item, full ? "not taken - the bag full" : "into the bag");
    }
    // (SetDropItemText's season line, after the drops and outside their 16: a VS tournament's result where it may drop the
    // season's token, flag 33 set and EndVSTournament gave the season a point - vsseasonscorerewards' itemId, one)
    if (vs.result && game.cr3Db && game.cr3Vs.seasonDrop && game.flag(33) && game.cr3Db->vsRewards().seasonPoint) {
        const int token = game.cr3Db->month().seasonItem;
        if (token > 0 && !cr3ItemText(token, false).empty()) {
            const bool full = 99 - game.cr3Items[token] < 1;
            cr3ResultDropLines.push_back(full ? -token : token);
            if (!full) game.cr3Items[token] += 1;
            SDL_Log("battle %d: the season's token %d %s", cr3Battle.coupling, token, full ? "not taken - the bag full" : "into the bag");
        }
    }
    if (game.next.battleMusicAfter >= 0) audio.stopBgm(30); // (phaseResult: FadeBgm, a second at 30 updates/s)
    cr3BattleStage = Cr3BattleStage::Result, cr3StageStart = cr3BattleClock;
    // (BattleResultV2Menu::SetParamNumber 0xb20b98 case 7, as CreateBattleResultMenu makes it: the Information bar's
    // "You won the battle!" - every result's, there till the drops' lines)
    cr3ResultLine = menuAnnounce(0x121), cr3ResultLineAt = cr3BattleClock;
    SDL_Log("battle %d: result - %d EXP, %d money", cr3Battle.coupling, cr3ResultExp, cr3ResultMoney);
}

// the keys: up / down the focus, Return chooses, Backspace back from the targets
bool Viewer::cr3BattleKey(SDL_Keycode k) {
    if (cr3ContinueKey(k)) return true;
    if (cr3BattleStage == Cr3BattleStage::Result && cr3GeneUpKey(k)) return true;
    if (cr3BattleEvents && cr3BattleEvents->running()) return false;
    const bool enter = k == SDLK_RETURN || k == SDLK_KP_ENTER;
    if (enter && (cr3BattleStage == Cr3BattleStage::Won || cr3BattleStage == Cr3BattleStage::Result)) return cr3BattleTap(), true;
    if (enter && tapCr3Analyze()) return true;
    // (the marker up: the Vita's × - FUN_8112bdc4, PadTrigger 5 -, the back key - GetHardReturnTap -:
    // tap 100, com_004, and BtlInterfaceSystem::OnUpdate 0x9a3c54's StopAuto; the turn in course plays out)
    if (k == SDLK_BACKSPACE && cr3AutoResult != 0) {
        audio.playSe("com_004", false);
        stopCr3Auto("the back key");
        return true;
    }
    // (the get-set with its tabs - FUN_8112bdc4 and its tab helpers FUN_8112a8c4 / 8112a8da: top to bottom
    // Mass Attack, Auto Battle, Battle, Escape, Options, round; a lit tab at its pressed frame, ○ its tap)
    if (cr3BattleStage == Cr3BattleStage::Menu && cr3AutoResult == 0 && cr3TabsShown() && (k == SDLK_UP || k == SDLK_DOWN || (enter && cr3TabLit))) {
        if (enter) return chooseCr3GetSet(cr3TabLit == 2 ? 5 : 6), true;
        if (k == SDLK_UP) {
            if (cr3TabLit == 2) cr3TabLit = 0, cr3Focus = 2;
            else if (cr3TabLit == 1) cr3TabLit = 2;
            else if (cr3Focus == 0) cr3TabLit = 1;
            else --cr3Focus;
        } else {
            if (cr3TabLit == 2) cr3TabLit = 1;
            else if (cr3TabLit == 1) cr3TabLit = 0;
            else if (cr3Focus == 2) cr3Focus = 0, cr3TabLit = 2;
            else ++cr3Focus;
        }
        audio.playSe("com_002", false);
        if (cr3TabLit) SDL_Log("battle: cursor on the %s tab", cr3TabLit == 2 ? "Mass Attack" : "Auto Battle");
        else SDL_Log("battle: cursor %d", cr3Focus);
        return true;
    }
    cr3::Cr3Command targetCommand;
    const bool allTargets = cr3BattleStage == Cr3BattleStage::Target && cr3Rules.command(cr3ChosenCommand, targetCommand) && targetCommand.all;
    const int n = cr3BattleStage == Cr3BattleStage::Menu ? 3 : cr3BattleStage == Cr3BattleStage::Command ? 4
                : cr3BattleStage == Cr3BattleStage::Escape ? (cr3EscapeAsk ? 2 : 0)
                : cr3BattleStage == Cr3BattleStage::List ? std::max(int(cr3List.size()), 1)
                : cr3BattleStage == Cr3BattleStage::Target ? (allTargets ? 1 : int(cr3Targets().size())) : 0;
    if (n == 0) return false;
    const int before = cr3Focus;
    if (k == SDLK_UP || k == SDLK_DOWN) {
        const int direction = k == SDLK_UP ? -1 : 1;
        if (vitaButtons() && cr3BattleStage == Cr3BattleStage::Target && cr3TargetCanChange()) cr3Focus = (cr3Focus + direction + n + 1) % (n + 1);
        else if (vitaButtons() && cr3BattleStage == Cr3BattleStage::List) cr3Focus = std::clamp(cr3Focus + direction, 0, n - 1);
        else cr3Focus = (cr3Focus + direction + n) % n;
        if (before != cr3Focus) {
            audio.playSe("com_002", false);
            SDL_Log("battle: cursor %d%s", cr3Focus, cr3BattleStage == Cr3BattleStage::Target && cr3Focus == n ? " (change target side)" : "");
        }
    }
    else if (enter) {
        if (cr3BattleStage == Cr3BattleStage::Menu) chooseCr3GetSet(cr3Focus);
        else if (cr3BattleStage == Cr3BattleStage::Escape) chooseCr3Escape(cr3Focus);
        else if (cr3BattleStage == Cr3BattleStage::List) chooseCr3ListRow(cr3Focus);
        else if (cr3BattleStage == Cr3BattleStage::Command) chooseCr3Command(cr3Focus);
        else if (cr3TargetCanChange() && cr3Focus == n) {
            cr3TargetChanged = !cr3TargetChanged, cr3Focus = 0;
            audio.playSe("com_003", false);
            SDL_Log("battle: target side changed");
        }
        else chooseCr3Target(cr3Focus);
    } else if (k == SDLK_BACKSPACE && cr3BattleStage == Cr3BattleStage::Escape) { // (GetHardReturnTap: com_004, back)
        audio.playSe("com_004", false);
        chooseCr3Escape(1);
    } else if (k == SDLK_BACKSPACE && (cr3BattleStage == Cr3BattleStage::Target || cr3BattleStage == Cr3BattleStage::List)) {
        audio.playSe("com_004", false);
        cr3BattleStage = Cr3BattleStage::Command, cr3StageStart = cr3MenuStart = cr3BattleClock, cr3Focus = 0;
    } else if (k == SDLK_BACKSPACE && cr3BattleStage == Cr3BattleStage::Command) {
        audio.playSe("com_004", false);
        if (cr3Choices.empty()) {
            beginCr3Turn(false); // (back to the get-set)
        } else {
            const int prev = cr3Choices.back()[0];
            cr3Choices.pop_back();
            if (const auto it = cr3TurnItems.find(prev); it != cr3TurnItems.end()) {
                game.cr3Items[it->second] = std::min(game.cr3Items[it->second] + 1, 99);
                cr3TurnItems.erase(it);
            }
            cr3Chooser = prev - 1;
            nextCr3Chooser();
        }
    } else return false;
    return true;
}

// a click (fx, fy: the screen's fractions) on a button
bool Viewer::cr3BattleClick(float fx, float fy) {
    if (tapCr3Analyze()) return true; // (a tap anywhere: the Analyze's next line)
    if (cr3BattleStage == Cr3BattleStage::Result && cr3ResultPhase == 2 && (cr3GeneUp.step == 6 || cr3GeneUp.step == 7)) {
        // (the replace list's rows - another's tap its focus, the focused one's it chosen -, its question's Yes / No)
        if (cr3BattleClock - cr3GeneUp.at < 0.2f) return true;
        const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
        const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
        for (auto it = cr3GeneUpTaps.rbegin(); it != cr3GeneUpTaps.rend(); ++it) {
            if (ux < std::min(it->x0, it->x1) || ux > std::max(it->x0, it->x1) || uy < std::min(it->y0, it->y1) || uy > std::max(it->y0, it->y1)) continue;
            if (it->id == kTapYes || it->id == kTapNo) answerCr3GeneAsk(it->id == kTapYes);
            else if (it->id >= kTapRow && it->id < kTapRow + 7) {
                if (it->id - kTapRow == cr3GeneUp.focus) chooseCr3GeneRow(cr3GeneUp.focus);
                else cr3GeneUp.focus = it->id - kTapRow, audio.playSe("com_002", false);
            }
            break;
        }
        return true;
    }
    if (cr3Ctn.stage != 0) { // (the continue's buttons, its question's)
        const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
        const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
        for (auto it = cr3CtnTaps.rbegin(); it != cr3CtnTaps.rend(); ++it)
            if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1))
                return chooseCr3Continue(it->id), true;
        return true;
    }
    if (cr3AutoResult != 0 && !(cr3BattleEvents && cr3BattleEvents->running())) {
        // (the marker - comBtnMenu::TouchSimpleRelease inside comBtnTab_fr03: tap 100, com_004 - in any
        // stage, the turn in course played out; a tap anywhere else does not stop it)
        const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
        const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
        for (const Cr3Tap& t : cr3AutoTaps)
            if (ux >= std::min(t.x0, t.x1) && ux <= std::max(t.x0, t.x1) && uy >= std::min(t.y0, t.y1) && uy <= std::max(t.y0, t.y1)) {
                audio.playSe("com_004", false);
                stopCr3Auto("the marker");
                return true;
            }
    }
    if (cr3BattleStage != Cr3BattleStage::Menu && cr3BattleStage != Cr3BattleStage::Command && cr3BattleStage != Cr3BattleStage::Target &&
        cr3BattleStage != Cr3BattleStage::List && !(cr3BattleStage == Cr3BattleStage::Escape && cr3EscapeAsk))
        return false;
    if (cr3BattleEvents && cr3BattleEvents->running()) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (const Cr3Tap& t : cr3Taps)
        if (ux >= std::min(t.x0, t.x1) && ux <= std::max(t.x0, t.x1) && uy >= std::min(t.y0, t.y1) && uy <= std::max(t.y0, t.y1)) {
            if (cr3BattleStage == Cr3BattleStage::Menu) chooseCr3GetSet(t.id);
            else if (cr3BattleStage == Cr3BattleStage::Escape) chooseCr3Escape(t.id);
            else if (cr3BattleStage == Cr3BattleStage::List) chooseCr3ListRow(t.id);
            else if (cr3BattleStage == Cr3BattleStage::Command) chooseCr3Command(t.id);
            else if (t.id == -2) {
                cr3TargetChanged = !cr3TargetChanged, cr3Focus = 0;
                audio.playSe("com_003", false);
                SDL_Log("battle: target side changed");
            } else chooseCr3Target(t.id);
            return true;
        }
    return false;
}

// BtlUnit::VGetCameraPoint: its figure's DummyCP joint as last posed (CrxModel::GetPosition),
// else where it stands
std::array<float, 3> Viewer::cameraPoint(size_t k) {
    const cr3::Cr3BattleUnit& bu = cr3Battle.units[k];
    ViewFigure& f = cr3BattleUnits[k].body;
    std::vector<uint8_t> d;
    if (f.skinned && archive.read(bu.figure + ".name", d)) {
        const std::vector<std::string> names = cr3::parseJointNames(d);
        const auto it = std::find(names.begin(), names.end(), "DummyCP");
        const std::vector<cr3::JointTransform>& p = f.posed.empty() ? f.skeleton.basePose : f.posed;
        const std::vector<cr3::Mat34> w = f.skeleton.world(p, f.place);
        const size_t j = size_t(it - names.begin());
        if (it != names.end() && j < w.size()) return {w[j].m[3], w[j].m[7], w[j].m[11]};
    }
    return bu.home;
}

void Viewer::cr3BattleTap() {
    if (cr3BattleEvents && cr3BattleEvents->running()) return cr3BattleEvents->touch(); // (its window's page)
    if (tapCr3Analyze()) return; // (an Analyze's lines: the next)
    if (cr3BattleStage == Cr3BattleStage::Result) {
        // (TouchGestureTap: the tally completed; a window's alert closed - CheckTapAlertWindowMenu -; the
        // cards' moves and the window's own parts take none; then the drops' lines, at the last the end)
        if (cr3ResultPhase == 0 && cr3BattleClock - cr3StageStart < cr3ResultLead() + 1.5f) {
            cr3StageStart = cr3BattleClock - cr3ResultLead() - 1.5f; // (VIP's: its multiply passed over too)
            SDL_Log("battle: result tally completed");
        } else if (cr3ResultPhase == 2) {
            if (cr3GeneUp.alert && cr3BattleClock - cr3GeneUp.alertAt > 0.1f) cr3GeneUp.alert = false, SDL_Log("battle: gene alert closed");
        } else if (cr3ResultPhase == 4 && !showCr3DropLine()) endCr3Battle();
        return;
    }
    if (cr3BattleStage == Cr3BattleStage::Won && cr3VictoryIn) {
        cr3StageStart = cr3BattleClock - cr3VictoryDuration;
        SDL_Log("battle: victory camera skipped");
        return;
    }
    if (cr3BattleStage == Cr3BattleStage::In) cr3Cam.start = cr3BattleClock - cr3Cam.end, cr3BattleInSkipped = true; // (phaseBattleIn: a tap ends it)
    else if (cr3BattleStage == Cr3BattleStage::Menu && cr3AutoResult == 0) chooseCr3GetSet(cr3TabLit ? (cr3TabLit == 2 ? 5 : 6) : cr3Focus);
    else if (cr3BattleStage == Cr3BattleStage::Escape) chooseCr3Escape(cr3Focus);
    else if (cr3BattleStage == Cr3BattleStage::List) chooseCr3ListRow(cr3Focus);
    else if (cr3BattleStage == Cr3BattleStage::Command) chooseCr3Command(cr3Focus);
    else if (cr3BattleStage == Cr3BattleStage::Target) chooseCr3Target(cr3Focus);
}

void Viewer::endCr3Battle() {
    if (vs.result) { // (VS: the won tournament's result dismissed - on to its results)
        vs.result = false;
        endCr3Battle();
        vsShowResults();
        return;
    }
    if (activeAceId > 0) { // (Fld2TaskEncount::AceCouplingLevelUp @ 0x00867d88)
        if (!cr3BattleLost && !cr3Escaped)
            for (AreaEncounters::Ace& a : areaEnc.aces)
                if (a.id == activeAceId && a.calcLevel == 0 && a.stepLv > 0) {
                    const int nextLv = a.capLv > 0 ? std::min(a.level + a.stepLv, a.capLv) : a.level + a.stepLv;
                    a.level = nextLv;
                    game.cr3AceLevels[a.id] = nextLv;
                    SDL_Log("ace %d: leveled up to %d", a.id, nextLv);
                }
        activeAceId = -1;
    }
    for (int& c : game.next.cr3BattleParty) c = -1; // (GameMain::SM_ChangeMode: cleared)
    cr3BattleMode = false;
    clearCr3Effects();
    cr3::GameState::Next& n = game.next;
    const int next = n.battleNext;
    n.battleNext = 1;
    cr3BattleEvents.reset();
    if ((next == 14 || cr3BattleVista) && !n.vistaFunction.empty() && !cr3BattleLost) { // (a game over: the field's evt002 instead - EventBattleLose)
        const std::string fn = n.vistaFunction, map = n.vistaMap.empty() ? "null" : n.vistaMap;
        n = cr3::GameState::Next();
        SDL_Log("battle: on to %s in %s", fn.c_str(), map.c_str());
        startVista(eventScriptOf(fn) + ":" + fn, map);
        return;
    }
    SDL_Log("battle: over (next %d)", next);
    n = cr3::GameState::Next();
    if (!savedArea.empty()) {
        n.fieldMode = 1;
        const std::string back = savedArea; // (a copy: loadArea clears savedArea as it restores the place)
        if (!loadArea(back, areaPlayerArgName)) SDL_Log("area %s: not entered", back.c_str());
    }
}

void Viewer::drawCr3Battle(int w, int h) {
    // the camera: the motion's joint at its time (held at its end, or looped), on its actor
    float t = cr3BattleClock - cr3Cam.start;
    if (cr3Cam.held || (!cr3Cam.loop && t > cr3Cam.end)) t = cr3Cam.end;
    else if (cr3Cam.loop && cr3Cam.end > 0) t = std::fmod(t, cr3Cam.end);
    cr3::Mat34 cw;
    float fovDeg = 45, zn = 0.1f, zf = 7000;
    const bool own = cr3Cam.name.find("Cam_bt") == std::string::npos;
    if (!(own ? cr3BattleOwnCam : cr3BattleCam).at(cr3Cam.name, t, cw, fovDeg, zn, zf)) cw.m[3] = 0, cw.m[7] = 3, cw.m[11] = 8;
    cw.m[3] *= cr3Cam.scale, cw.m[7] *= cr3Cam.scale, cw.m[11] *= cr3Cam.scale;
    zn = 0.1f, zf = 7000; // (BtlCamera::Initialize: its own planes; the motion gives the field of view alone)
    const float c = std::cos(cr3Cam.actorYaw), s = std::sin(cr3Cam.actorYaw);
    cr3::Mat34 actor;
    actor.m[0] = c, actor.m[2] = s, actor.m[8] = -s, actor.m[10] = c;
    actor.m[3] = cr3Cam.actor[0], actor.m[7] = cr3Cam.actor[1], actor.m[11] = cr3Cam.actor[2];
    cw = actor * cw;
    if (cr3Cam.lookWeight > 0) { // (turned part of the way toward its target: forward blended, up kept)
        float f[3] = {-cw.m[2], -cw.m[6], -cw.m[10]};
        float d[3] = {cr3Cam.lookAt[0] - cw.m[3], cr3Cam.lookAt[1] - cw.m[7], cr3Cam.lookAt[2] - cw.m[11]};
        const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dl > 1e-4f) {
            for (int i = 0; i < 3; ++i) f[i] += (d[i] / dl - f[i]) * cr3Cam.lookWeight;
            const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            for (float& c : f) c /= fl;
            float x[3] = {-f[2], 0, f[0]}; // (forward x up)
            const float xl = std::max(std::hypot(x[0], x[2]), 1e-5f);
            x[0] /= xl, x[2] /= xl;
            const float z[3] = {-f[0], -f[1], -f[2]};
            const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]}; // (z x x)
            for (int r = 0; r < 3; ++r) cw.m[r * 4 + 0] = x[r], cw.m[r * 4 + 1] = y[r], cw.m[r * 4 + 2] = -f[r];
        }
    }
    const float fov = fovDeg * 3.14159265f / 180;
    dev->setCamera(zn, zf, fov);
    const Mat4 world = cr3::mat4FromMat34(cw);
    Mat4 proj = perspective(fov, float(w) / float(std::max(h, 1)), zn, zf);
    if (cr3Cam.screenShift != 0) { // (Camera::Render: a clip-space move by the offset / the width)
        Mat4 shift{};
        shift[0] = shift[5] = shift[10] = shift[15] = 1;
        shift[12] = cr3Cam.screenShift;
        proj = cr3::mat4Multiply(shift, proj);
    }
    applyQuake(proj); // (battle layers 5..9: QuakeControlTask::Render)
    const Mat4 vp = cr3::mat4Multiply(proj, cr3::mat4RigidInverse(world));
    cr3BattleVP = vp;
    cr3::SceneLighting l = lighting;
    l.camera[0] = cw.m[3], l.camera[1] = cw.m[7], l.camera[2] = cw.m[11];
    l.time = time;
    dev->setLighting(l);
    ViewFigure& f = cr3BattleField;
    f.place = cr3::Mat34();
    { // (BtlField's animator stepped: looped, or - ResetLooping - on to its end time and held)
        const float dt = cr3StageHidden ? 0.0f : std::max(cr3BattleClock - cr3FieldDrawnAt, 0.0f), length = f.animLoaded ? f.anim.duration() : 0.0f; // (hidden: not stepped)
        cr3FieldDrawnAt = cr3BattleClock;
        cr3FieldTime += dt;
        if (length <= 0) cr3FieldTime = 0;
        else if (cr3FieldLoop) cr3FieldTime = std::fmod(cr3FieldTime, length);
        else cr3FieldTime = std::min(cr3FieldTime, cr3FieldEnd >= 0 ? std::min(cr3FieldEnd, length) : length);
    }
    if (!cr3StageHidden) { // (BtlField::Hide: the stage, its objects not drawn)
        pose(f, cr3FieldTime, false);
        drawFigure(f, vp.data());
        for (Cr3FieldObject& o : cr3FieldObjects) drawAttached(o.figure, f, o.joint, cr3BattleClock, vp.data()); // (BtlFieldObject: on its joint, looped)
    }
    for (auto& [slot, m] : cr3ScriptModels) { // (BattleModel's: at their place, turned - x, y, z degrees -, looped)
        if (m.figure.meshes.empty()) continue;
        const float d = 3.14159265f / 180;
        const float cx = std::cos(m.rotation[0] * d), sx = std::sin(m.rotation[0] * d), cy = std::cos(m.rotation[1] * d), sy = std::sin(m.rotation[1] * d),
                    cz = std::cos(m.rotation[2] * d), sz = std::sin(m.rotation[2] * d);
        cr3::Mat34& p = m.figure.place;
        p = cr3::Mat34(); // (Rx Ry Rz)
        p.m[0] = cy * cz, p.m[1] = -cy * sz, p.m[2] = sy;
        p.m[4] = sx * sy * cz + cx * sz, p.m[5] = -sx * sy * sz + cx * cz, p.m[6] = -sx * cy;
        p.m[8] = -cx * sy * cz + sx * sz, p.m[9] = cx * sy * sz + sx * cz, p.m[10] = cx * cy;
        p.m[3] = m.at[0], p.m[7] = m.at[1], p.m[11] = m.at[2];
        pose(m.figure, cr3BattleClock, true);
        drawFigure(m.figure, vp.data());
    }
    // (BtlUnit::poseChild 0x009db6ac: a held unit at its holder's effect point - its turn not followed)
    std::map<size_t, cr3::Vec3> heldAt;
    for (const auto& [holder, h] : cr3Held) {
        const int hu = holder >= 0 && size_t(holder) < cr3Rules.fighters.size() ? cr3Rules.fighters[size_t(holder)].unit : -1;
        const int tu = h.first >= 0 && size_t(h.first) < cr3Rules.fighters.size() ? cr3Rules.fighters[size_t(h.first)].unit : -1;
        if (hu >= 0 && tu >= 0 && size_t(hu) < cr3BattleUnits.size() && size_t(tu) < cr3BattleUnits.size())
            heldAt[size_t(tu)] = cr3EffectPoint(size_t(hu), h.second);
    }
    for (size_t k = 0; k < cr3BattleUnits.size() && k < cr3Battle.units.size(); ++k) {
        if (cr3BattleStage == Cr3BattleStage::Won && cr3VictoryIn && cr3VictorUnit >= 0 && int(k) != cr3VictorUnit) continue;
        const cr3::Cr3BattleUnit& bu = cr3Battle.units[k];
        Cr3BattleUnitView& u = cr3BattleUnits[k];
        const float opacity = u.opacity.at(cr3BattleClock);
        if (opacity <= 0 || u.scriptHidden || u.directionHidden) continue;
        const float yaw = bu.yaw + u.meleeTurn.at(cr3BattleClock);
        const float step = u.meleeStep.at(cr3BattleClock);
        const float yc = std::cos(yaw) * bu.scale, ys = std::sin(yaw) * bu.scale;
        for (ViewFigure* part : {&u.body, &u.head}) {
            if (part->meshes.empty()) continue;
            part->place = cr3::Mat34();
            part->place.m[0] = yc, part->place.m[2] = ys, part->place.m[8] = -ys, part->place.m[10] = yc, part->place.m[5] = bu.scale;
            part->place.m[3] = bu.home[0] + u.meleeOffset[0] * step;
            part->place.m[7] = bu.home[1];
            part->place.m[11] = bu.home[2] + u.meleeOffset[2] * step;
            if (const auto held = heldAt.find(k); held != heldAt.end())
                part->place.m[3] = held->second[0], part->place.m[7] = held->second[1], part->place.m[11] = held->second[2];
            part->tint[3] = opacity;
            const float mt = u.motionLoop ? cr3BattleClock + u.phase : cr3BattleClock - u.motionStart;
            pose(*part, mt, u.motionLoop);
            drawFigure(*part, vp.data());
        }
        drawAttached(u.weapon, u.body, "DummyWP", cr3BattleClock, vp.data());
    }
    if (cr3BattleSpark.fx) { // (the field's visual effect: BtlFieldVisualEffect - its figure at the field's origin)
        const float dt = cr3BattleClock - cr3BattleSparkClock;
        cr3BattleSparkClock = cr3BattleClock;
        if (dt > 0 && dt < 1) cr3BattleSpark.fx->step(dt);
        drawSpark(cr3BattleSpark, cr3::Mat34(), cw, vp.data());
    }
    drawCr3Effects(cw, vp);
    drawCr3Glass();
}

void Viewer::drawCr3Result(int w, int h) {
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    const float t = cr3BattleClock - cr3StageStart, since = cr3BattleClock - cr3ResultPhaseAt;
    bool levelled = false;
    for (const Cr3ResultCard& rc : cr3ResultCards) levelled |= rc.up;
    // (state 6: the info / point menus faded out over 0.2 s; state 0xb's end: back in as long)
    const float menuAlpha = cr3ResultPhase == 1 ? std::max(1.0f - since / 0.2f, 0.0f) : cr3ResultPhase == 2 ? 0.0f
                          : cr3ResultPhase == 3 ? std::min(since / 0.2f, 1.0f) : 1.0f;
    UiFig& pos = cr3UiPut("btrsltposition", "btrsltposition_in", t, 0, 0, view);
    float x, y;
    if (cr3UiCall(pos, "call_btRsltBgMenu", x, y, w, h)) cr3UiPut("btrsltbgmenu", "", 0, x, y, view);
    // (the background - the position's own meshes too - stands at z 0.03 .. 0.05, in front of the
    // menus' 0 .. 0.02: the parts are drawn in order, over it)
    dev->clearDepth();
    // (SetParamNumber case 4: with VIP the plain banners - +0x18 / +0x20 - deleted, btRsltTitleVipTMenu / VipBMenu - +0x28 / +0x30 -
    // at their own joints; without, the VIP ones and the panels)
    if (cr3ResultVip) {
        if (cr3UiCall(pos, "call_btRsltTitleVipTMenu", x, y, w, h)) cr3UiPut("btrslttitleviptmenu", "btrslttitleviptmenu_in", t, x, y, view);
        if (cr3UiCall(pos, "call_btRsltTitleVipBMenu", x, y, w, h)) cr3UiPut("btrslttitlevipbmenu", "btrslttitlevipbmenu_in", t, x, y, view);
    } else {
        if (cr3UiCall(pos, "call_btRsltTitleTMenu", x, y, w, h)) cr3UiPut("btrslttitletmenu", "btrslttitletmenu_in", t, x, y, view);
        if (cr3UiCall(pos, "call_btRsltTitleBMenu", x, y, w, h)) cr3UiPut("btrslttitlebmenu", "btrslttitlebmenu_in", t, x, y, view);
    }
    // (the replace list's focused row's help - GeneLvUpMenu's own btRsltPosition / btRsltInfoMenu, msg 0xd)
    std::string infoLine = cr3ResultLine;
    float infoAlpha = menuAlpha;
    if (cr3ResultPhase == 2 && cr3GeneUp.step == 6 && cr3GeneUp.card >= 0 && size_t(cr3GeneUp.card) < cr3ResultCards.size()) {
        const Cr3ResultCard& rc = cr3ResultCards[size_t(cr3GeneUp.card)];
        if (cr3GeneUp.focus == 6 && size_t(cr3GeneUp.entry) < rc.learnt.size())
            infoLine = cr3SkillName(rc.learnt[size_t(cr3GeneUp.entry)][0], rc.learnt[size_t(cr3GeneUp.entry)][1], true);
        else if (rc.gene >= 0 && size_t(rc.gene) < game.cr3Genes.size() && cr3GeneUp.focus < 6) {
            const cr3::Cr3Gene& g = game.cr3Genes[size_t(rc.gene)];
            infoLine = cr3SkillName(g.slots[cr3GeneUp.focus][0], g.slots[cr3GeneUp.focus][1], true);
        }
        infoAlpha = 1;
    }
    if (infoAlpha > 0 && cr3UiCall(pos, "call_btRsltInfoMenu", x, y, w, h)) { // (its typeM_L01: SetDropItemText's line)
        UiFig& info = cr3UiPut("btrsltinfomenu", "btrsltinfomenu_in", t, x, y, view, infoAlpha);
        float tx, ty;
        const bool infoText = cr3UiCall(info, "typeM_L01", tx, ty);
        if (!infoLine.empty() && infoText) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            const size_t br = infoLine.find("\\n"); // (a help's break: the first line here)
            drawUiText(infoLine.substr(0, br), 26, (tx + 0.075f) / 10, (ty - 0.15f) / 10, w, h, true, infoAlpha);
        }
    }
    if (menuAlpha > 0 && cr3UiCall(pos, "call_btRsltPointMenu", x, y, w, h)) {
        // (two btRsltPointPanel_numNN - N its digits - the EXP's and the money's, a
        // btRsltPointNum_fr00 frame a digit)
        UiFig& pm = cr3UiPut("btrsltpointmenu", "btrsltpointmenu_in", t, x, y, view, menuAlpha);
        // (VIP - Update 0xb23e1c: state 0 holds the base totals with the factors' panels for kCr3VipWait, then the panels fade out
        // over 0.133 s while the multiplied digits - SetRsltPointMenuVip 0xb24fb0 - fade in; state 1 pulses them 1/3 s: diffuse 1 + p,
        // scale 1 + p / 2, each slid by p (its x - GetVipCorrectionXPosition 0xb255e8: the EXP row's digits' middle) / 2, p 1 -> 0)
        const bool vipBase = cr3ResultVip && t < kCr3VipWait;
        const float vipT = t - kCr3VipWait, pulse = cr3ResultVip && vipT >= 0 && vipT < 1.0f / 3 ? 1.0f - vipT * 3 : 0.0f;
        float correctX = 0;
        for (int k = 0; k < 2; ++k) {
            float px, py;
            if (!cr3UiCall(pm, "call_btRsltPointPanel_num00_00" + std::to_string(k + 1), px, py)) continue;
            const int total = std::max(vipBase ? cr3ResultBase[k] : k ? cr3ResultMoney : cr3ResultExp, 0);
            const float shown = cr3ResultVip ? 1.0f : std::clamp(t / 1.5f, 0.0f, 1.0f); // (CR3-U's count up; VIP's the totals as they stand)
            const std::string digits = std::to_string(int(float(total) * shown));
            const int n = std::min(int(digits.size()), 6);
            char lay[32];
            std::snprintf(lay, sizeof lay, "btrsltpointpanel_num%02d", n);
            const float digitAlpha = menuAlpha * (cr3ResultVip && !vipBase ? std::min(vipT / 0.1333f, 1.0f) : 1.0f);
            UiFig& panel = cr3UiPut(lay, "", 0, px, py, view, digitAlpha);
            float dxs[6] = {}, dys[6] = {};
            int placed = 0;
            for (int d = 0; d < n; ++d) {
                const std::string joint = n == 1 ? std::string("call_btRsltPointNum_fr00") : "call_btRsltPointNum_fr00_00" + std::to_string(d + 1);
                if (cr3UiCall(panel, joint, dxs[placed], dys[placed])) ++placed;
            }
            if (k == 0 && placed > 0) { // (the correction: the EXP row's middle)
                for (int d = 0; d < placed; ++d) correctX += dxs[d];
                correctX /= float(placed);
            }
            for (int d = 0; d < placed; ++d) {
                // (SetRsltPointMenu / SetRsltPointMenuVip: the money's digits at frames + 10)
                const float frame = float(digits[size_t(d)] - '0' + (k ? 10 : 0)) / 30;
                if (pulse > 0) {
                    const float keep = cr3UiScale;
                    cr3UiScale = 1 + pulse * 0.5f;
                    cr3UiTintMaterial = "mat_bt_results_01", cr3UiMaterialTint[0] = cr3UiMaterialTint[1] = cr3UiMaterialTint[2] = 1 + pulse;
                    cr3UiPut("btrsltpointnum_fr00", "", frame, dxs[d] + pulse * (dxs[d] - correctX) * 0.5f, dys[d], view, digitAlpha);
                    cr3UiTintMaterial.clear(), cr3UiScale = keep;
                } else cr3UiPut("btrsltpointnum_fr00", "", frame, dxs[d], dys[d], view, digitAlpha);
            }
            // (btRsltVipPanel - _in - at btRsltPointMenu's call_btRsltVipPanel_00n - Pose 0xb25cc4's shift by the digits shown, x 0.16,
            // looks for that joint in the num panel, which has none -: "x", the factor's whole, ".", its tenths - SetRsltVipMenu 0xb27990,
            // 9.9 at most -, comNum01_fr00 digits - the panel's own, btRsltVipPanel's ctor 0xb1a09c - at its joints _002 / _001 (_003 the
            // tens: none))
            const float panelAlpha = menuAlpha * (vipBase ? 1.0f : std::max(1.0f - vipT / 0.1333f, 0.0f));
            float vx, vy;
            if (cr3ResultVip && panelAlpha > 0 && cr3UiCall(pm, "call_btRsltVipPanel_00" + std::to_string(k + 1), vx, vy)) {
                // (its _in as a relative animator not reproduced: the panel at rest)
                UiFig& vp = cr3UiPut("btrsltvippanel", "", 0, vx, vy, view, panelAlpha);
                const float by = std::min(cr3ResultVipBy[k], 9.9f);
                const int part[2] = {int(by), int(by * 10.0f + 0.5f) % 10};
                dev->clearDepth(); // (over the panel's own meshes)
                for (int q = 0; q < 2; ++q) {
                    float nx, ny;
                    if (cr3UiCall(vp, "call_comNum01_fr00_00" + std::to_string(2 - q), nx, ny)) cr3UiPut("comnum01_fr00", "", float(part[q]) / 30, nx, ny, view, panelAlpha);
                }
            }
        }
    }
    // the cards: btRsltCharaPanel_in (fading in from 1/6 s), out (SetOutMove: btRsltCharaPanel_out,
    // fading out from 0.2667 s) while the windows are up, in again after (SetInMove)
    const bool out = cr3ResultPhase == 1;
    const float cardT = cr3ResultPhase == 1 || cr3ResultPhase == 3 ? since : cr3ResultPhase == 4 && levelled ? 10.0f : t;
    const float cardAlpha = out ? std::clamp(1.0f - (cardT - 0.2667f) * 6, 0.0f, 1.0f) : std::clamp((cardT - 1.0f / 6) * 6, 0.0f, 1.0f);
    if (cr3ResultPhase != 2 && cr3UiCall(pos, "call_btRsltCharaMenu", x, y, w, h)) {
        UiFig& cm = cr3UiPut("btrsltcharamenu", out ? "btrsltcharamenu_out" : "btrsltcharamenu_in", cardT, x, y, view);
        int slot = 0;
        for (const Cr3ResultCard& rc : cr3ResultCards) {
            float px, py;
            if (!cr3UiCall(cm, "call_btRsltCharaPanel_00" + std::to_string(++slot), px, py)) continue;
            UiFig& card = cr3UiPut("btrsltcharapanel", out ? "btrsltcharapanel_out" : "btrsltcharapanel_in", cardT, px, py, view);
            float cx, cy;
            // (SetCardModel: the worn gene's card - card_default at call_GeneCard, 4.75 x)
            if (rc.master > 0 && cr3UiCall(card, "call_GeneCard", cx, cy)) drawCr3Card(rc.master, cx, cy, 1.0f, 4.75f, view, cardAlpha);
            dev->clearDepth(); // (over the card's shadow)
            const int level = cr3ResultNew ? rc.level : rc.oldLevel;
            const bool top = level >= rc.maxLevel; // (SetNumber: MAX - its bar full, SetLevelPer then ignored)
            // (SetCharaCardData: the icon's frame the character's - 1, the bar its share done x 100 / 30 s)
            if (cr3UiCall(card, "call_btRsltCharaIcon_fr00", cx, cy)) cr3UiPut("btrsltcharaicon_fr00", "", float(rc.ch - 1) / 30, cx, cy, view);
            if (cr3UiCall(card, "call_btRsltLv_per", cx, cy))
                cr3UiPut("btrsltlv_per", "", top ? 100.0f / 30 : (cr3ResultNew ? rc.progress : rc.oldProgress) * 100 / 30, cx, cy, view);
            // the level (SetNumber 0xb185f8): btRsltLvPanel_num00's three places, btRsltLvNum_fr00 a
            // digit's frame - the leading places "0" at +0x110 0.5 (dimmed); at its top MAX - frames 10, 11, 12
            if (cr3UiCall(card, "call_btRsltLvPanel_num00", cx, cy)) {
                UiFig& lv = cr3UiPut("btrsltlvpanel_num00", "", 0, cx, cy, view);
                const int n = std::clamp(level, 0, 999), lead = n < 10 ? 2 : n < 100 ? 1 : 0;
                const int digits[3] = {n / 100, n / 10 % 10, n % 10};
                for (int d = 0; d < 3; ++d) {
                    float dx, dy;
                    if (!cr3UiCall(lv, "call_btRsltLvNum_fr00_00" + std::to_string(d + 1), dx, dy)) continue;
                    cr3UiAlphaSet = !top && d < lead; // (its materials' alpha outright - PartsBase::SetAlpha)
                    cr3UiPut("btrsltlvnum_fr00", "", float(top ? 10 + d : digits[d]) / 30, dx, dy, view, cr3UiAlphaSet ? 0.5f : 1.0f);
                    cr3UiAlphaSet = false;
                }
            }
        }
    }
    if (cr3ResultPhase == 2) drawCr3GeneUp(w, h, view);
}

// GeneLvUpMenu (Initialize 0xb1a950): btRsltUpPositon (_in / _out 0.1667 s, fading in over 0.2 s), the
// gene's card at its call_GeneCard (7 x), btRsltLvUpMenu / btRsltSkillMenu at their fit joints (parked
// at their ends until started - 3.3 s each); the alert (AlertwindowMenu: comMsgSMenu); the replace
// list (comListMenu, ReOpenListMenu 0x1b - no back) with the window slid left, its question (0x1cd)
void Viewer::drawCr3GeneUp(int w, int h, const Mat4& view) {
    cr3GeneUpTaps.clear();
    const Cr3GeneUp& u = cr3GeneUp;
    if (u.card < 0 || size_t(u.card) >= cr3ResultCards.size()) return;
    const Cr3ResultCard& rc = cr3ResultCards[size_t(u.card)];
    const float s = cr3BattleClock - u.at, now = cr3BattleClock;
    // (state 3: x -0.5, y -0.1 an update to -2.5 / -0.5; back the same)
    const float slid = u.step == 5 ? std::min(s * 30, 5.0f) : u.step == 6 || u.step == 7 ? 5.0f : u.step == 8 ? std::max(5.0f - s * 30, 0.0f) : 0.0f;
    const bool closing = u.step == 9;
    const float inT = u.step == 0 ? s : 1.0f;
    const float alpha = closing ? std::max(1.0f - s / 0.1667f, 0.0f) : std::min(inT / 0.2f, 1.0f);
    dev->clearDepth();
    UiFig& up = cr3UiPut("btrsltuppositon", closing ? "btrsltuppositon_out" : "btrsltuppositon_in", closing ? s : inT, -0.5f * slid, -0.1f * slid, view, alpha);
    float x, y;
    if (cr3UiCall(up, "call_GeneCard", x, y)) drawCr3Card(rc.master, x, y, 1.0f, 7.0f, view, alpha);
    dev->clearDepth();
    if (cr3UiCall(up, "call_btRsltLvUpMenu", x, y, w, h)) cr3UiPut("btrsltlvupmenu", "btrsltlvupmenu", std::min(now - u.lvAt, 3.3f), x, y, view, alpha);
    if (cr3UiCall(up, "call_btRsltSkillMenu", x, y, w, h)) cr3UiPut("btrsltskillmenu", "btrsltskillmenu", std::min(now - u.skillAt, 3.3f), x, y, view, alpha);
    if (u.alert) drawCr3Told(u.text, now - u.alertAt, kTapAlert, w, h, view, &cr3GeneUpTaps);
    if ((u.step != 6 && u.step != 7) || rc.gene < 0 || size_t(rc.gene) >= game.cr3Genes.size()) return;
    const cr3::Cr3Gene& g = game.cr3Genes[size_t(rc.gene)];
    // (BuildCommmandSkillList: slots 0 .. 5 - id the slot -, the new one id 10)
    struct Row { std::string name; int icon = -1; };
    std::vector<Row> rows;
    auto iconOf = [&](int type, int id) {
        cr3::MbeFile::Row row; // (StatusMain::SetStatusParameter's: a command's +0x08, a skill's skill_para +0x08)
        if (!cr3TablesReady()) return -1;
        return (type == 0 ? cr3BattleTables.command.find("Command", id, "iii", row) : cr3BattleTables.skill.find("Skill", id, "iii", row)) ? row.i(2) : -1;
    };
    for (int k = 0; k < 6; ++k) rows.push_back({cr3SkillName(g.slots[k][0], g.slots[k][1]), iconOf(g.slots[k][0], g.slots[k][1])});
    if (size_t(u.entry) < rc.learnt.size())
        rows.push_back({cr3SkillName(rc.learnt[size_t(u.entry)][0], rc.learnt[size_t(u.entry)][1]), iconOf(rc.learnt[size_t(u.entry)][0], rc.learnt[size_t(u.entry)][1])});
    const int focus = std::clamp(u.focus, 0, int(rows.size()) - 1), listN = int(rows.size());
    if (u.step == 6) {
        UiFig& pos = cr3UiPut("comlistposition", "comlistposition_io", s, 0, 0, view);
        float lx, ly;
        if (cr3UiCall(pos, "call_comListMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
            if (cr3UiCall(menu, "call_comListPanel", lx, ly)) drawCr3ListEnds(lx, ly, listN, focus, view);
            if (cr3UiCall(menu, "call_comListPanel", lx, ly))
                for (int k = std::max(focus - 2, 0); k < listN && k <= focus + 2; ++k) {
                    const Row& row = rows[size_t(k)];
                    Cr3FocusRow focusRow(*this, k == focus);
                    UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, k, focus), cr3ListRowY(ly, k, focus - 2, focus), view);
                    float cx, cy;
                    if (k == focus && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                    if (row.icon >= 0 && cr3UiCall(panel, "call_comListIcon_fr00", cx, cy))
                        cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), row.icon), cx, cy, view);
                    if (cr3UiCall(panel, "typeM_L01", cx, cy)) {
                        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                        const float ks = cr3UiFocus ? 1.1f : 1.0f;
                        drawUiText(row.name, 26 * ks, (cx + 0.075f * ks) / 10, (cy - 0.15f * ks) / 10, w, h, true, cr3UiFocus ? 1.0f : 0.3f);
                    }
                    float ax, ay, bx, by;
                    if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3GeneUpTaps.push_back({ax, ay, bx, by, kTapRow + k});
                }
        }
        return;
    }
    std::string text = menuAnnounce(0x1cd); // ({genename} will forget {csname}.\nAre you sure?)
    if (const size_t at = text.find("{genename}"); at != std::string::npos) text.replace(at, 10, cr3NameOf("gene_name.mbe", "GeneName", g.master));
    if (const size_t at = text.find("{csname}"); at != std::string::npos) text.replace(at, 8, rows[size_t(focus)].name);
    drawCr3Asked(text, s, u.askFocus, true, kTapYes, kTapNo, w, h, view, &cr3GeneUpTaps);
}

float Viewer::cr3SpellCardDuration(size_t count) {
    // BattleCardCutInMenu ends when its position (one card) or menu (two/three) animation ends.
    constexpr std::array<float, 4> frames = {0, 35, 38, 41};
    return frames[std::min(count, frames.size() - 1)] / 30;
}

void Viewer::drawCr3SpellCards(int w, int h, float elapsed, const std::vector<int>& geneIds) {
    const size_t count = std::min(geneIds.size(), kCr3SpellCardSoundTimes.size());
    if (count == 0 || elapsed < 0 || elapsed >= cr3SpellCardDuration(count)) return;

    // BattleCardCutInMenu's CustomFigure dims the scene behind the portrait strips.
    static constexpr float screen[] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1,
                                       -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
    const float dim[] = {0, 0, 0, 0.5f};
    if (white.id) fullFrameQuad(white, screen, dim);
    dev->clearDepth();
    dev->setLighting(cr3::SceneLighting());
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);

    const std::string layout = "btcardcutin" + (count == 1 ? std::string() : std::to_string(count));
    const std::string position = layout + "position";
    const std::string menu = layout + "menu";
    const float previousZ = cr3UiZ;
    cr3UiZ = 0;
    UiFig& pos = cr3UiPose(position, count == 1 ? position + "_in" : position, elapsed, 0, 0);
    float x, y;
    if (cr3UiCall(pos, "call_btCardCutIn", x, y)) {
        cr3UiZ = 0.1f;
        UiFig& strips = cr3UiPose(menu, count == 1 ? menu : menu + "_in", elapsed, x, y);
        for (size_t k = 0; k < count; ++k) {
            cr3::Mat34 anchor;
            if (!cr3JointMatrix(strips, "call_btCardCutIn_id00_00" + std::to_string(k + 1), anchor)) continue;
            cr3UiZ = anchor.m[11];
            UiFig& portrait = cr3UiPose("btcardcutin_id00", "", 0, anchor.m[3], anchor.m[7]);
            const auto* gene = game.cr3Db ? game.cr3Db->geneInfo(geneIds[k]) : nullptr;
            char imageName[32];
            std::snprintf(imageName, sizeof imageName, "cutin_%03d", gene ? gene->series : geneIds[k]);
            cr3UiMaterial(portrait, "mat_cutin_001", imageName);
            portrait.fig.tint[3] = 1;
            drawFigure(portrait.fig, view.data());
        }
    }

    // BattleCard3CutInMenu keeps its own 1.2-second lifetime and delays each subsequent
    // nameplate by one frame. The rarity emblem holds the frame of the gene's grade.
    if (elapsed < 1.2f) {
        cr3UiZ = 0;
        UiFig& names = cr3UiPose("btcardnameposition", "", 0, 0, 0);
        for (size_t k = 0; k < count; ++k) {
            const float nameTime = elapsed - float(k) / 30;
            if (nameTime < 0) break;
            cr3::Mat34 anchor;
            if (!cr3JointMatrix(names, "call_btCardCutInPMenu_00" + std::to_string(k + 1), anchor)) continue;
            cr3UiZ = anchor.m[11] + 0.1f;
            UiFig& panel = cr3UiPut("btcardcutinpmenu", "", nameTime, anchor.m[3], anchor.m[7], view);
            if (const auto* gene = game.cr3Db ? game.cr3Db->geneInfo(geneIds[k]) : nullptr;
                gene && cr3JointMatrix(panel, "call_btCardCutInR_fr00", anchor)) {
                cr3UiZ = anchor.m[11] + 0.1f;
                cr3UiPut("btcardcutinr_fr00", "", float(gene->grade) / 30, anchor.m[3], anchor.m[7], view);
            }
            if (cr3UiCall(panel, "typeM_C01", x, y))
                drawUiText(cr3NameOf("gene_name.mbe", "GeneName", geneIds[k]), 30, x / 10, y / 10, w, h, false, 1.0f, 0.28f);
        }
    }
    cr3UiZ = previousZ;
    // (the announcement banner - OpenAnnounce's name, a cooperation's " II" - over the cut-in: footage, Part 9 28:29)
    if (!cr3Announce.text.empty() && cr3BattleClock < cr3Announce.at + cr3Announce.seconds) {
        dev->clearDepth();
        drawCr3Announce(view, w, h);
    }
}

void Viewer::drawCr3BattleHud(int w, int h) {
    if (!cr3BattleMode) return;
    const bool hud = cr3BattleStage >= Cr3BattleStage::Menu && cr3BattleStage <= Cr3BattleStage::Action;
    if (cr3BattleStage == Cr3BattleStage::Result) return drawCr3Result(w, h);
    if (vs.screen == 6) return drawVsScore(w, h);
    if (vs.wildOpen) return drawVsWildCards(w, h);
    const bool announcing = !cr3Announce.text.empty() && cr3BattleClock < cr3Announce.at + cr3Announce.seconds;
    if (!hud && cr3Popups.empty() && !announcing) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    if (announcing) drawCr3Announce(view, w, h);
    float x, y;
    if (hud) {
        // the party's status (btStMenu: btStPanel each - the portrait bt_chaIcon_j<char>, the HP and
        // MP bars posed at their percent / 30 s, the numbers - SpriteNumId 5 and 4 places; HP under
        // 20% reddened)
        UiFig& pos = cr3UiPut("btstposition", "", 0, 0, 0, view);
        if (!cr3BattleUiClosed && cr3UiCall(pos, "call_btStMenu", x, y, w, h)) { // (Battle.CloseUI: BtlStatusUI closed)
            UiFig& menu = cr3UiPut("btstmenu", "", 0, x, y, view);
            int slot = 0;
            for (size_t f = 0; f < cr3Rules.fighters.size(); ++f) {
                const cr3::Cr3Fighter& ft = cr3Rules.fighters[f];
                if (!ft.player) continue;
                float px, py;
                if (!cr3UiCall(menu, "call_btStPanel_00" + std::to_string(++slot), px, py)) continue;
                UiFig& panel = cr3UiPut("btstpanel", "", 0, px, py, view);
                float cx, cy;
                if (cr3UiCall(panel, "call_btStChara_id", cx, cy)) {
                    char icon[32];
                    std::snprintf(icon, sizeof icon, "bt_chaicon_j%03d", ft.id);
                    UiFig& face = uiFig("btstchara_id00");
                    cr3UiTexture(face, icon);
                    cr3UiPut("btstchara_id00", "", 0, cx - 0.1f, cy, view);
                }
                const int hp = f < cr3HpShown.size() ? cr3HpShown[f] : ft.hp;
                const int mp = f < cr3MpShown.size() ? cr3MpShown[f] : ft.mp;
                const float hpPct = ft.maxHp > 0 ? float(hp) * 100 / float(ft.maxHp) : 0;
                const float mpPct = ft.maxMp > 0 ? float(mp) * 100 / float(ft.maxMp) : 0;
                if (cr3UiCall(panel, "call_btStHp_per", cx, cy)) cr3UiPut("btsthp_per", "", hpPct / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_btStMp_per", cx, cy)) cr3UiPut("btstmp_per", "", mpPct / 30, cx, cy, view);
                const float white[3] = {1, 1, 1}, low[3] = {1, 0.5f, 0.5f}, out[3] = {1, 0.25f, 0.25f};
                if (cr3UiCall(panel, "btStHpno", cx, cy)) cr3SpriteNum(hp, 5, cx, cy, w, h, hpPct <= 0 ? out : hpPct < 20 ? low : white);
                if (cr3UiCall(panel, "btStMpno", cx, cy)) cr3SpriteNum(mp, 4, cx, cy, w, h, white);
                // (btStPanel::SetCharaAttribute 0x00a0f2f4 - SetParamNumber kind 8 -: btStAtbIcon_fr00 at the unit's
                // element / 30 - the worn gene's to begin with, then ChangeElement's)
                const int element = f < cr3ElementShown.size() ? cr3ElementShown[f] : ft.element;
                if (cr3UiCall(panel, "call_btStAtbIcon_fr00", cx, cy)) cr3UiPut("btstatbicon_fr00", "", float(element) / 30, cx, cy, view);
                // (SetAbnormalityStatusIcon 0x00a0f0b4: one btStIcon_fr00 at its icon / 30, hidden with none)
                if (const int icon = cr3StatusIcon(f); icon >= 0 && cr3UiCall(panel, "call_btStIcon_fr00", cx, cy))
                    cr3UiPut("btsticon_fr00", "", float(icon) / 30, cx, cy, view);
            }
        }
        // the break gauge (btBrkMenu: btBrkWinPanel looping, btBrkGauge at ((v + 1000) / 20) / 30 s -
        // even 1.6333 -, btBrkName_fr00's frame: 0 Player, 1 Enemy, 2 Even, 3 a full break; the rules'
        // gauge, battle/cr3rules.hpp, as each hit lands)
        UiFig& bpos = cr3UiPut("btbrkposition", "", 0, 0, 0, view);
        if (!cr3BattleUiClosed && cr3UiCall(bpos, "call_btBrkMenu", x, y, w, h)) { // (and BtlBreakGaugeUI)
            UiFig& menu = cr3UiPut("btbrkmenu", "", 0, x, y, view);
            float cx, cy;
            if (cr3UiCall(menu, "call_btBrkWinPanel", cx, cy)) {
                UiFig& win = uiFig("btbrkwinpanel");
                const float len = win.fig.animLoaded ? std::max(win.fig.anim.duration(), 0.01f) : 1.0f;
                UiFig& wp = cr3UiPut("btbrkwinpanel", "", std::fmod(cr3BattleClock, len), cx, cy, view);
                float gx, gy;
                if (cr3UiCall(wp, "call_btBrkGauge", gx, gy))
                    cr3UiPut("btbrkgauge", "", float((std::clamp(cr3GaugeShown, -1000, 1000) + 1000) / 20) / 30.0f, gx, gy, view);
            }
            if (cr3UiCall(menu, "call_btBrkNamePanel", cx, cy)) {
                UiFig& np = cr3UiPut("btbrknamepanel", "btbrknamepanel_io", std::min(cr3BattleClock - cr3StageStart, 1.0f), cx, cy, view);
                float nx, ny;
                if (cr3UiCall(np, "call_btBrkName_fr00", nx, ny)) {
                    dev->clearDepth(); // (over the ring: its display priority's)
                    const int g = cr3GaugeShown;
                    const int frame = std::abs(g) >= 1000 ? 3 : g > 0 ? 0 : g < 0 ? 1 : 2;
                    cr3UiPut("btbrkname_fr00", "", float(frame) / 30.0f, nx, ny, view);
                }
            }
        }
        // BtlTotalDamageUI (btToMenu: a btToPanel a slot, top to bottom - call_btToPanel_001..003 -, the unit's
        // hits and total, its HP before and now; @ 0x009a4744 / 0x00a47584)
        if (cr3BattleStage == Cr3BattleStage::Action && cr3ComboSince >= 0 && cr3ActionPlay.spellCardAge < 0) {
            UiFig& tpos = cr3UiPut("bttoposition", "bttoposition_io", cr3BattleClock - cr3ComboSince, 0, 0, view);
            if (cr3UiCall(tpos, "call_btToMenu", x, y, w, h)) {
                UiFig& tmenu = cr3UiPut("bttomenu", "", 0, x, y, view);
                for (size_t i = 0; i < cr3Totals.size(); ++i) {
                    const Cr3TotalSlot& sl = cr3Totals[i];
                    if (sl.fighter < 0 || size_t(sl.fighter) >= cr3Rules.fighters.size()) continue;
                    const size_t f = size_t(sl.fighter);
                    const cr3::Cr3Fighter& ft = cr3Rules.fighters[f];
                    const cr3::Cr3BattleUnit& bu = cr3Battle.units[size_t(ft.unit)];
                    float px, py, cx, cy;
                    if (!cr3UiCall(tmenu, "call_btToPanel_00" + std::to_string(i + 1), px, py)) continue;
                    // (btcoPanel::ChangeCharaFace2 0x00a48f00: bt_targIcon_<model><variation> - vGetVariation: a
                    // character's "a", a monster's own -, its alpha bt_targIcon_<model>_At; hidden without both)
                    const std::string colour = "bt_targicon_" + bu.name + (ft.player || bu.variant.empty() ? std::string("a") : bu.variant);
                    const std::string alpha = "bt_targicon_" + bu.name + "_at";
                    if (!texture(colour) || !texture(alpha)) continue;
                    UiFig& panel = cr3UiPut("bttopanel", "", 0, px, py, view);
                    if (cr3UiCall(panel, "call_btToChara_id00", cx, cy)) {
                        UiFig& icon = uiFig("bttochara_id00");
                        cr3UiTexture(icon, colour, alpha);
                        cr3UiPut("bttochara_id00", "", 0, cx, cy, view);
                    }
                    if (cr3UiCall(panel, "call_btToHpBg", cx, cy)) cr3UiPut("bttohpbg", "", 0, cx, cy, view);
                    const int hp1 = f < cr3HpShown.size() ? cr3HpShown[f] : ft.hp;
                    const float pct0 = ft.maxHp > 0 ? std::clamp(float(sl.startHp) * 100 / float(ft.maxHp), 0.0f, 100.0f) : 0;
                    const float pct1 = ft.maxHp > 0 ? std::clamp(float(hp1) * 100 / float(ft.maxHp), 0.0f, 100.0f) : 0;
                    if (cr3UiCall(panel, "call_btToHp02_per", cx, cy)) cr3UiPut("bttohp02_per", "", pct0 / 30, cx, cy, view);
                    if (cr3UiCall(panel, "call_btToHp01_per", cx, cy)) cr3UiPut("bttohp01_per", "", pct1 / 30, cx, cy, view);
                    if (!sl.numbers) continue; // (SetNumberViewComboDamageNum: a lone hit that isn't damage - the bar alone)
                    if (cr3UiCall(panel, "call_btToNum_num00", cx, cy)) {
                        const std::string digits = std::to_string(std::clamp(sl.raw, 0, 999999));
                        const int n = std::clamp(int(digits.size()), 1, 6);
                        char lay[24];
                        std::snprintf(lay, sizeof lay, "bttonum_num%02d", n);
                        UiFig& num = cr3UiPut(lay, "", 0, cx, cy, view);
                        for (int d = 0; d < n; ++d) {
                            float dx, dy;
                            if (cr3UiCall(num, "call_btToNum_fr00_00" + std::to_string(d + 1), dx, dy))
                                cr3UiPut("bttonum_fr00", "", float(digits[size_t(d)] - '0') / 30, dx, dy, view);
                        }
                    }
                    if (cr3UiCall(panel, "call_btHitNum_num00", cx, cy)) {
                        const std::string digits = std::to_string(std::clamp(sl.hits, 1, 999));
                        const int n = std::clamp(int(digits.size()), 1, 3);
                        char lay[24];
                        std::snprintf(lay, sizeof lay, "bthitnum_num%02d", n);
                        UiFig& num = cr3UiPut(lay, "", 0, cx, cy, view);
                        for (int d = 0; d < n; ++d) {
                            float dx, dy;
                            if (cr3UiCall(num, "call_btHitNum_fr00_00" + std::to_string(d + 1), dx, dy))
                                cr3UiPut("bthitnum_fr00", "", float(digits[size_t(d)] - '0') / 30, dx, dy, view);
                        }
                    }
                }
            }
        }
    }
    cr3Taps.clear(), cr3AutoTaps.clear();
    const bool waiting = !(cr3BattleEvents && cr3BattleEvents->running());
    const bool asking = cr3BattleStage == Cr3BattleStage::Escape && cr3EscapeAsk;
    if (cr3AutoResult != 0 && cr3BattleStage != Cr3BattleStage::Won && cr3BattleStage != Cr3BattleStage::Result && cr3BattleStage != Cr3BattleStage::Lost) {
        // the marker while it runs (comBtnMenu::SetParamNumber 6 0xa4cdc0, the last tab pattern: comBackPosition,
        // comBtnTab_fr03 at its call_comBackMenu_fit3 - frame 0 Mass Attack, 1 Auto Battle, + 2 pressed - its
        // tapAreaS03 / E03 the tap; in every stage)
        UiFig& bp = cr3UiPut("combackposition", "", 0, 0, 0, view);
        if (cr3UiCall(bp, "call_comBackMenu_fit3", x, y, w, h)) {
            UiFig& tab = cr3UiPut("combtntab_fr03", "", float((cr3AutoResult == 6 ? 1 : 0) + (cr3MarkerPressed ? 2 : 0)) / 30, x, y, view);
            float ax, ay, bx, by;
            if (cr3UiCall(tab, "tapAreaS03", ax, ay) && cr3UiCall(tab, "tapAreaE03", bx, by)) cr3AutoTaps.push_back({ax, ay, bx, by, 100});
        }
    }
    if ((cr3BattleStage == Cr3BattleStage::Command || (cr3BattleStage == Cr3BattleStage::Menu && cr3AutoResult == 0) || asking) && waiting) {
        // the command buttons (comBtnMenu, its io animator as it opens; four at panels 004..007 -
        // comBtnMenu::SetParamNumber 5 - each comBtnPanel with comBtnBg_fr00 (its slot's frame),
        // comBtnIcon_fr00 and comBtnText_fr00 (the button's), comBtnR; the focused one's text
        // comBtnTextTap_fr00)
        UiFig& pos = cr3UiPut("combtnposition", "", 0, 0, 0, view);
        if (cr3UiCall(pos, "call_comBtnMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("combtnmenu", "combtnmenu_io", cr3BattleClock - cr3MenuStart, x, y, view);
            // (the get-set's three on panels 005..007: 0 Battle, 2 Escape, 3 Options;
            // the commands' four on 004..007; the escape's question two on 006..007: 10, 11)
            const bool getSet = cr3BattleStage == Cr3BattleStage::Menu;
            const int count = asking ? 2 : getSet ? 3 : 4, first = asking ? 6 : getSet ? 5 : 4;
            static constexpr int kGetSet[3] = {0, 2, 3};
            for (int b = 0; b < count; ++b) {
                float px, py;
                if (!cr3UiCall(menu, "call_comBtnPanel_00" + std::to_string(first + b), px, py)) continue;
                const int id = asking ? 10 + b : getSet ? kGetSet[b] : kCr3Buttons[b];
                // (Escape where forbidden; a command its status effects
                // forbid - BtlStatus::OpenMenuInterface: comBtn::SelectableSet(false), its parts'
                // diffuse (0.549, 0.549, 0.655))
                const bool off = asking ? false : getSet ? (b == 1 && !cr3EscapeAllowed()) : !cr3Rules.menuEnabled(cr3Chooser, b);
                cr3UiTint[0] = cr3UiTint[1] = off ? 0.549f : 1.0f, cr3UiTint[2] = off ? 0.655f : 1.0f; // (cr3UiPut's tint)
                const bool focused = b == cr3Focus && !(getSet && cr3TabLit); // (a lit tab: the cursor off the buttons)
                if (vitaButtons() && focused) {
                    cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
                    cr3UiTintMaterial = "mat_com_menuBtn_02";
                    cr3UiMaterialTint[0] = off ? 0.17f : 0.5f, cr3UiMaterialTint[1] = off ? 0.33f : 1.0f, cr3UiMaterialTint[2] = off ? 0.5f : 1.5f;
                }
                if (getSet && b == 0 && cr3TabsShown()) {
                    // (Battle's tabs - comBtn::Pose 0xa4a66c, cross-placed: comBtnTab_fr01 - Mass Attack - at the
                    // panel's call_comBtnTab_fr02, comBtnTab_fr02 - Auto Battle - at call_comBtnTab_fr01; comBtn::
                    // Render 0xa4a8dc: before the panel, behind it. Frame 0, lit 1; their own tap areas, ahead of
                    // the panel's)
                    const UiFig& at = cr3UiPose("combtnpanel", "", 0, px, py);
                    float m[2][2];
                    const bool placed = cr3UiCall(at, "call_comBtnTab_fr02", m[0][0], m[0][1]) && cr3UiCall(at, "call_comBtnTab_fr01", m[1][0], m[1][1]);
                    for (int tab = 0; tab < 2 && placed; ++tab) { // (0 Mass Attack, 1 Auto Battle)
                        const bool lit = cr3TabLit == (tab == 0 ? 2 : 1);
                        const std::string n = std::to_string(tab + 1);
                        UiFig& tf = cr3UiPut("combtntab_fr0" + n, "", lit ? 1.0f / 30 : 0, m[tab][0], m[tab][1], view);
                        float ax, ay, bx, by;
                        if (cr3UiCall(tf, "tapAreaS0" + n, ax, ay) && cr3UiCall(tf, "tapAreaE0" + n, bx, by))
                            cr3Taps.insert(cr3Taps.begin(), {ax, ay, bx, by, tab == 0 ? 5 : 6});
                    }
                }
                UiFig& panel = cr3UiPut("combtnpanel", "", 0, px, py, view);
                float cx, cy;
                if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(b) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnIcon_fr00", cx, cy)) cr3UiPut("combtnicon_fr00", "", float(id) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnText_fr00", cx, cy))
                    cr3UiPut(focused ? "combtntexttap_fr00" : "combtntext_fr00", "", float(id) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view);
                float ax, ay, bx, by;
                if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3Taps.push_back({ax, ay, bx, by, b});
                cr3UiTintMaterial.clear();
                cr3UiMaterialTint[0] = cr3UiMaterialTint[1] = cr3UiMaterialTint[2] = 1;
            }
            cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1.0f;
        }
    } else if (cr3BattleStage == Cr3BattleStage::Target && waiting) {
        // the targets (btTgtMenu, btTgtMenu_in; a btTgtPanel each at call_btTgtPanel_001..: the
        // name at typeM_C01, the HP bar btTgtBar_per, the icon bt_targIcon_<model><variant>)
        UiFig& pos = cr3UiPut("bttgtposition", "", 0, 0, 0, view);
        if (cr3UiCall(pos, "call_btTgtMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("bttgtmenu", "bttgtmenu_in", cr3BattleClock - cr3MenuStart, x, y, view);
            const std::vector<int> alive = cr3Targets();
            cr3::Cr3Command selected;
            const bool all = cr3Rules.command(cr3ChosenCommand, selected) && selected.all;
            for (size_t i = 0; i < alive.size() && i < 3; ++i) {
                const cr3::Cr3Fighter& ft = cr3Rules.fighters[size_t(alive[i])];
                const cr3::Cr3BattleUnit& bu = cr3Battle.units[size_t(ft.unit)];
                float px, py;
                if (!cr3UiCall(menu, "call_btTgtPanel_00" + std::to_string(i + 1), px, py)) continue;
                UiFig& panel = cr3UiPut("bttgtpanel", "", 0, px, py, view, (all ? cr3Focus == 0 : int(i) == cr3Focus) ? 1.0f : 0.85f);
                float cx, cy;
                const size_t ti = size_t(alive[i]);
                const int shownMp = ti < cr3MpShown.size() ? cr3MpShown[ti] : ft.mp;
                const float pct = selected.mpHeal()
                                      ? (ft.maxMp > 0 ? float(shownMp) * 100 / float(ft.maxMp) : 0)
                                      : (ft.maxHp > 0 ? float(cr3HpShown[ti]) * 100 / float(ft.maxHp) : 0);
                if (cr3UiCall(panel, "call_btTgtBar_per", cx, cy)) cr3UiPut("bttgtbar_per", "", pct / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(i) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_btTgtChara_id00", cx, cy)) {
                    UiFig& icon = uiFig("bttgtchara_id00");
                    cr3UiTexture(icon, "bt_targicon_" + bu.name + (bu.variant.empty() ? std::string("a") : bu.variant), "bt_targicon_" + bu.name + "_a");
                    cr3UiPut("bttgtchara_id00", "", 0, cx, cy, view);
                }
                if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view);
                // (TargetSelectButton::SetParameter 0xb46a68 - both sides' units: BtlMonsterStatus::VAddTargetListInterface
                // 0x9c6d94 / BtlStatus:: 0x9b31dc -: the element - btStAtbIcon_fr00 at call_btStAtbIcon_fr00, frame
                // BtlStatus::GetElement / 30, none at -1 -, the level - TargetParam +0x0c, 999 at most: btTgtLv_num0N's N
                // btTgtLvNum_fr00 digits -, the status icon - cr3TargetIcon - at call_btStIcon_fr00)
                const int element = ti < cr3ElementShown.size() ? cr3ElementShown[ti] : ft.element;
                if (element >= 0 && cr3UiCall(panel, "call_btStAtbIcon_fr00", cx, cy)) cr3UiPut("btstatbicon_fr00", "", float(element) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_btTgtLv_num00", cx, cy)) {
                    const std::string digits = std::to_string(std::clamp(ft.level, 0, 999));
                    UiFig& lv = cr3UiPut("bttgtlv_num0" + std::to_string(digits.size()), "", 0, cx, cy, view);
                    for (size_t d = 0; d < digits.size(); ++d)
                        if (float dx, dy; cr3UiCall(lv, "call_btTgtLvNum_fr00_00" + std::to_string(d + 1), dx, dy))
                            cr3UiPut("bttgtlvnum_fr00", "", float(digits[d] - '0') / 30, dx, dy, view);
                }
                if (const int icon = cr3TargetIcon(ti); icon >= 0 && cr3UiCall(panel, "call_btStIcon_fr00", cx, cy))
                    cr3UiPut("btsticon_fr00", "", float(icon) / 30, cx, cy, view);
                if (cr3UiCall(panel, "typeM_C01", cx, cy)) {
                    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                    drawUiText(cr3FighterName(ft), 30, cx / 10, cy / 10, w, h, false, 0.3f); // (dark on the panel)
                }
                float ax, ay, bx, by;
                if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3Taps.push_back({ax, ay, bx, by, int(i)});
            }
            // (BtlMonsterStatus::VAddTargetListInterface 0x009c6d94: the monster's help - TargetParam +0x28 - only where
            // its species was analyzed this battle - BtlMonsterAnalyzation::Has -, else none; on the top banner while
            // it is targeted)
            if (!all && cr3Focus >= 0 && size_t(cr3Focus) < alive.size() && !announcing) {
                const cr3::Cr3Fighter& ft = cr3Rules.fighters[size_t(alive[size_t(cr3Focus)])];
                const std::string text = !ft.player && cr3Analyzed.count(ft.id) ? cr3NameOf("mon_help.mbe", "MonsterHelp", ft.id) : std::string();
                UiFig& hp = cr3UiPut("bthelpposition", "", 0, 0, 0, view);
                float hx, hy, tx, ty;
                if (!text.empty() && cr3UiCall(hp, "call_btHelpMenu", hx, hy, w, h)) {
                    UiFig& help = cr3UiPut("bthelpmenu", "", 0, hx, hy, view);
                    if (cr3UiCall(help, "typeM_L01", tx, ty)) {
                        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                        drawUiText(text, 26, (tx + 0.075f) / 10, (ty - 0.15f) / 10, w, h, true, 1.0f);
                    }
                }
            }
            if (cr3TargetCanChange()) {
                const bool focused = cr3Focus == (all ? 1 : int(alive.size()));
                float px, py, cx, cy;
                if (cr3UiCall(menu, "call_btTgtChangePanel", px, py)) {
                    if (focused) {
                        cr3UiTintMaterial = "mat_bt_parts_03";
                        cr3UiMaterialTint[0] = 0.5f, cr3UiMaterialTint[1] = 1, cr3UiMaterialTint[2] = 1.5f;
                    }
                    UiFig& change = cr3UiPut("bttgtchangepanel", "", 0, px, py, view);
                    if (cr3UiCall(change, "call_btTgtChange_fr", cx, cy)) cr3UiPut("bttgtchange_fr00", "", cr3TargetChanged ? 1.0f / 30 : 0, cx, cy, view);
                    cr3UiTintMaterial.clear();
                    cr3UiMaterialTint[0] = cr3UiMaterialTint[1] = cr3UiMaterialTint[2] = 1;
                    float ax, ay, ex, ey;
                    if (cr3UiCall(change, "tapAreaCP01", ax, ay) && cr3UiCall(change, "tapAreaAP01", ex, ey)) {
                        const float r = std::hypot(ex - ax, ey - ay);
                        cr3Taps.push_back({ax - r, ay - r, ax + r, ay + r, -2});
                    }
                }
            }
        }
    }
    if (cr3BattleStage == Cr3BattleStage::List && waiting) {
        // the list (comListMenu, interface 0x2d: comListPosition and its io animator, rows of
        // comListPanel 0.76 apart from call_comListPanel - the name at typeM_L01, the cost in
        // comListNumM_num00's digits, the count in comListNumI; the cursor comListCursor on
        // the focused; what can't be used dimmed)
        UiFig& pos = cr3UiPut("comlistposition", "comlistposition_io", cr3BattleClock - cr3MenuStart, 0, 0, view);
        if (cr3UiCall(pos, "call_comListMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
            float lx, ly;
            // (its category: comListCategoryPanel - comListCBg / CIcon / Ctext at the list's frame:
            // 0 Genes, 1 Items)
            if (cr3UiCall(menu, "call_comListCategoryPanel", lx, ly)) {
                UiFig& cat = cr3UiPut("comlistcategorypanel", "", 0, lx, ly, view);
                const float frame = (cr3ListItems ? float(1 + cr3ItemCategory) : 0.0f) / 30;
                float cx, cy;
                if (cr3UiCall(cat, "call_comListCBg_fr00", cx, cy)) cr3UiPut("comlistcbg_fr00", "", frame, cx, cy, view);
                if (cr3UiCall(cat, "call_comListCIcon_fr00", cx, cy)) cr3UiPut("comlistcicon_fr00", "", frame, cx, cy, view);
                if (cr3UiCall(cat, "call_comListCtext_fr00", cx, cy)) cr3UiPut("comlistctext_fr00", "", frame, cx, cy, view);
            }
            // (the focused one's help across the top: btHelpMenu at btHelpPosition's fit8, its
            // line at typeM_L01 - command_help.mbe's)
            if (cr3Focus >= 0 && size_t(cr3Focus) < cr3List.size()) {
                UiFig& hp = cr3UiPut("bthelpposition", "", 0, 0, 0, view);
                float hx, hy;
                if (cr3UiCall(hp, "call_btHelpMenu", hx, hy, w, h)) {
                    UiFig& help = cr3UiPut("bthelpmenu", "", 0, hx, hy, view);
                    float tx, ty;
                    const Cr3ListRow& row = cr3List[size_t(cr3Focus)];
                    const std::string text = row.item ? cr3ItemText(row.item, true)
                                                      : cr3NameOf("command_help.mbe", "CommandHelp", row.command);
                    if (!text.empty() && cr3UiCall(help, "typeM_L01", tx, ty)) {
                        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                        drawUiText(text, 26, (tx + 0.075f) / 10, (ty - 0.15f) / 10, w, h, true, 1.0f);
                    }
                }
            }
            const int listN = int(cr3List.size());
            if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
                // (BtlItemListUI / BtlCommandListUI: a comListMenu - drawCr3ListEnds -, no bar - as the reference)
                drawCr3ListEnds(lx, ly, listN, cr3Focus, view);
            }
            if (cr3UiCall(menu, "call_comListPanel", lx, ly))
                for (int k = std::max(cr3Focus - 2, 0); k < listN && k <= cr3Focus + 2; ++k) {
                    const Cr3ListRow& row = cr3List[size_t(k)];
                    const float ry = cr3ListRowY(ly, k, cr3Focus - 2, cr3Focus), alpha = 1;
                    Cr3FocusRow focusRow(*this, k == cr3Focus, row.usable);
                    UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, k, cr3Focus), ry, view, alpha);
                    float cx, cy;
                    if (int(k) == cr3Focus && row.usable && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                    // (comListItem::Initialize 0xa56620: comListIcon_fr00 only for an icon of 0 or more - SetIconPattern 0x880848)
                    if (row.icon >= 0 && cr3UiCall(panel, "call_comListIcon_fr00", cx, cy))
                        cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), row.icon), cx, cy, view, alpha);
                    const int number = row.item ? row.count : row.cost;
                    if (cr3UiCall(panel, row.item ? "call_comListNumI_num00" : "call_comListNumM_num00", cx, cy)) {
                        UiFig& num = cr3UiPut(row.item ? "comlistnumi_num00" : "comlistnumm_num00", "", 0, cx, cy, view, alpha);
                        const std::string digits = std::to_string(std::max(number, 0));
                        int slots = 0;
                        for (const std::string& j : num.joints) slots += j.rfind("call_comListNum_fr00_", 0) == 0;
                        for (int d = 0; d < int(digits.size()) && d < slots; ++d) {
                            float dx, dy; // (right-aligned in its slots)
                            if (cr3UiCall(num, "call_comListNum_fr00_00" + std::to_string(slots - int(digits.size()) + d + 1), dx, dy))
                                cr3UiPut("comlistnum_fr00", "", float(digits[size_t(d)] - '0') / 30, dx, dy, view, alpha);
                        }
                    }
                    if (cr3UiCall(panel, "typeM_L01", cx, cy)) {
                        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                        // (ProvisionalMenuText: the line's box hangs from the joint - its middle ~0.17 below)
                        const float ks = cr3UiFocus ? 1.1f : 1.0f; // (focused: white, 1.1 x)
                        drawUiText(row.name, 26 * ks, (cx + 0.075f * ks) / 10, (cy - 0.15f * ks) / 10, w, h, true, cr3UiFocus ? 1.0f : row.usable ? 0.3f : 0.15f); // (dark on the light row; not usable half as bright)
                    }
                    float ax, ay, bx, by;
                    if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3Taps.push_back({ax, ay, bx, by, int(k)});
                }
        }
    }
    // the damage (DamageInfo: btNumMenu - btNumMenu_in over its first 0.53 s - the digits'
    // layout btNum_num0N, a btNum_fr00 frame each), over the target where it was hit
    for (auto it = cr3Popups.begin(); it != cr3Popups.end();) {
        const float age = cr3BattleClock - it->start;
        if (age > 1.3f) {
            it = cr3Popups.erase(it);
            continue;
        }
        const float* m = cr3BattleVP.data();
        const float* p = it->at.data();
        const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        if (cw > 0) {
            const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
            const float sx = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw * float(frameW) * 0.5f / k;
            const float sy = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw * float(frameH) * 0.5f / k;
            const int n = int(std::to_string(std::max(it->value, 0)).size());
            const float alpha = age > 1.0f ? 1.0f - (age - 1.0f) / 0.3f : 1.0f;
            if (it->bonus > 0) {
                // (DamageInfoMes: btBonusMenu, btBonusMenu_in; btBonus_fr00 at call_btBonus_fr,
                // frame kind - 1 - kinds 1, 3, 6 raised 1.2 -, btBonusBg_fr00 at
                // call_btBonusBg_fr, frame 1 for kinds 4 / 5)
                UiFig& bm = cr3UiPut("btbonusmenu", "btbonusmenu_in", std::min(age, 1.0f), sx, sy, view, alpha);
                float bx, by;
                if (cr3UiCall(bm, "call_btBonusBg_fr", bx, by))
                    cr3UiPut("btbonusbg_fr00", "", (it->bonus | 1) == 5 ? 1.0f / 30.0f : 0.0f, bx, by, view, alpha);
                if (cr3UiCall(bm, "call_btBonus_fr", bx, by)) {
                    const bool raised = it->bonus == 1 || it->bonus == 3 || it->bonus == 6;
                    cr3UiPut("btbonus_fr00", "", float(it->bonus - 1) / 30.0f, bx, by + (raised ? 1.2f : 0.0f), view, alpha);
                }
                ++it;
                continue;
            }
            UiFig& menu = cr3UiPut("btnummenu", "btnummenu_in", std::min(age, 0.5333f), sx, sy, view, alpha);
            float lx, ly;
            if (cr3UiCall(menu, "call_btNum_num", lx, ly)) {
                char lay[16];
                std::snprintf(lay, sizeof lay, "btnum_num%02d", std::clamp(n, 1, 6));
                UiFig& layout = cr3UiPut(lay, "", 0, lx, ly, view, alpha);
                const std::string digits = std::to_string(std::max(it->value, 0));
                for (int d = 0; d < n; ++d) {
                    float dx, dy;
                    if (cr3UiCall(layout, "call_btNum_fr00_00" + std::to_string(d + 1), dx, dy))
                        cr3UiPut("btnum_fr00", "", float(std::clamp(it->color, 0, 4) * 10 + (digits[size_t(d)] - '0')) / 30, dx, dy, view, alpha);
                }
            }
        }
        ++it;
    }
}

} // namespace cr3_viewer
