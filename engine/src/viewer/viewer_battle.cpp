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
        // auto battle off before a scripted event - CR3-U has no auto battle to stop)
        vm.setNative("Battle", "StopAuto", [](cr3::NativeCall&) {});
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
        // turn's end script: playCr3ScriptCommands)
        vm.setNative("BattleCommand", "Add", [this](cr3::NativeCall& c) {
            if (c.integer(0, -1) >= 0) cr3Rules.scripted[c.integer(0)].push_back(c.integer(2));
        });
        vm.setNative("BattleCoupling", "SetId", [this](cr3::NativeCall& c) { cr3NextCoupling = c.integer(0, -1); });
        vm.setNative("BattleCommand", "SetId", [this](cr3::NativeCall& c) { cr3ScriptCommands.push_back({c.integer(0), -1}); });
        vm.setNative("BattleCommand", "SetIdAndTarget", [this](cr3::NativeCall& c) { cr3ScriptCommands.push_back({c.integer(0), c.integer(1, -1)}); });
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

// (a name's "{source}" - BtlDamageChunk::OpenAnnounce - and "{assistant}": the actor, the
// partner)
std::string Viewer::cr3FighterName(const cr3::Cr3Fighter& f) {
    return f.player ? cr3NameOf("char_name.mbe", "CharName", f.id) : cr3NameOf("mon_name.mbe", "MonsterName", f.id);
}

// the status effects an action gave (BtlDamageTargetList::openStatusEffectAnnounce): those with
// lines (StatusEffect +0x74 / +0x78 - a monster's, a character's -, -1 none); one on one target its
// line, "{target}" the target; several on one the command's (+0xe8), else 99 (a monster) / 0x6a;
// one alike on several targets its +0x76 / +0x7a; else as several
void Viewer::announceCr3Effects(const cr3::Cr3Action& a) {
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

void Viewer::rollCr3Drop(int fi) {
    const cr3::Cr3Fighter& f = cr3Rules.fighters[size_t(fi)];
    if (f.player || cr3Drops.count(fi)) return;
    cr3Drops[fi] = 0;
    cr3::MbeFile::Row r;
    if (!cr3BattleTables.monster.find("Monster", f.id, "iipiiiiiiiiiiiiiiiiiiiiiiiiiiiiii", r)) return;
    const int kind0 = r.i(27), kind1 = r.i(30);
    if (kind0 == 8) return;
    int dropBoost = 0; // (BtlStatusList::GetDropRateCorrection @ 0x009b01e0: living party skill 85)
    for (const cr3::Cr3Fighter& pf : cr3Rules.fighters)
        if (pf.player && !pf.dead())
            for (int c : cr3Rules.skillConstants(pf, 85)) dropBoost += c;
    const int c0 = kind0 > 0 && r.i(29) > 0 ? std::max(r.i(29) + dropBoost, 0) : 0;
    const int c1 = kind1 > 0 && kind1 != 8 && r.i(32) > 0 ? std::max(r.i(32) + dropBoost, 0) : 0;
    const int roll = int(SDL_rand(256));
    const int pick = roll < c0 ? 0 : roll < c0 + c1 ? 1 : -1;
    if (pick < 0) return;
    const int kind = pick ? kind1 : kind0, id = pick ? r.i(31) : r.i(28);
    if (kind < 1 || kind > 7 || id <= 0) return;
    const int full = (kind - 1) * 100000 + id;
    cr3Drops[fi] = full;
    std::string text = announceText("AnnounceBattle", 0x1d);
    if (const size_t at = text.find("{item.name}"); at != std::string::npos) text.replace(at, 11, cr3ItemText(full, false));
    setCr3Announce(text, 1.0f);
    audio.playSe("com_013", false);
    SDL_Log("battle %.2f s: fighter %d drops item %d", cr3BattleClock, fi, full);
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
    } else if (stage == 2 && id == kCtnYes) { // (given up: the defeat - BtlGameOverUI isn't in; on as lost)
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
void Viewer::loadCr3BattleUnits(size_t from) {
    std::string err;
    int monsters = 0;
    for (size_t k = 0; k < cr3Battle.units.size(); ++k) {
        const cr3::Cr3BattleUnit& bu = cr3Battle.units[k];
        Cr3BattleUnitView& u = cr3BattleUnits[k];
        if (!bu.player) u.phase = float(monsters++) + 0.25f; // (Standby: its slot's 0 / 1 / 2, + up to 0.5 at random)
        if (k < from) continue;
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
    if (gaugeArg != 0) cr3Rules.gauge = std::clamp(gaugeArg * 250, -1000, 1000), cr3Rules.gaugeStarted = true, SDL_Log("battle: the break gauge at %d (test)", cr3Rules.gauge); // (--gauge -4 .. 4: tests)
    for (size_t k = 0, p = 0; k < cr3Rules.fighters.size() && p < party.size(); ++k)
        if (cr3Rules.fighters[k].player) {
            const int unit = cr3Rules.fighters[k].unit;
            cr3Rules.fighters[k] = party[p++], cr3Rules.fighters[k].unit = unit;
        }
    cr3Rules.gauge = gauge;
    cr3HpShown.clear(), cr3MpShown.clear();
    cr3ComboStartHp.clear(), cr3ComboDamage.clear(), cr3ComboHits.clear(), cr3ComboSince = -1;
    cr3VictorUnit = -1, cr3VictoryDuration = 2.5f;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters) cr3HpShown.push_back(f.hp), cr3MpShown.push_back(f.mp);
    for (size_t k = 0; k < cr3Rules.fighters.size(); ++k) {
        const cr3::Cr3Fighter& f = cr3Rules.fighters[k];
        if (!f.player) SDL_Log("battle %d: fighter %zu - monster %d, level %d, HP %d", coupling, k, f.id, f.level, f.maxHp);
    }
    cr3Drops.clear(), cr3Choices.clear(), cr3Actions.clear(), cr3Popups.clear();
    SDL_Log("battle %d: the next wave - battle %d, %zu units", cr3Battle.coupling, coupling, cr3Battle.units.size());
    playCr3Camera(cr3Battle.cameras[1], 1.0f, false, {0, 0, 0}, 0, 0);
    cr3BattleStage = Cr3BattleStage::Action; // (phaseCallTurnEndEvent: the new script's turn's end, then the next turn)
    runCr3BattleScript("Battle_Turn_End_Base");
    playCr3ScriptCommands();
    beginCr3Turn();
}

bool Viewer::startCr3Battle(int coupling, int fieldId) {
    cr3BattleLost = false, cr3Escaped = false, cr3ScriptCommands.clear(), cr3NextCoupling = -1, cr3WaveExp = cr3WaveMoney = 0, cr3WaveDrops.clear(), cr3VictoryIn = false, cr3VictorUnit = -1, cr3VictoryDuration = 2.5f, cr3Drops.clear(), cr3Ctn = Cr3Continue(), cr3GameOverAt = -1; // (BtlSystem::Init: s_ResultParam -1)
    clearCr3Effects();
    cr3Held.clear(), cr3StartCommand = {-1, -1}, cr3BattleUiClosed = false; // (the scripts' holds, opening command, UI)
    cr3FieldLoop = true, cr3FieldEnd = -1, cr3FieldTime = cr3FieldDrawnAt = 0;
    for (Cr3FieldObject& o : cr3FieldObjects) unload(o.figure);
    for (auto& [slot, m] : cr3ScriptModels) unload(m.figure);
    cr3FieldObjects.clear(), cr3ScriptModels.clear();
    cr3Turn = 0, cr3ForcedEnd = -1, cr3BattleFieldId = fieldId, cr3FieldEffects.clear(); // (BtlResult's: 0, +4 / +8 none, factors 1)
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
    cr3PartyRules();
    cr3Rules.partyMpLeft = game.cr3Mp; // (the MP the last battle left)
    cr3Rules.lastAttackLevel = game.cr3LastAttackLevel, cr3Rules.finestArtsLevel = game.cr3FinestArtsLevel; // (the finishers' tiers: CrxCharacterData +0x14 / +0x18)
    if (!cr3Rules.setup(cr3BattleTables, cr3Battle, game.next.battleLevel, seed, &err)) SDL_Log("battle rules: %s", err.c_str());
    if (gaugeArg != 0) cr3Rules.gauge = std::clamp(gaugeArg * 250, -1000, 1000), cr3Rules.gaugeStarted = true, SDL_Log("battle: the break gauge at %d (test)", cr3Rules.gauge); // (--gauge -4 .. 4: tests)
    for (size_t k = 0; k < cr3Rules.fighters.size(); ++k) {
        const cr3::Cr3Fighter& f = cr3Rules.fighters[k];
        SDL_Log("battle %d: fighter %zu - %s %d, level %d, HP %d", coupling, k, f.player ? "character" : "monster", f.id, f.level, f.maxHp);
    }
    cr3HpShown.clear(), cr3MpShown.clear();
    cr3ComboStartHp.clear(), cr3ComboDamage.clear(), cr3ComboHits.clear(), cr3ComboSince = -1;
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
    // (BtlSysStateStart::phaseStart - GiveFieldStatusEffect: the field's effects on every unit, for
    // good; none on one with a skill of kind 0x6a)
    for (size_t k = 0; k < cr3Rules.fighters.size() && !cr3FieldEffects.empty(); ++k) {
        if (!cr3Rules.skillConstants(cr3Rules.fighters[k], 0x6a).empty()) continue;
        for (int e : cr3FieldEffects) cr3Rules.giveEffect(int(k), e, true);
    }
    if (!cr3FieldEffects.empty()) SDL_Log("battle %d: %zu field status effects", coupling, cr3FieldEffects.size());
    playCr3Camera(cr3Battle.cameras[0], cr3Battle.getSetEnd, false, {0, 0, 0}, 0, 0); // (PlayBattleIn)
    cr3BattleMode = true;
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
    // end held; then (the battle's start script, the announcements: not yet) the first
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
            cr3Actions = cr3Rules.turn(cr3Choices);
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
        if (!inputScript.empty() && inputScript.front() == "vs:next" && uiClock - vs.scoreAt > 1.0f) inputScript.pop_front(), vsBattleOver(true);
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
        if (!inputScript.empty() && inputScript.front() == "result" && cr3BattleClock - cr3StageStart > 1.5f) inputScript.pop_front(), endCr3Battle();
        else if (autoBattle && cr3BattleClock - cr3StageStart > 1.5f) endCr3Battle(); // (--input auto: the result taken)
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

void Viewer::cr3HitEffects(int command, int element, size_t targetUnit, bool critical) {
    cr3::MbeFile::Row d;
    if (!cr3BattleTables.direction.find("CommandDirection", command, "iiiiiii_piiiiiiiiiiiiiiiii", d)) return;
    const uint32_t flags = uint32_t(d.i(20)); // (+0x58)
    float scale = 1;
    if (flags & 0x80) scale = (SDL_randf() * 2 - 1) * d.f(24) + 1; // (+0x68)
    const std::array<float, 3> at = unitPoint(targetUnit, "DummyDP01", 1.2f);
    if (cr3ActionPlay.timing.effects.empty()) spawnCr3Effect(d.i(9), at, scale);
    if (flags & 0x100) { // (+0x59 bit 0)
        if (critical) spawnCr3Effect(4, at, scale);
        if (element > 0) spawnCr3Effect(element + 4, at, scale);
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

void Viewer::stepCr3ActionEffects(const cr3::Cr3Action& action, size_t source, float t) {
    using Placement = cr3::Cr3BattleTables::ActionTiming::Placement;
    auto& play = cr3ActionPlay;
    const auto& timing = play.timing;
    while (play.nextEffect < timing.effects.size() && t >= timing.effects[play.nextEffect].time) {
        const auto& cue = timing.effects[play.nextEffect++];
        const float start = cr3ActionStart + cue.time;
        if (cue.launch) {
            for (const auto& target : action.targets)
                launchCr3Projectile(source, size_t(cr3Rules.fighters[size_t(target.target)].unit), start);
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

void Viewer::beginCr3Turn(bool countTurn) {
    if (countTurn) ++cr3Turn; // (BtlSysStateSelect::phaseStart)
    cr3Choices.clear(), cr3TurnItems.clear();
    cr3ComboStartHp.clear(), cr3ComboDamage.clear(), cr3ComboHits.clear(), cr3ComboSince = -1;
    cr3Chooser = -1;
    cr3BattleStage = Cr3BattleStage::Menu, cr3StageStart = cr3MenuStart = cr3BattleClock, cr3Focus = 0;
    playCr3Camera(cr3Battle.cameras[0], cr3Battle.getSetEnd, false, {0, 0, 0}, 0, 0);
    cr3Cam.held = true;
    if (!cr3GetSetSeen) audio.playSe("bt_001", false), cr3GetSetSeen = true;
}

bool Viewer::cr3EscapeAllowed() const { return (cr3Battle.couplingFlags & 2) == 0; }

// the get-set menu's choice: 0 Battle, 1 Escape, 2 Options
void Viewer::chooseCr3GetSet(int b) {
    if (b == 1 && !cr3EscapeAllowed()) return (void)audio.playSe("com_005", false);
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
        cr3Actions = cr3Rules.turn(cr3Choices);
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
            row.command = id, row.name = cr3NameOf("command_name.mbe", "CommandName", id), row.cost = cr3Rules.mpCost(f, c);
            const int hCost = cr3Rules.hpCost(f, c);
            row.usable = row.cost <= f.mp && (hCost == 0 || hCost < f.hp) && cr3CommandHasTarget(c);
            cr3List.push_back(row);
        }
        for (int id : f.geneCommands) {
            cr3::Cr3Command c;
            if (!cr3Rules.command(id, c) || c.type < 1 || c.type > 4) continue;
            Cr3ListRow row;
            row.command = id, row.name = cr3NameOf("command_name.mbe", "CommandName", id), row.cost = cr3Rules.mpCost(f, c);
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
            row.item = id, row.command = command, row.count = n, row.name = cr3ItemText(id, false);
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
    cr3Actions = cr3Rules.turn(cr3Choices);
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
        cr3ChosenCommand = cr3Rules.fighters[size_t(cr3Chooser)].attackCommand, cr3ChosenItem = 0;
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
    cr3Choices.push_back({cr3Chooser, cr3ChosenCommand >= 0 ? cr3ChosenCommand : cr3Rules.fighters[size_t(cr3Chooser)].attackCommand, target});
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
    // (hideOtherThanTarget: all but the attacker and its targets)
    for (size_t k = 0; k < cr3BattleUnits.size(); ++k) {
        bool keep = k == su;
        for (const cr3::Cr3Hit& t : a.targets) keep |= cr3Rules.fighters[size_t(t.target)].unit == int(k);
        cr3BattleUnits[k].directionHidden = !keep;
    }
    cr3LastAttack = Cr3LastAttack();
    cr3LastAttack.on = true, cr3LastAttack.unit = su, cr3LastAttack.finestArts = finestArts;
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
    SDL_Log("battle %.2f s: a %s (kind %d, motion %d) - camera %s, white %s", cr3BattleClock, finestArts ? "finest arts" : "last attack", kind, ext,
            camera.empty() ? "(none)" : camera.c_str(), white >= 0 ? std::to_string(int(white)).c_str() : "none");
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
        playCr3Camera(cr3Battle.cameras[4], 1.0f, false, {}, 0, 0);
        cr3White.begin(cr3BattleClock, 1, 0, 1.0f);
        SDL_Log("battle %.2f s: the finest arts' white lifted", cr3BattleClock);
    }
}

void Viewer::endCr3LastAttack() {
    if (!cr3LastAttack.on) return;
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
        case 2: {
            cr3::MbeFile::Row row;
            std::string name = "Cam_bt_up_PC_00";
            if (cr3Battle.cameraSize == 0) name = "Cam_bt_colosseum_00";
            else if (!foe.player && cr3BattleTables.model.find("Camera", foe.cameraSize, "ifpppppppppp", row)) name = row.s(11);
            const cr3::Vec3 point = cr3Battle.cameraSize == 0 ? cr3::Vec3{} : unitPoint(target, "DummyCP", 1.0f);
            playCr3Camera(name, duration, false, point, foe.player ? 0 : foe.yaw, 0);
            break;
        }
        case 4:
            playCr3Camera(cr3Battle.cameras[4], duration, false, {}, 0, 0);
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
        play.end = std::max(play.end, play.timing.hits.back() + 0.9f);
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
    const cr3::Cr3Fighter& s = cr3Rules.fighters[size_t(a.source)];
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
    if (!s.player) { // (BtlTotalDamageUI: reset player combo chain when an enemy acts)
        cr3ComboStartHp.clear(), cr3ComboDamage.clear(), cr3ComboHits.clear(), cr3ComboSince = -1;
    } else if (cr3ComboStartHp.size() != cr3Rules.fighters.size()) {
        cr3ComboStartHp = cr3HpShown;
        cr3ComboDamage.assign(cr3Rules.fighters.size(), 0);
        cr3ComboHits.assign(cr3Rules.fighters.size(), 0);
    }
    const size_t su = size_t(s.unit);
    const cr3::Cr3BattleUnit& bu = cr3Battle.units[su];
    if (a.command >= 0) { // (BtlDirection::openAnnounce: the command's name - an item's, used - for a second)
        const auto item = cr3TurnItems.find(a.source);
        std::string text = item != cr3TurnItems.end() ? cr3ItemText(item->second, false) : cr3NameOf("command_name.mbe", "CommandName", a.command);
        if (const size_t at = text.find("{source}"); at != std::string::npos) text.replace(at, 8, cr3FighterName(s));
        if (const size_t at = text.find("{assistant}"); at != std::string::npos)
            for (const cr3::Cr3Fighter& o : cr3Rules.fighters)
                if (o.player == s.player && &o != &s) {
                    text.replace(at, 11, cr3FighterName(o));
                    break;
                }
        if (!text.empty()) setCr3Announce(text, 1.0f);
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
    size_t mostHits = 0;
    for (const cr3::Cr3Hit& target : a.targets) mostHits = std::max(mostHits, target.hits.size());
    cr3ActionPlay.directionCommand = cr3BattleTables.directionCommand(a.command, s.attackCommand);
    auto timing = cr3BattleTables.actionTiming(bu.model, cr3ActionPlay.directionCommand, std::max(mostHits, size_t(1)), c.repeat, c.element);
    std::string motion = cr3MotionOf(timing.motions.empty() ? c.motion : timing.motions.front().type);
    if (motion.empty() || motion == "bb01") motion = "bs01"; // (a shot's: BtlShotDirection plays 0xf)
    setCr3Motion(su, motion, false);
    for (int pi : a.partners) setCr3Motion(size_t(cr3Rules.fighters[size_t(pi)].unit), motion, false); // (a cooperation's others: the same)
    if (a.cooperated()) SDL_Log("battle %.2f s: a cooperation of %zu", cr3BattleClock, a.partners.size() + 1);
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
    cr3ActionPlay.timing = std::move(timing);
    if (motion == "bo01" && a.targets.size() == 1 && a.partners.empty())
        startCr3Melee(su, size_t(cr3Rules.fighters[size_t(a.targets.front().target)].unit));
    if (startCr3LastAttack(a, su)) return;
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
    if (cr3PendingStart) {
        cr3PendingStart = false;
        return startCr3Action();
    }
    if (cr3ActionAt >= cr3Actions.size()) return endCr3Actions();
    stepCr3LastAttack();
    const cr3::Cr3Action& a = cr3Actions[cr3ActionAt];
    const size_t source = size_t(cr3Rules.fighters[size_t(a.source)].unit);
    const float t = cr3BattleClock - cr3ActionStart;
    const auto& timing = cr3ActionPlay.timing;
    while (cr3ActionPlay.nextMotion < timing.motions.size() && t >= timing.motions[cr3ActionPlay.nextMotion].time) {
        const auto& next = timing.motions[cr3ActionPlay.nextMotion++];
        const std::string motion = cr3MotionOf(next.type);
        setCr3Motion(source, motion, false, cr3ActionPlay.melee);
        for (int partner : a.partners) setCr3Motion(size_t(cr3Rules.fighters[size_t(partner)].unit), motion, false);
        if (cr3BattleUnits[source].motion == motion && cr3BattleUnits[source].body.animLoaded)
            cr3ActionPlay.end = std::max(cr3ActionPlay.end, t + cr3BattleUnits[source].body.anim.duration());
    }
    stepCr3CastEffects(source, t);
    if (!finishCr3SpellHandoff(a, source, t)) return;
    stepCr3ActionEffects(a, source, t);
    size_t mostHits = 0;
    for (const cr3::Cr3Hit& th : a.targets) mostHits = std::max(mostHits, th.hits.size());
    while (a.target >= 0 && cr3ActionPlay.hitsShown < int(mostHits) && size_t(cr3ActionPlay.hitsShown) < timing.hits.size() &&
           t >= timing.hits[size_t(cr3ActionPlay.hitsShown)]) {
        const size_t h = size_t(cr3ActionPlay.hitsShown++);
        for (const cr3::Cr3Hit& th : a.targets) { // (each target the hit lands on at once)
            if (h >= th.hits.size()) continue;
            const int d = th.hits[h];
            const int mpD = h < th.mpHits.size() ? th.mpHits[h] : 0;
            const uint8_t flags = h < th.flags.size() ? th.flags[h] : 0;
            if (h < th.gauge.size()) cr3GaugeShown = th.gauge[h];
            const size_t tu = size_t(cr3Rules.fighters[size_t(th.target)].unit);
            int& shown = cr3HpShown[size_t(th.target)];
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
            cr3::Cr3Command hc;
            cr3Rules.command(a.command, hc);
            if (cr3Rules.fighters[size_t(a.source)].player && !cr3Rules.fighters[size_t(th.target)].player && d >= 0 && !hc.ally() &&
                size_t(th.target) < cr3ComboHits.size()) {
                ++cr3ComboHits[size_t(th.target)];
                cr3ComboDamage[size_t(th.target)] += d;
                if (cr3ComboSince < 0) cr3ComboSince = cr3BattleClock;
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
            if (d > 0 || (d == 0 && mpD > 0) || (d == 0 && mpD == 0 && !hc.ally())) setCr3Motion(tu, "bd01", false);
            else if (shown > 0 && cr3BattleUnits[tu].knockedOut) restoreCr3Unit(tu);
        }
        audio.playSe(0x164, false);
        if (cr3ActionPlay.hitsShown == int(mostHits)) {
            announceCr3Effects(a);
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
    }
    stepCr3MeleeReturn(source, t);
    if (t >= cr3ActionPlay.end && !cr3ActionPlay.melee) {
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
            if (dc.direction == 0x11 || dc.direction == 0x1e) {
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

void Viewer::endCr3Actions() {
    if (cr3ForcedEnd == 3) { // (Battle.ForceEnd: over - no victory, no result)
        SDL_Log("battle %d: ended by its script", cr3Battle.coupling);
        cr3ForcedEnd = -1;
        return endCr3Battle();
    }
    const int outcome = cr3ForcedEnd == 0 ? 1 : cr3Rules.outcome(); // (Battle.ForceVictory)
    cr3ForcedEnd = -1;
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
        runCr3BattleScript("Battle_Defeat_Base"); // (phaseDefeatEventStart: the result 1, Battle_Defeat_Base)
        if (playCr3ScriptCommands()) return beginCr3Turn(); // (its commands: not lost - the next turn)
        cr3BattleStage = Cr3BattleStage::Lost, cr3StageStart = cr3BattleClock;
        cr3BattleLost = true;
        SDL_Log("battle %d: lost", cr3Battle.coupling);
    } else {
        // (the turn's end: the status effects' HP changes - poison, regeneration - and their
        // turns)
        runCr3BattleScript("Battle_Turn_End_Base"); // (its turn's end: battle scripts')
        playCr3ScriptCommands();
        // (VS: a type-1 lifeline item's command at the turn's end - 79, Format: the party's
        // passing effects cured)
        if (vs.screen == 5)
            for (const VsBoost& b : vsBoostTable())
                if (b.type == 1 && std::find(vs.boosts.begin(), vs.boosts.end(), b.id) != vs.boosts.end())
                    for (cr3::Cr3Fighter& f : cr3Rules.fighters)
                        if (f.player)
                            f.effects.erase(std::remove_if(f.effects.begin(), f.effects.end(), [](const std::pair<int, int>& e) { return e.second >= 0; }),
                                            f.effects.end());
        for (const auto& [fi, change] : cr3Rules.endTurn()) {
            const size_t tu = size_t(cr3Rules.fighters[size_t(fi)].unit);
            cr3HpShown[size_t(fi)] = cr3Rules.fighters[size_t(fi)].hp;
            cr3Popups.push_back({std::abs(change), unitPoint(tu, "DummyDP01", 1.2f), cr3BattleClock, 0, change > 0 ? 3 : 0});
        }
        beginCr3Turn();
    }
}

bool Viewer::playCr3ScriptCommands() {
    if (cr3ScriptCommands.empty()) return false;
    for (const auto& [id, target] : cr3ScriptCommands) {
        cr3::Cr3Command c;
        if (!cr3Rules.command(id, c)) {
            SDL_Log("battle %d: script command %d unknown", cr3Battle.coupling, id);
            continue;
        }
        const bool players = c.target == 0 || c.target == 1 || c.target == 2 || c.target == 11; // (its own side's: the party's)
        for (size_t k = 0; k < cr3Rules.fighters.size(); ++k) {
            cr3::Cr3Fighter& f = cr3Rules.fighters[k];
            if (target >= 0 ? int(k) != target : (f.player != players || (!c.all && !f.dead() && !c.removes.empty())))
                continue;
            const bool fallen = f.dead();
            for (int e : c.removes) cr3Rules.removeEffect(int(k), e);
            if (fallen && std::find(c.removes.begin(), c.removes.end(), 1) == c.removes.end()) continue; // (KO not taken away)
            if (c.ally() || fallen) {
                const cr3::Vec3 dp = unitPoint(size_t(f.unit), "DummyDP01", 1.2f);
                if (c.damage == 16 || c.damage == 17) {
                    const int beforeMp = f.mp;
                    const int m = c.damage == 16 ? std::max(c.power, 0) : std::max(f.maxMp * c.power / 100, 0);
                    f.mp = std::min(f.mp + m, f.maxMp);
                    cr3Popups.push_back({f.mp - beforeMp, dp, cr3BattleClock, 0, 4});
                } else {
                    const int before = f.hp;
                    const int h = c.damage == 6 ? std::max(c.power, 1) : std::max(f.maxHp * c.power / 100, 1);
                    f.hp = std::min(std::max(f.hp, 0) + h, f.maxHp);
                    cr3Popups.push_back({f.hp - before, dp, cr3BattleClock, 0, 3});
                    if (c.damage == 18) {
                        const int beforeMp = f.mp;
                        f.mp = std::min(f.mp + std::max(f.maxMp * c.power / 100, 0), f.maxMp);
                        cr3Popups.push_back({f.mp - beforeMp, {dp[0], dp[1] - 0.35f, dp[2]}, cr3BattleClock, 0, 4});
                    }
                }
            }
            cr3HpShown[k] = f.hp;
            if (k < cr3MpShown.size()) cr3MpShown[k] = f.mp;
            if (fallen && f.hp > 0) restoreCr3Unit(size_t(f.unit));
        }
        SDL_Log("battle %d: script command %d played (target %d)", cr3Battle.coupling, id, target);
    }
    cr3ScriptCommands.clear();
    return true;
}

void Viewer::giveCr3GeneExp() {
    cr3ResultCards.clear();
    bool anyUp = false;
    for (int slot = 0; slot < 5; ++slot) {
        const int ch = game.cr3Party[slot];
        if (ch <= 0) continue;
        cr3::MbeFile::Row charRow;
        if (cr3TablesReady() && cr3BattleTables.character.find("Char", ch, "iiii", charRow) && (charRow.i(3) & 1) != 0) continue; // (Guest: no result card / gene EXP)
        bool standing = false;
        for (const cr3::Cr3Fighter& f : cr3Rules.fighters)
            if (f.player && f.id == ch) standing = !f.dead();
        const int gain = slot < 3 && standing ? cr3ResultExp : cr3ResultExp / 2;
        Cr3ResultCard card;
        card.ch = ch;
        const int gi = game.wornGene(ch);
        const cr3::Cr3OfflineDb::GeneInfo* info = gi >= 0 && game.cr3Db ? game.cr3Db->geneInfo(game.cr3Genes[size_t(gi)].master) : nullptr;
        if (info) {
            cr3::Cr3Fighter oldStats;
            const bool hadStats = cr3CharaStats(ch, oldStats);
            cr3::Cr3Gene& g = game.cr3Genes[size_t(gi)];
            const int before = game.cr3Db->geneLevel(g);
            game.cr3Db->addGeneExp(g, gain); // (held at its top; its new commands and skills learnt - SaveCommandSkill)
            card.level = game.cr3Db->geneLevel(g);
            const int at = game.cr3Db->geneExp(g.master, card.level), next = game.cr3Db->geneExp(g.master, card.level + 1);
            card.progress = card.level >= info->maxLevel || next <= at ? 0.0f : float(g.exp - at) / float(next - at);
            card.up = card.level > before;
            if (card.up && hadStats && game.cr3Mp.count(ch)) { // (CrxCharacter::RecoverBonusMP @ 0x008f61b8)
                cr3::Cr3Fighter newStats;
                if (cr3CharaStats(ch, newStats) && newStats.maxMp > oldStats.maxMp) {
                    const int nextMp = game.cr3Mp[ch] + (newStats.maxMp - oldStats.maxMp);
                    if (nextMp >= newStats.maxMp) game.cr3Mp.erase(ch);
                    else game.cr3Mp[ch] = nextMp;
                }
            }
            anyUp |= card.up;
            SDL_Log("battle %d: character %d's gene %d +%d EXP - level %d%s", cr3Battle.coupling, ch, g.master, gain, card.level, card.up ? " (up)" : "");
        }
        cr3ResultCards.push_back(card);
    }
    if (anyUp) audio.playSe("com_018", false);
}

void Viewer::acquireCr3Remuneration() {
    int levels = 0, monsters = 0, geneTop = 1;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters)
        if (f.player) geneTop = std::max(geneTop, f.geneLevel);
        else levels += f.level, ++monsters;
    const float gap = std::clamp(1.0f + float((monsters ? levels / monsters : 0) - geneTop) * 0.02f, 0.5f, 2.0f);
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters) {
        if (f.player || !f.dead()) continue;
        cr3::MbeFile::Row r;
        if (!cr3BattleTables.monster.find("Monster", f.id, "iipiiiiiiiiiiiiii", r)) continue;
        const int exp = r.i(14) + (f.level - 1) * int(int16_t(uint32_t(r.i(16)) & 0xffff));
        if (exp >= 1) cr3WaveExp += std::max(int(gap * float(exp)), 1);
        cr3WaveMoney += r.i(15) + (f.level - 1) * int(int16_t(uint32_t(r.i(16)) >> 16)); // (+ a steal skill's - kind 0x74 -: not in)
    }
    for (const auto& [fi, item] : cr3Drops)
        if (item > 0) cr3WaveDrops.push_back(item);
    cr3Drops.clear();
}

void Viewer::enterCr3Result() {
    acquireCr3Remuneration();
    cr3ResultExp = cr3WaveExp, cr3ResultMoney = cr3WaveMoney;
    // (BtlInterfaceSystem::EntryResult: x (1 + every unit's skills' kind 0x6c constants / 100) - the
    // money's 0x6d)
    float expBy = 1, moneyBy = 1;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters) {
        for (int c : cr3Rules.skillConstants(f, 0x6c)) expBy += float(c) / 100;
        for (int c : cr3Rules.skillConstants(f, 0x6d)) moneyBy += float(c) / 100;
    }
    // (BtlResult::GetExperience: (+4 - the monsters', what scripts added) x its factor - +0x28, the
    // scripts' SetExperienceFactor; the potions' and items' not in -; the money's likewise)
    cr3ResultExp = (cr3ResultExp + cr3ResultAdd[0]) * cr3ResultFactor[0], cr3ResultMoney = (cr3ResultMoney + cr3ResultAdd[1]) * cr3ResultFactor[1];
    cr3ResultExp = int(expBy * float(cr3ResultExp)), cr3ResultMoney = int(moneyBy * float(cr3ResultMoney));
    const int64_t goldMax = game.cr3Db ? game.cr3Db->constant("GOLD_MAX", 9999999) : 9999999;
    game.cr3Money = int(std::clamp<int64_t>(int64_t(game.cr3Money) + cr3ResultMoney, 0, goldMax));
    giveCr3GeneExp();
    for (int item : cr3WaveDrops) // (VAcquireDrop: the drops into the bag)
        game.cr3Items[item] = std::clamp(game.cr3Items[item] + 1, 0, 99), SDL_Log("battle %d: item %d into the bag", cr3Battle.coupling, item);
    if (game.next.battleMusicAfter >= 0) audio.stopBgm(30); // (phaseResult: FadeBgm, a second at 30 updates/s)
    cr3BattleStage = Cr3BattleStage::Result, cr3StageStart = cr3BattleClock;
    SDL_Log("battle %d: result - %d EXP, %d money", cr3Battle.coupling, cr3ResultExp, cr3ResultMoney);
}

// the keys: up / down the focus, Return chooses, Backspace back from the targets
bool Viewer::cr3BattleKey(SDL_Keycode k) {
    if (cr3ContinueKey(k)) return true;
    if (cr3BattleEvents && cr3BattleEvents->running()) return false;
    const bool enter = k == SDLK_RETURN || k == SDLK_KP_ENTER;
    if (enter && (cr3BattleStage == Cr3BattleStage::Won || cr3BattleStage == Cr3BattleStage::Result)) return cr3BattleTap(), true;
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
    if (cr3Ctn.stage != 0) { // (the continue's buttons, its question's)
        const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
        const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
        for (auto it = cr3CtnTaps.rbegin(); it != cr3CtnTaps.rend(); ++it)
            if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1))
                return chooseCr3Continue(it->id), true;
        return true;
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
    if (cr3BattleStage == Cr3BattleStage::Result) {
        if (cr3BattleClock - cr3StageStart < 1.5f) {
            cr3StageStart = cr3BattleClock - 1.5f;
            SDL_Log("battle: result tally completed");
        } else endCr3Battle();
        return;
    }
    if (cr3BattleStage == Cr3BattleStage::Won && cr3VictoryIn) {
        cr3StageStart = cr3BattleClock - cr3VictoryDuration;
        SDL_Log("battle: victory camera skipped");
        return;
    }
    if (cr3BattleStage == Cr3BattleStage::In) cr3Cam.start = cr3BattleClock - cr3Cam.end, cr3BattleInSkipped = true; // (phaseBattleIn: a tap ends it)
    else if (cr3BattleStage == Cr3BattleStage::Menu) chooseCr3GetSet(cr3Focus);
    else if (cr3BattleStage == Cr3BattleStage::Escape) chooseCr3Escape(cr3Focus);
    else if (cr3BattleStage == Cr3BattleStage::List) chooseCr3ListRow(cr3Focus);
    else if (cr3BattleStage == Cr3BattleStage::Command) chooseCr3Command(cr3Focus);
    else if (cr3BattleStage == Cr3BattleStage::Target) chooseCr3Target(cr3Focus);
}

void Viewer::endCr3Battle() {
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
    const float t = cr3BattleClock - cr3StageStart;
    UiFig& pos = cr3UiPut("btrsltposition", "btrsltposition_in", t, 0, 0, view);
    float x, y;
    if (cr3UiCall(pos, "call_btRsltBgMenu", x, y, w, h)) cr3UiPut("btrsltbgmenu", "", 0, x, y, view);
    // (the background - the position's own meshes too - stands at z 0.03 .. 0.05, in front of the
    // menus' 0 .. 0.02: the parts are drawn in order, over it)
    dev->clearDepth();
    if (cr3UiCall(pos, "call_btRsltTitleTMenu", x, y, w, h)) cr3UiPut("btrslttitletmenu", "btrslttitletmenu_in", t, x, y, view);
    if (cr3UiCall(pos, "call_btRsltTitleBMenu", x, y, w, h)) cr3UiPut("btrslttitlebmenu", "btrslttitlebmenu_in", t, x, y, view);
    if (cr3UiCall(pos, "call_btRsltInfoMenu", x, y, w, h)) cr3UiPut("btrsltinfomenu", "btrsltinfomenu_in", t, x, y, view);
    if (cr3UiCall(pos, "call_btRsltPointMenu", x, y, w, h)) {
        // (two btRsltPointPanel_numNN - N its digits - the EXP's and the money's, a
        // btRsltPointNum_fr00 frame a digit)
        UiFig& pm = cr3UiPut("btrsltpointmenu", "btrsltpointmenu_in", t, x, y, view);
        for (int k = 0; k < 2; ++k) {
            float px, py;
            if (!cr3UiCall(pm, "call_btRsltPointPanel_num00_00" + std::to_string(k + 1), px, py)) continue;
            const int total = std::max(k ? cr3ResultMoney : cr3ResultExp, 0);
            const std::string digits = std::to_string(int(float(total) * std::clamp(t / 1.5f, 0.0f, 1.0f)));
            const int n = std::min(int(digits.size()), 5);
            char lay[32];
            std::snprintf(lay, sizeof lay, "btrsltpointpanel_num%02d", n);
            UiFig& panel = cr3UiPut(lay, "", 0, px, py, view);
            for (int d = 0; d < n; ++d) {
                float dx, dy;
                const std::string joint = n == 1 ? std::string("call_btRsltPointNum_fr00") : "call_btRsltPointNum_fr00_00" + std::to_string(d + 1);
                if (cr3UiCall(panel, joint, dx, dy)) cr3UiPut("btrsltpointnum_fr00", "", float(digits[size_t(d)] - '0') / 30, dx, dy, view);
            }
        }
    }
    if (cr3UiCall(pos, "call_btRsltCharaMenu", x, y, w, h)) {
        UiFig& cm = cr3UiPut("btrsltcharamenu", "btrsltcharamenu_in", t, x, y, view);
        int slot = 0;
        for (const Cr3ResultCard& rc : cr3ResultCards) {
            float px, py;
            if (!cr3UiCall(cm, "call_btRsltCharaPanel_00" + std::to_string(++slot), px, py)) continue;
            UiFig& card = cr3UiPut("btrsltcharapanel", "btrsltcharapanel_in", t, px, py, view);
            float cx, cy;
            dev->clearDepth(); // (over the card's shadow)
            // (SetCharaCardData: the icon's frame the character's - 1, the bar its share done x 100 / 30 s)
            if (cr3UiCall(card, "call_btRsltCharaIcon_fr00", cx, cy)) cr3UiPut("btrsltcharaicon_fr00", "", float(rc.ch - 1) / 30, cx, cy, view);
            if (cr3UiCall(card, "call_btRsltLv_per", cx, cy)) cr3UiPut("btrsltlv_per", "", rc.progress * 100 / 30, cx, cy, view);
            // (a level up: EffectGeneUp's btRsltCharaCard and the gene's card, at the panel's
            // call_btRsltCharaCard / call_btRsltUpEffe - joints the panel doesn't have: not drawn)
            const cr3::Cr3Fighter f = [&] { cr3::Cr3Fighter x; x.level = rc.level; return x; }();
            // the level: btRsltLvPanel_num00's three places, btRsltLvNum_fr00 frame a digit
            // (leading places blank)
            if (cr3UiCall(card, "call_btRsltLvPanel_num00", cx, cy)) {
                UiFig& lv = cr3UiPut("btrsltlvpanel_num00", "", 0, cx, cy, view);
                const std::string digits = std::to_string(std::clamp(f.level, 0, 999));
                for (size_t d = 0; d < digits.size(); ++d) {
                    float dx, dy;
                    if (cr3UiCall(lv, "call_btRsltLvNum_fr00_00" + std::to_string(3 - digits.size() + d + 1), dx, dy))
                        cr3UiPut("btrsltlvnum_fr00", "", float(digits[d] - '0') / 30, dx, dy, view);
                }
            }
        }
    }
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
        // BtlTotalDamageUI (btToMenu: cumulative damage & hit counter per struck enemy across
        // consecutive player actions in the current chain @ 0x009a4744 / 0x00a47584)
        if (cr3BattleStage == Cr3BattleStage::Action && cr3ComboSince >= 0 && cr3ActionPlay.spellCardAge < 0) {
            std::vector<size_t> struck;
            for (size_t f = 0; f < cr3Rules.fighters.size() && f < cr3ComboHits.size() && struck.size() < 3; ++f)
                if (!cr3Rules.fighters[f].player && cr3ComboHits[f] > 0) struck.push_back(f);
            if (!struck.empty()) {
                UiFig& tpos = cr3UiPut("bttoposition", "bttoposition_io", cr3BattleClock - cr3ComboSince, 0, 0, view);
                if (cr3UiCall(tpos, "call_btToMenu", x, y, w, h)) {
                    UiFig& tmenu = cr3UiPut("bttomenu", "", 0, x, y, view);
                    for (size_t i = 0; i < struck.size(); ++i) {
                        const size_t f = struck[i];
                        const cr3::Cr3Fighter& ft = cr3Rules.fighters[f];
                        const cr3::Cr3BattleUnit& bu = cr3Battle.units[size_t(ft.unit)];
                        float px, py, cx, cy;
                        if (!cr3UiCall(tmenu, "call_btToPanel_00" + std::to_string(i + 1), px, py)) continue;
                        UiFig& panel = cr3UiPut("bttopanel", "", 0, px, py, view);
                        if (cr3UiCall(panel, "call_btToChara_id00", cx, cy)) {
                            UiFig& icon = uiFig("bttochara_id00");
                            cr3UiTexture(icon, "bt_targicon_" + bu.name + (bu.variant.empty() ? std::string("a") : bu.variant), "bt_targicon_" + bu.name + "_a");
                            cr3UiPut("bttochara_id00", "", 0, cx, cy, view);
                        }
                        if (cr3UiCall(panel, "call_btToHpBg", cx, cy)) cr3UiPut("bttohpbg", "", 0, cx, cy, view);
                        const int hp0 = f < cr3ComboStartHp.size() ? cr3ComboStartHp[f] : ft.hp;
                        const int hp1 = f < cr3HpShown.size() ? cr3HpShown[f] : ft.hp;
                        const float pct0 = ft.maxHp > 0 ? std::clamp(float(hp0) * 100 / float(ft.maxHp), 0.0f, 100.0f) : 0;
                        const float pct1 = ft.maxHp > 0 ? std::clamp(float(hp1) * 100 / float(ft.maxHp), 0.0f, 100.0f) : 0;
                        if (cr3UiCall(panel, "call_btToHp02_per", cx, cy)) cr3UiPut("bttohp02_per", "", pct0 / 30, cx, cy, view);
                        if (cr3UiCall(panel, "call_btToHp01_per", cx, cy)) cr3UiPut("bttohp01_per", "", pct1 / 30, cx, cy, view);
                        if (cr3UiCall(panel, "call_btToNum_num00", cx, cy)) {
                            const std::string digits = std::to_string(std::clamp(cr3ComboDamage[f], 0, 999999));
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
                            const std::string digits = std::to_string(std::clamp(cr3ComboHits[f], 1, 999));
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
    }
    cr3Taps.clear();
    const bool waiting = !(cr3BattleEvents && cr3BattleEvents->running());
    const bool asking = cr3BattleStage == Cr3BattleStage::Escape && cr3EscapeAsk;
    if ((cr3BattleStage == Cr3BattleStage::Command || cr3BattleStage == Cr3BattleStage::Menu || asking) && waiting) {
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
                // (Escape where forbidden; Options: not in yet; a command its status effects
                // forbid - BtlStatus::OpenMenuInterface: comBtn::SelectableSet(false), its parts'
                // diffuse (0.549, 0.549, 0.655))
                const bool off = asking ? false : getSet ? (b == 1 && !cr3EscapeAllowed()) : !cr3Rules.menuEnabled(cr3Chooser, b);
                cr3UiTint[0] = cr3UiTint[1] = off ? 0.549f : 1.0f, cr3UiTint[2] = off ? 0.655f : 1.0f; // (cr3UiPut's tint)
                if (vitaButtons() && b == cr3Focus) {
                    cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
                    cr3UiTintMaterial = "mat_com_menuBtn_02";
                    cr3UiMaterialTint[0] = off ? 0.17f : 0.5f, cr3UiMaterialTint[1] = off ? 0.33f : 1.0f, cr3UiMaterialTint[2] = off ? 0.5f : 1.5f;
                }
                UiFig& panel = cr3UiPut("combtnpanel", "", 0, px, py, view);
                float cx, cy;
                if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(b) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnIcon_fr00", cx, cy)) cr3UiPut("combtnicon_fr00", "", float(id) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnText_fr00", cx, cy))
                    cr3UiPut(b == cr3Focus ? "combtntexttap_fr00" : "combtntext_fr00", "", float(id) / 30, cx, cy, view);
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
                if (cr3UiCall(panel, "typeM_C01", cx, cy)) {
                    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                    drawUiText(cr3FighterName(ft), 30, cx / 10, cy / 10, w, h, false, 0.3f); // (dark on the panel)
                }
                float ax, ay, bx, by;
                if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3Taps.push_back({ax, ay, bx, by, int(i)});
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
