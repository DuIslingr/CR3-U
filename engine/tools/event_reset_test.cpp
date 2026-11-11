#include "event/runtime.hpp"
#ifdef TEST_CR3
#include "db/cr3db.hpp"
#include "field/fieldscript.hpp"
#endif
#include "script/vm.hpp"
#include <squirrel.h>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace game = TEST_NAMESPACE;

int main() {
    game::MvglArchive archive;
    game::GameState state;
    game::EventRuntime events(archive, state);
    game::ScriptVM vm;
    game::bindGameState(vm, state);
    events.bindNatives(vm);
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++problems; std::printf("scene reset FAILED: %s\n", what); }
    };
    auto script = [&](const char* source, const char* function) {
        if (SQ_FAILED(sq_compilebuffer(vm.vm(), source, SQInteger(std::strlen(source)), "scene-reset", SQTrue)) || !vm.runMain() || !vm.call(function)) {
            std::printf("scene reset script failed: %s\n", vm.error().c_str());
            ++problems;
            return false;
        }
        return true;
    };
    events.autoPage = 0;
    events.askYesNo = true;
    events.language = 3;
    events.tutorialId = 12;
    events.touch();
    const char* dirty = R"(
function dirty() {
    VistaEvent.SetMapAnimPause(true);
    Cr3Fade.SetEffectLine();
    EvtSkip.ButtonCreate();
    EvtWindow.Open(1);
    EvtWindow.SetMessageText(1, "Previous scene");
    EvtWindow.SetSkipButtonEnable(1, false);
    EvtWindow.SetWaitButtonEnable(1, false);
    EvtWindow.SetVistaPauseButtonEnable(true);
    VistaEvent.ExecutionEndStart();
}
)";
    if (!script(dirty, "dirty")) return 1;
    check(events.vistaPauseEnabled, "script enables the vista pause button");
    events.vistaPaused = true;
    check(events.mapAnimPaused && events.effectLine && events.skipButtonShown() && events.finishingSlash(), "old scene setup");
    events.fastForward = true;
    events.reset();
    check(!events.vistaPauseEnabled && !events.vistaPaused, "old vista pause permission and pause state are gone");
    check(!events.mapAnimPaused && !events.effectLine, "new scene resumes the map and removes the old screen effect");
    check(!events.skipButtonShown() && !events.fastForward, "old skip controls and held state are gone");
    check(!events.awaitingSlash() && !events.finishingSlash(), "execution finale does not survive the scene");
    check(!events.windowOpen() && !events.messageShown() && events.messagesSet() == 0, "old dialogue and its counters are gone");
    check(events.autoPage == 0 && events.askYesNo && events.language == 3 && events.tutorialId == 12, "host playback policy and language/manual selection survive");
    script("function noTouch(){if(Cr3Touch.GetTouchState()!=0)throw \"old tap reached the new scene\";}", "noTouch");
    if (!script("function nextMessage(){EvtWindow.SetMessageText(1,\"Fresh dialogue\");}", "nextMessage")) return 1;
    check(events.fastForwardShown() && events.messagesSet() == 1, "fresh dialogue has normal skip permission");
    events.advance(); // reveal
    events.advance(); // dismiss, without needing a new Open to repair stale permissions
    check(!events.messageShown(), "fresh dialogue advances with a tap");
#ifdef TEST_CR3
    // One message with one language and a fixed-width voi2 trailer entry.
    std::vector<uint8_t> voiced(22 + 32 + 8);
    voiced[0] = 16, voiced[4] = 20, voiced[8] = 1;
    voiced[20] = 'x';
    std::memcpy(voiced.data() + 22, "test_voice", 10);
    std::memcpy(voiced.data() + 54, "voi2", 4);
    voiced[58] = 1;
    game::TextData text;
    check(text.parse(voiced) && text.message(1, 0) == "x", "named voice trailer preserves dialogue text");
    check(text.voiceName(1) == "test_voice" && text.voice(1) == -1, "voi2 exposes the named voice");
    check(text.voiceName(0).empty() && text.voiceName(2).empty(), "voice lookup is one-based and bounded");
    voiced[58] = 2;
    check(text.parse(voiced) && text.voiceName(1).empty(), "truncated voice trailer is ignored");
    const char* menus = R"(
function menus() {
    VistaEvent.LoadSecondMap("old_map");
    VistaEvent.ChangeMap();
    InterfaceWindow.CreateSelectThreeChoiceMenu();
    InterfaceWindow.CreateFastForwardWindow();
    InterfaceWindow.SetVisibleFastForwardWindow(true);
    InterfaceWindow.CreateLevelUpMenu();
    InterfaceWindow.CreateFieldKeyBuyMenu();
    InterfaceWindow.CreateClearQuestDetailMenu(7);
    Cr3System.ShowInputDialog(1);
    EvtIntroduction.SetDungeonNum(3);
    EvtIntroduction.SetDungeonLevel(4);
}
)";
    if (!script(menus, "menus")) return 1;
    events.openGeneGet(1, 2, true, false);
    events.collectListOpen(1, 2, 3);
    events.openWorldSelect(false);
    events.openGeneLab("clerk", false);
    check(events.mapChanged && !events.secondMap.empty() && events.choiceShown() == 3 && events.skipButtonShown() && events.inputShown(), "old secondary map and menus setup");
    events.reset();
    check(!events.mapChanged && events.secondMap.empty(), "new vista uses its own map");
    check(!events.choiceShown() && !events.skipButtonShown() && !events.inputShown(), "old choice, fast-forward window and text input are closed");
    check(!events.geneGet().open && !events.collectList().open && !events.worldSelect().open && !events.geneLab().open, "old reward, collection and destination menus are closed");
    check(events.levelUpShown() < 0 && !events.keyBuyShown() && !events.clearQuestShown() && events.mapName() < 0, "old banners and quest overlays are gone");
    if (!script(R"(function tutorialPages(){EvtWindow.SetMessageText(4,"First\\fSecond");})", "tutorialPages")) return 1;
    check(events.tutorialPage(-1) && events.page() == 0 && events.messageShown(), "tutorial previous stops at first page");
    check(events.tutorialPage(1) && events.page() == 1 && events.messageShown(), "tutorial next browses without closing the message");
    check(events.tutorialPage(1) && events.page() == 1 && events.messageShown(), "tutorial next stops at last page");
    check(events.tutorialPage(-1) && events.page() == 0, "tutorial previous returns to first page");
    script("function ordinaryMessage(){EvtWindow.SetMessageText(1,\"Ordinary dialogue\");}", "ordinaryMessage");
    check(!events.tutorialPage(1) && events.page() == 0 && events.messageShown(), "tutorial shoulders cannot page ordinary dialogue");

    // Audited CR3 runtime parity checks (fades, scene moves, \c/\l, \w, tapWait, CutIn open, YesNo close, dungeons).
    events.reset();
    const game::ParsedMessage aligned = game::parseMessage("\\cCentred\\n\\lLeft again");
    check(aligned.pages.size() == 1 && aligned.pages[0].lines.size() == 2 &&
          aligned.pages[0].lines[0].centred && !aligned.pages[0].lines[1].centred,
          "\\c and \\l control alignment per line");
    {
        game::EventModel em;
        em.name = "j001";
        events.scene().models().push_back(em);
    }
    if (!script(R"(
function fadeAndMove() {
    VistaEvent.SetMove("j001", 300, 0);
    VistaEvent.CameraMove(5, 15);
    Cr3Fade.FadeOut(0, 0);
    WaitFrame(1);
    Cr3Fade.FadeIn(0, 10);
}
)", "fadeAndMove")) return 1;
    check(std::fabs(events.fade().at(0) - 1.0f) < 1e-4f, "0-frame FadeOut snaps immediately to opaque");
    check(events.scene().models().size() == 1 && !events.scene().models()[0].moving &&
          std::fabs(events.scene().models()[0].sceneTime - 10.0f) < 1e-4f,
          "SetMove(m, 300, 0) rests at start frame without teleporting back to 0");
    events.update(&vm); // resumes Wait(1) -> calls FadeIn(0, 10) AFTER fades ticked this update
    check(std::fabs(events.fade().at(1.0f) - 1.0f) < 1e-4f, "FadeIn holds opaque during first 30-Hz tick after script resumes");
    events.update(&vm); // hold 2 -> 1
    check(std::fabs(events.fade().at(1.0f) - 1.0f) < 1e-4f, "FadeIn holds opaque across 4 60-Hz updates (2 ticks)");
    events.update(&vm); // hold 1 -> 0
    events.update(&vm); // first step down by 0.1
    check(events.fade().at(0) < 0.95f && events.fade().at(0) > 0.85f, "FadeIn steps linearly after 2-tick hold");
    for (int k = 0; k < 20; ++k) events.update(&vm);
    check(std::fabs(events.scene().cameraTime() - 0.5f) < 1e-4f &&
          std::fabs(events.scene().cameraEnd() - 0.5f) < 1e-4f,
          "CameraMove(5, 15) stops at frame 15 (0.5 s)");

    events.reset();
    if (!script(R"(function cutInWait(){EvtWindow.Open(0);EvtWindow.SetMessageText(0,"A\\w006B");})", "cutInWait")) return 1;
    for (int k = 0; k < 4; ++k) events.update(&vm);
    check(events.revealed() == 0, "CutIn window waits 5 opening ticks before typing");
    events.update(&vm); // tick 5: opening finishes, types up to \w006 stop at index 1
    check(events.revealed() >= 1 && !events.awaitingTap(), "CutIn starts typing once open and pauses on \\w");
    events.touch();
    check(events.revealed() == 1 && events.messageShown(), "user touch does not skip active \\w wait");
    for (int k = 0; k < 7; ++k) events.update(&vm); // finish \w006 and reveal 'B'
    check(events.awaitingTap() && events.messageShown(), "page awaits tap once typing completes");
    events.touch(); // before 3 ticks of awaitingTap: ignored by 0.10 s tapWait guard
    check(events.messageShown(), "0.10 s tapWait guard blocks immediate accidental tap");
    for (int k = 0; k < 3; ++k) events.update(&vm);
    events.touch();
    check(!events.messageShown(), "tap dismisses page once 0.10 s tapWait has elapsed");

    events.reset();
    if (!script("function askYN(){EvtWindow.ShowYesNoButton();} function hideYN(){EvtWindow.HideYesNoButton();}", "askYN")) return 1;
    check(events.yesNoShown() && events.yesNoClosing() < 0, "Yes/No opens without closing timer");
    events.answerYesNo(true);
    if (!vm.call("hideYN")) return 1;
    check(!events.yesNoShown() && events.yesNoClosing() >= 0, "HideYesNoButton starts 0.10 s close animation");
    for (int k = 0; k < 3; ++k) events.update(&vm);
    check(events.yesNoClosing() < 0, "Yes/No close animation finishes after 3 ticks");

    check(game::cr3DungeonNum("t0108") == 0 && game::cr3DungeonNum("d0101") == 5 &&
          game::cr3DungeonNum("d1801") == 22 && game::cr3DungeonNum("e0001") == 23 &&
          std::strcmp(game::cr3DungeonPrefix(22), "d18") == 0 && std::strcmp(game::cr3DungeonPrefix(23), "e00") == 0,
          "CR3 dungeon table maps all 24 prefixes (t01..t05, d01..d18, e00)");

    // Round 2 audited EventRuntime natives: EvtScene.DestroySprite / ChangeColor, Cr3System.CrossFade, and StaffrollMenu.
    events.reset();
    {
        game::EventModel em;
        em.name = "j001";
        events.scene().models().push_back(em);
    }
    if (!script(R"(
function spriteAndColor() {
    EvtScene.CreateSprite(0);
    EvtScene.CreateSprite(1);
    EvtScene.DestroySprite(0);
    EvtScene.ChangeColor("j001", 0.25, 0.5, 0.75);
}
)", "spriteAndColor")) return 1;
    check(events.scene().sprites.size() == 1 && events.scene().sprites[0].id == 1,
          "EvtScene.DestroySprite removes targeted sprite id");
    check(std::fabs(events.scene().models()[0].tint[0] - 0.25f) < 1e-4f &&
          std::fabs(events.scene().models()[0].tint[1] - 0.5f) < 1e-4f &&
          std::fabs(events.scene().models()[0].tint[2] - 0.75f) < 1e-4f,
          "EvtScene.ChangeColor updates model RGB tint");

    // Round 14: the face - LoadFaceMotion / SetFaceMotion at once, SetBlendFaceMotion(chr, loop, frames)
    // cross-faded over frames / 30 s from the face before (Cr3EvtSceneSetBlendFaceMotion 0x844808)
    events.reset();
    {
        game::EventModel em;
        em.name = "j001", em.figure = "j001";
        events.scene().models().push_back(em);
    }
    if (!script(R"(
function faces() {
    EvtScene.LoadFaceMotion("j001", "f001");
    EvtScene.SetFaceMotion("j001", true);
    EvtScene.LoadFaceMotion("j001", "f007");
    EvtScene.SetBlendFaceMotion("j001", false, 6);
}
)", "faces")) return 1;
    {
        const game::EventModel& fm = events.scene().models()[0];
        check(fm.faceMotion == "j001_f007" && !fm.faceLoop && fm.faceFrom == "j001_f001" && fm.faceFromLoop &&
                  std::fabs(fm.faceBlend - 0.2f) < 1e-4f && fm.faceBlendTime == 0,
              "SetBlendFaceMotion fades from the face before over frames / 30 s");
    }
    events.scene().update(0.25f);
    check(events.scene().models()[0].faceFrom.empty(), "the face fade over: the face before let go");

    events.reset();
    if (!script("function startCF(){Cr3System.CrossFade(6);WaitFrame(1);VistaEvent.ChangeMap();}", "startCF")) return 1;
    check(events.crossFade().capture && events.crossFade().active && std::fabs(events.crossFade().alpha() - 1.0f) < 1e-4f,
          "CrossFade(6) arms capture on pre-transition frame");
    events.markCrossFadeCaptured();
    events.update(&vm); // tick 1 -> elapsed 1/30 of 6/30, resumes WaitFrame(1) -> ChangeMap()
    check(events.mapChanged && events.crossFade().active && !events.crossFade().capture &&
          events.crossFade().alpha() < 0.9f && events.crossFade().alpha() > 0.75f,
          "CrossFade blends captured frame across ChangeMap");
    for (int k = 0; k < 5; ++k) events.update(&vm);
    check(!events.crossFade().active && std::fabs(events.crossFade().alpha()) < 1e-4f,
          "CrossFade completes after requested 30-Hz frame count");

    events.reset();
    if (!script(R"(
function runStaffroll() {
    InterfaceWindow.CreateStaffrollMenu();
    InterfaceWindow.SetDisplaySectionStaffrollMenu(10, 12);
    InterfaceWindow.SetScrollFontDataStaffrollMenu(22, 28, 1, 0.2, -1.2, -0.89);
    InterfaceWindow.SetScrollSpeedStaffrollMenu(0.14, 0.28);
}
)", "runStaffroll")) return 1;
    check(events.staffroll().open && !events.staffroll().finished && events.staffroll().endSteps == 29,
          "StaffrollMenu opens with native endSteps = (end - start + 0x1b)");
    if (!script("function assertRolling(){if(InterfaceWindow.CheckToBeContinueStaffrollMenu())throw \"ended early\";}", "assertRolling")) return 1;
    for (int k = 0; k < 35; ++k) events.update(&vm);
    check(events.staffroll().finished, "StaffrollMenu finishes once scrolled steps reach endSteps");
    if (!script("function assertDone(){if(!InterfaceWindow.CheckToBeContinueStaffrollMenu())throw \"still rolling\";}", "assertDone")) return 1;

    // Round 2 audited Cr3OfflineDb: immediate VIP bonus on BuyVIP / BuyVIPTrial, VS tournament gold + GOLD_MAX clamp,
    // and VS season scores[0] -> scores[1] progression.
    {
        auto bInt = [](int v) { game::BsonValue b; b.kind = game::BsonValue::Kind::Int; b.i = v; return b; };
        auto bStr = [](const char* s) { game::BsonValue b; b.kind = game::BsonValue::Kind::String; b.s = s; return b; };
        auto bArr = [](std::initializer_list<game::BsonValue> list) {
            game::BsonValue b; b.kind = game::BsonValue::Kind::Array; b.items = list; return b;
        };
        auto bObj = [](std::initializer_list<std::pair<std::string, game::BsonValue>> fields) {
            game::BsonValue b; b.kind = game::BsonValue::Kind::Object;
            for (const auto& kv : fields) b.members.push_back(kv);
            return b;
        };
        game::Cr3OfflineDb db;
        db.setTable("constant", bArr({bObj({
            {"VIP_PRICE", bInt(30)}, {"VIP_DAYS", bInt(30)}, {"VIP_TRIAL_DAYS", bInt(7)}, {"VIP_DAYS_MAX", bInt(90)},
            {"GOLD_MAX", bInt(9999999)}, {"OBTAIN_GP_VS_END_TOURNAMENT", bInt(1)}, {"PRESENT_FROM_SERVICE", bInt(2)},
        })}));
        db.setTable("vstournaments_cli", bArr({bObj({
            {"_id", bInt(101)}, {"mode", bInt(0)}, {"count", bInt(1)}, {"prize_gold", bInt(2500)},
            {"season_item_id", bInt(1)}, {"season_prob", bInt(100)},
            {"sdate", bArr({bInt(101)})}, {"edate", bArr({bInt(1231)})},
        })}));
        db.setTable("vsseasons", bArr({bObj({
            {"_id", bInt(7)}, {"scores", bArr({bInt(2), bInt(10)})}, {"count", bInt(2)},
            {"sdate", bInt(101)}, {"edate", bInt(1231)},
        })}));

        game::GameState gs;
        gs.cr3Coins = 50;
        check(db.buyVIP(gs) == game::kDbOk && gs.cr3VipDays == 30 && gs.cr3Presents.size() == 2 &&
              gs.cr3Presents[0].item == 400005 && gs.cr3Presents[1].item == 106 && (gs.cr3Daily & 0x10),
              "buyVIP immediately grants Day 1 VIP login bonus presents");
        game::GameState gsTrial;
        check(db.buyVIPTrial(gsTrial) == game::kDbOk && gsTrial.cr3VipDays == 7 && gsTrial.cr3Presents.size() == 2,
              "buyVIPTrial immediately grants Day 1 VIP login bonus presents");

        const std::time_t midYear = 1751328000; // July 2025
        check(db.getVSTournaments(gs, midYear) == game::kDbOk, "load synthetic VS tournament");
        gs.cr3Money = 1000;
        check(db.beginVSTournament(gs, 0, {}, midYear) == game::kDbOk && gs.cr3Vs.season == 7 &&
              gs.cr3Vs.seasonCount == 2 && gs.cr3Vs.seasonLevel == 0,
              "beginVSTournament initializes seasonCount from vsseasons.scores[0] (not count)");
        db.endVSBattle(gs, 5000, 1, false);
        db.endVSTournament(gs, midYear);
        check(gs.cr3Money == 3500 && db.vsRewards().gold == 2500 && gs.cr3Vs.seasonPoints == 1 && gs.cr3Vs.seasonLevel == 0,
              "endVSTournament credits prize_gold to GameState::cr3Money and advances seasonPoints");
        // Win 3 more 1-battle tournaments (points: 1->2 => level 1, count stays 2; then 0->1->2 => level 2 == count => seasonCount switches to scores[1]=10!).
        for (int run = 0; run < 3; ++run) {
            db.beginVSTournament(gs, 0, {}, midYear);
            db.endVSBattle(gs, 5000, 1, false);
            db.endVSTournament(gs, midYear);
        }
        check(gs.cr3Vs.seasonLevel == 2 && gs.cr3Vs.seasonPoints == 0 && gs.cr3Vs.seasonCount == 10,
              "CheckVSSeasonScore steps seasonCount from scores[0]=2 to scores[1]=10 once seasonLevel reaches count");
        gs.cr3Money = 9999000;
        db.beginVSTournament(gs, 0, {}, midYear);
        db.endVSBattle(gs, 5000, 1, false);
        db.endVSTournament(gs, midYear);
        check(gs.cr3Money == 9999999, "gold additions clamp cleanly at GOLD_MAX (9999999)");

        // Round 3 audited Cr3OfflineDb: receivePresent Oz-Coin fix & COIN_MAX clamp, getDailyItem partial-stack top-up,
        // and mergeGene setting story flag 34.
        db.setTable("constant", bArr({bObj({
            {"LOGIN_BONUS_COIN_MASTER_ID", bInt(-2)}, {"LOGIN_BONUS_COIN", bInt(10)}, {"COIN_MAX", bInt(1000)}, {"GOLD_MAX", bInt(9999999)},
        })}));
        db.setTable("dailyitems", bArr({bObj({
            {"_id", bInt(1)}, {"masterId", bInt(105)}, {"count", bInt(10)}, {"flag", bInt(4200)},
        })}));
        game::GameState gsPres;
        gsPres.cr3Coins = 990;
        gsPres.cr3Presents.push_back({-2, 1, 0, false, {}});
        gsPres.cr3Presents.push_back({-2, 1, 0, false, {}});
        check(db.receivePresent(gsPres, 0) == game::kDbOk && gsPres.cr3Coins == 1000 && gsPres.cr3Items.count(-2) == 0 &&
              db.receivePresent(gsPres, 0) == game::kDbFull && gsPres.cr3Presents.size() == 1,
              "receivePresent grants Oz Coins without adding bogus item -2 and blocks past COIN_MAX");
        gsPres.cr3Items[105] = 95;
        check(db.getDailyItem(gsPres, 1) == game::kDbOk && gsPres.cr3Items[105] == 99 && gsPres.flag(4200),
              "getDailyItem tops up partial stack (95 -> 99) and sets daily flag");
        game::GameState gsFullDaily;
        gsFullDaily.cr3Items[105] = 99;
        check(db.getDailyItem(gsFullDaily, 1) == game::kDbFull && gsFullDaily.cr3Items[105] == 99 && !gsFullDaily.flag(4200),
              "getDailyItem leaves daily flag unset when stack is already at 99");
    }

    // Round 3 audited EventRuntime & GameState: EvtWindow 8..10 vs TutorialMenu 20, default centered alignment,
    // SetContentTutorialMenuNum, OnFadeNarretionMenu, textured EventSprite & model attachments, SetDungeonLevel & CheckNewGene.
    events.reset();
    {
        game::EventModel em;
        em.name = "j001";
        events.scene().models().push_back(em);
    }
    if (!script(R"(
function round3Scene() {
    EvtWindow.Open(8);
    EvtWindow.SetMessageText(8, "Top banner\\n\\lLeft line");
    InterfaceWindow.OnFadeNarretionMenu();
    EvtWindow.Open(6);
    InterfaceWindow.SetPictureTutorialMenu(14);
    Cr3System.SetDungeonLevel(6);
    Cr3System.BattleInColor(0.2, 0.4, 0.8);
    Cr3System.SetViewportEnable(true);
    Cr3System.SetViewportPosition(0.5, 0.25);
    Cr3System.SetViewportSize(0.4, 0.4);
    EvtScene.CreateSpriteWithTexture(3, "cutin021_c01");
    EvtScene.SetSpriteScale(3, 1.5, 1.5);
    EvtScene.SetSpriteColorAnimation(3, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 10);
    EvtScene.SetSpriteAlphaAnimation(3, 0.0, 1.0, 10);
    EvtScene.ChangeVariation("j001", "b");
    EvtScene.ChangeArmor("j001", 84);
    EvtScene.SetWeaponVisible("j001", false);
    EvtScene.CreateAttachment("j001", 2, "w101", "DummyWP");
}
)", "round3Scene")) return 1;
    check(events.windowOpen(8) && events.messageWindow() == game::EventRuntime::AnnounceTop &&
          game::EventRuntime::AnnounceTop != game::EventRuntime::TutorialMenu,
          "EvtWindow.Open(8) (AnnounceTop) is distinct from TutorialMenu (20)");
    check(events.message().pages.size() == 1 && events.message().pages[0].lines.size() == 2 &&
          events.message().pages[0].lines[0].centred && !events.message().pages[0].lines[1].centred,
          "Announcement window 8 defaults lines to centered while honoring explicit \\l");
    if (!script("function switchTut(){InterfaceWindow.SetContentTutorialMenuNum(9);}", "switchTut")) return 1;
    check(events.windowOpen(8) && events.messageWindow() == game::EventRuntime::TutorialMenu,
          "SetContentTutorialMenuNum targets TutorialMenu (20) without closing AnnounceTop (8)");
    check(events.narrationAboveFade(), "OnFadeNarretionMenu elevates Narration Z above fade");
    if (!script("function closeNarr(){EvtWindow.Close(6);}", "closeNarr")) return 1;
    check(!events.narrationAboveFade(), "closing Narration window resets narrationAboveFade");
    check(events.tutorialMenuPicture() == 14, "SetPictureTutorialMenu updates tutorial picture id");
    check(state.dungeonLevel == 6 && state.battleInColorSet && state.viewportEnable,
          "SetDungeonLevel, BattleInColor, and SetViewportEnable update shared GameState");
    check(events.scene().sprites.size() == 1 && events.scene().sprites[0].texture == "cutin021_c01" &&
          std::fabs(events.scene().sprites[0].scale[0] - 1.5f) < 1e-4f &&
          std::fabs(events.scene().sprites[0].colourSeconds - 10.0f / 30.0f) < 1e-4f &&
          std::fabs(events.scene().sprites[0].alphaSeconds - 10.0f / 30.0f) < 1e-4f,
          "CreateSpriteWithTexture / SetSpriteScale / SetSpriteColorAnimation / SetSpriteAlphaAnimation configure textured sprite");
    for (int k = 0; k < 5; ++k) events.update(&vm);
    check(std::fabs(events.scene().sprites[0].alpha() - 0.5f) < 1e-4f && std::fabs(events.scene().sprites[0].colourAt(0) - 0.5f) < 1e-4f,
          "SetSpriteColorAnimation and SetSpriteAlphaAnimation interpolate RGBA at 30-Hz");
    check(events.scene().models()[0].variation == "b" && events.scene().models()[0].temporaryArmor == 84 &&
          !events.scene().models()[0].weaponVisible && events.scene().models()[0].attachments.count(2) == 1,
          "EvtScene ChangeVariation / ChangeArmor / SetWeaponVisible / CreateAttachment update EventModel");
    if (!script("function delAtt(){EvtScene.DeleteAttachment(\"j001\", 2);}", "delAtt")) return 1;
    check(events.scene().models()[0].attachments.empty(), "DeleteAttachment removes attachment slot");

    // CheckNewGene stays true right after GeneGet until Invoke(1) syncs cr3GeneBook, and dungeonLevel/cr3AceLevels roundtrip save/load.
    {
        game::GameState gsSave;
        game::Cr3OfflineDb db2;
        gsSave.cr3Db = &db2;
        game::EventRuntime ev2(archive, gsSave);
        game::ScriptVM vm2;
        game::bindGameState(vm2, gsSave);
        ev2.bindNatives(vm2);
        auto run2 = [&](const char* src, const char* fn) {
            return SQ_SUCCEEDED(sq_compilebuffer(vm2.vm(), src, SQInteger(std::strlen(src)), "gs2", SQTrue)) && vm2.runMain() && vm2.call(fn);
        };
        check(run2("function g(){Cr3System.GeneGet(301, 1); if(!Cr3System.CheckNewGene(301)) throw \"marked seen too early\"; Cr3System.Invoke(1); if(Cr3System.CheckNewGene(301)) throw \"not synced on Invoke(1)\";}", "g"),
              "CheckNewGene stays true after GeneGet until Invoke(1) syncs cr3GeneBook");
        gsSave.dungeonLevel = 9;
        gsSave.cr3AceLevels[12] = 45;
        const std::string saved = gsSave.save({{"area", "d0201"}});
        game::GameState loaded;
        std::map<std::string, std::string> extra;
        check(loaded.load(saved, &extra) && loaded.dungeonLevel == 9 && loaded.cr3AceLevels[12] == 45,
              "dungeonLevel and cr3AceLevels roundtrip cleanly across GameState::save/load");
        // Round 8: PartyEquip's SetLastAttackLevel(chr, 2), the story's (5, 2) - CrxCharacterData +0x14 / +0x18 -,
        // saved; UnEquipWeapon's wait on CheckDisposeMenu ends at once (no full-bag disposal menu)
        check(run2("function la(){Cr3System.SetLastAttackLevel(3, 2); Cr3System.SetLastAttackLevel(5, 2); Cr3System.SetFinestArtsLevel(3, 1);"
                   " if(Cr3System.CheckDisposeMenu()) throw \"disposal menu\";}", "la") &&
                  gsSave.cr3LastAttackLevel[3] == 2 && gsSave.cr3LastAttackLevel[5] == 2 && gsSave.cr3FinestArtsLevel[3] == 1,
              "SetLastAttackLevel / SetFinestArtsLevel set the character's tiers; CheckDisposeMenu is false");
        // Round 10: utils::SetBloom(on, offset, limit, scale) - GameSystem +0x10, +0x34, +0x30, +0x38 x 100
        check(!gsSave.bloom && gsSave.bloomLimit == 160 && gsSave.bloomOffset == 60 && gsSave.bloomScale == 90 &&
                  run2("function bl(){Cr3System.SetBloom(true, 0.75, 0.5, 0.25);}", "bl") && gsSave.bloom && gsSave.bloomOffset == 75 &&
                  gsSave.bloomLimit == 50 && gsSave.bloomScale == 25 && run2("function bo(){Cr3System.SetBloom(false, 0.6, 1.6, 0.9);}", "bo") &&
                  !gsSave.bloom,
              "SetBloom's defaults (off, 60 / 160 / 90) and its order: offset, limit, scale x 100");
        game::GameState loaded2;
        check(loaded2.load(gsSave.save({{"area", "t0101"}}), &extra) && loaded2.cr3LastAttackLevel == gsSave.cr3LastAttackLevel &&
                  loaded2.cr3FinestArtsLevel == gsSave.cr3FinestArtsLevel,
              "Last Attack / Finest Arts tiers roundtrip across GameState::save/load");
    }
#endif
    // reset is idempotent and leaves existing native bindings usable.
    events.reset();
    script("function finalMessage(){EvtWindow.Open(1);EvtWindow.SetMessageText(1,\"Still bound\");}", "finalMessage");
    check(events.windowOpen(1) && events.messageShown() && events.messagesSet() == 1, "native bindings still work after repeated reset");
    std::printf("scene reset: %d problems\n", problems);
    return problems ? 1 : 0;
}
