#pragma once
// An event playing (Cr3Event, VistaMain; docs/SCRIPTS.md): its script (`script\<event>.nut`, the
// function named after it) in the game's VM, with the natives an event drives - the scene's models,
// moves, camera and lights (EvtScene, VistaEvent: event/scene.hpp), the message window (EvtWindow,
// the text `message\<name>.dat`), the screen's fades (Cr3Fade), the story's state (Cr3Flag,
// Cr3Work) - and where it goes when it ends (Cr3System.BattleIN).
//
// The game updates 20 times a second (TestApp::onUpdate: no sooner than 0.05 s after the last), in
// GameMain::Update's order: the scene (VistaMain: EvtScene::Update, the camera, the lights), the
// script (ScriptManager::Update: a WaitFrame(n) - WaitProcessor - counts n updates down, then the
// script resumes), the windows, the fade. tick() is one of those updates.

#include <algorithm>
#include <array>
#include <map>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/mvgl.hpp"
#include "assets/text.hpp"
#include "event/scene.hpp"
#include "script/gamestate.hpp"
#include "script/vm.hpp"

namespace cr3 {

// The screen's fade (Cr3Fade over InterfaceMain's fade windows): a colour at an opacity moving
// linearly to its target over the frames the script gave
struct EventFade {
    int colour = 0;      // 0 black (fadeB), 1 white (fadeW), 2 fadeF, 3 the encounter's wipe
    float alpha = 0, target = 0, step = 0;
    void start(int colour, int frames, float from, float to);
    void update();
    // the opacity `fraction` (0..1) of an update on from the last
    float at(float fraction) const {
        const float d = step * std::clamp(fraction, 0.0f, 1.0f);
        return alpha < target ? std::min(alpha + d, target) : std::max(alpha - d, target);
    }
};

class EventRuntime {
public:
    // one update: CR3's scripts count 30 a second (WaitProcessor::StartWait / ProcessWait: n frames
    // are n / 30 s of elapsed time, whatever the display's rate) - CR2's were 20
    static constexpr float kTick = 1.0f / 30.0f;

    EventRuntime(const MvglArchive& archive, GameState& state);

    // the event `function` ("D01_01_0101_evt"): its script loaded and run up to its first wait.
    // False: no script, or it failed (error() says)
    bool start(const std::string& function);
    // another script's function (`script\<script>.nut`: title's manual_disp) - the same
    bool start(const std::string& script, const std::string& function);
    // one update; false: the script failed
    bool tick();
    bool running() const;     // the script hasn't ended

    // The same natives for another script (a field's: its events use the scene, the windows, the
    // fades too), and one update for it - the scene, its wait counted down (and it resumed), the
    // windows, the fade. reset(): every state back to none (a new scene).
    void bindNatives(ScriptVM& vm);
    // more natives for its scripts (a battle's: BattleStatus), bound after the rest at start
    std::function<void(ScriptVM&)> extraNatives;
    bool update(ScriptVM* vm);
    void reset();
    int frame() const { return frame_; }

    EventScene& scene() { return scene_; }
    const EventScene& scene() const { return scene_; }
    bool sceneShown() const { return scene_.loaded() && !terminated_ && !drawSkip_; }
    const EventFade& fade() const { return fade_; }
    // the face textures EvtScene.SetFaceTexture set (slot -> image)
    const std::map<int, std::string>& faces() const { return faces_; }

    // the message windows (Cr3Event's: 0 cut-in, 1 vista, 2 / 3 announcements, 4 tutorial, 5 system,
    // 6 narration): which are open (EvtWindow.Open), which the message is on (SetMessage's first)
    enum Window { CutIn = 0, Vista = 1, AnnounceA = 2, AnnounceB = 3, Tutorial = 4, System = 5, Narration = 6, TutorialMenu = 8 };
    // CR3's tutorial menu (InterfaceWindow.CreateTutorialMenuNum(n): TutorialMenu - ttlMnlMenu - with
    // message/AnnounceTutorialTxt's announcement n, its pages turned as a window's; its picture
    // SetPictureTutorialMenu(id) - tuto_ss<(id - 1) / 8 + 1>, frame (id - 1) % 8 -;
    // EndCheckTutorialMenu true once read, EndTutorialMenu closes it)
    bool tutorialMenuOpen() const { return tutorialMenuOpen_; }
    // CR3's map name (EvtIntroduction.SetDungeonNum(n) / SetDungeonLevel(lv): MapNameMenu - the
    // dungeon's name, frame n, and "World Lv" (lv - 1) x 10 + 1 -> lv x 10; in over 0.967 s, 2 s
    // held, out to 1.333 s; GetVisible while it shows). -1: none
    static constexpr float kMapNameSeconds = 0.96666664f + 2.0f + (1.3333334f - 0.96666664f);
    int mapName() const { return mapName_; }
    int mapNameLevel() const { return mapNameLevel_; }
    float mapNameAge() const { return mapNameAge_; }
    int dungeonLevel = 0; // (Cr3System.SetDungeonLevel / GetDungeonLevel)
    int tutorialMenuPicture() const { return tutorialMenuPicture_; }
    bool windowOpen() const { return !openWindows_.empty(); }
    bool windowOpen(int w) const { return openWindows_.count(w) != 0; }
    // how long a window has been open, or (closing) since it closed - its in / out animations'
    // time (seconds, as of the last update); -1: neither
    float windowAge(int w) const;
    bool windowClosing(int w) const { return closing_.count(w) != 0; }
    int messageWindow() const { return messageWindow_; }
    int itemShown() const { return itemShown_; } // (window 7's item: EvtWindow::SetItemMessage)
    bool messageShown() const { return message_ > 0; }
    const ParsedMessage& message() const { return parsed_; }
    int page() const { return page_; }
    // the page's characters shown so far (its lines' code points in turn): ProvisionalMenuText types
    // a character a drawn frame (EvtWindow::ShowMessage: speed 1, unless skipping) - at the game's
    // 60 frames a second, two an update; typed() - all of it
    int revealed() const { return revealed_; }
    bool typed() const;
    std::string speakerName() const; // the speaker's name tag (name data), "" none
    int messagesSet() const { return messagesSet_; } // (SetMessages since the start: checks)
    // the scene waits for the player's slash (VistaEvent.ExecutionStart)
    bool awaitingSlash() const { return execution_; }
    bool finishingSlash() const { return executionEnd_; } // (ExecutionEndStart's: the finale)
    // a tap (a swipe): the slash awaited, else the page typed at once, else the message's next
    // page, or its end (CheckMessage then says it's done)
    void advance();
    // a tap: also the touch Cr3Touch.GetTouchState says once (1: a finger down - WaitTouch's)
    void touch() { touched_ = true, advance(); }
    // pages turn (and slashes happen) by themselves after this many seconds (0: only advance())
    float autoPage = 3.0f;
    // The yes / no buttons (EvtWindow.ShowYesNoButton: BattleYNChoice - choice_io, circleBtn_fr 11
    // and 12 -, CheckYesNoButton its answer, 1 yes, 2 no, 0 none yet; HideYesNoButton). Asked
    // only when `askYesNo` (a player to answer: the viewer); else the check answers nothing (the
    // headless runs' scripts go on as without the buttons). With autoPage, yes after that long.
    bool askYesNo = false;
    // The fast-forward button (FastForward: `speedUp` on the message windows 0 .. 6, shown with a
    // page while EvtWindow.SetSkipButtonEnable allows - each Open allows it again): held
    // (EvtWindow::IsSkip), a page shows whole and turns at once, without a tap
    bool fastForward = false;
    // Cr3Fade.SetEffectLine / DeleteEffectLine (InterfaceMain::SetEffectLine: BattleEffectTypeBMenu,
    // radiate_loop on its loop over the screen)
    bool effectLine = false;
    bool mapAnimPaused = false; // (VistaEvent.SetMapAnimPause: the viewer holds the vista map's animation)
    // VistaMain's second map (VistaEvent.LoadSecondMap(name): built unless "null" or "") and which
    // is drawn (ChangeMap flips it: the first while unflipped, the second while flipped - so a
    // LoadSecondMap("") then ChangeMap leaves none, the scene's own sky showing)
    std::string secondMap;
    bool mapChanged = false;
    // the vista's skip button (EvtSkip: VistaFastButtonMenu - speedUp again; FldUtilGetVistaSkipEnable
    // is always on): from ButtonCreate till ButtonDelete, unless ButtonSetVisible(false). Held
    // (CheckButtonTouch: TestApp::onUpdate's), the game runs five updates a frame, the scripts'
    // PlaySE stays quiet and the pages race as the windows' button makes them
    bool skipButtonShown() const { return skipButton_; }
    bool skipHeld() const { return fastForward && skipButton_; }
    bool fastForwardShown() const {
        return message_ > 0 && messageWindow_ >= 0 && messageWindow_ <= 6 && !skipOff_.count(messageWindow_);
    }
    bool yesNoShown() const { return yesNoShown_; }
    float yesNoAge() const { return yesNoAge_; }
    void answerYesNo(bool yes) {
        if (yesNoShown_) yesNo_ = yes ? 1 : 2;
    }
    // CR3's choice menus (InterfaceWindow.CreateSelectTwo/ThreeChoiceMenu: GetTapSelect...Menu -1
    // until one is tapped, then its index; Msg_Choice2/3 poll it) - answered by answerChoice (the
    // viewer's), or with askYesNo and autoPage after that long - the first, then on each asking
    // again the next (a quiz asks until it's answered right, as a player would try them)
    int choiceShown() const { return choiceKind_; } // 0 none, 2 or 3 options
    void answerChoice(int index) {
        if (choiceKind_ && index >= 0 && index < choiceKind_) choiceAnswer_ = index;
    }
    // CR3's text entry (Cr3System.ShowInputDialog: the OS's keyboard dialog; CheckInputDialog while
    // open; GetInputDialogText its text; Set<Name|TeamName|PetName|FoodName>FromInputDialogText keep
    // it): closed by answerInput (the viewer's), or with askYesNo and autoPage after that long with
    // the text it holds (ShowInputDialog starts it with the current name; SetInputDialogText), accepted
    bool inputShown() const { return inputOpen_; }
    void answerInput(const std::string& text, bool ok = true) {
        if (inputOpen_) inputOpen_ = false, inputText_ = text, inputOk_ = ok;
    }
    // ("name", "team", "pet", "food" -> text): GameMain::SM_Startup's utils::SetDefaultName /
    // SetDefaultTeamName - "プレイヤー", "ケイオスチーム" in every release and language -, the
    // pet's and food's empty till entered
    std::map<std::string, std::string> enteredNames{{"name", "\u30d7\u30ec\u30a4\u30e4\u30fc"},
                                                    {"team", "\u30b1\u30a4\u30aa\u30b9\u30c1\u30fc\u30e0"},
                                                    {"pet", ""},
                                                    {"food", ""}};
    // utils::FilterMessage (both message windows' SetStringData, the name too): {name}, {team},
    // {pet}, {food} the entered names ({goldkey}, a count the port doesn't keep, stays)
    std::string filterMessage(std::string text) const;
    int language = 1; // (Cr3UtilOptionLanguage: 1 English)

    // The cut-ins' portraits (EvtFace over InterfaceMain's CutInWindowMenus): five layouts -
    // Create(kind) 0 `cutInR_io` (InterfaceMain::SetCutInWindow: interface 24), 1 `cutInLR_io`
    // (23), 2 `cutInL_io` (22), 3 `cutInPty_io` (99), 4 `cutInSgl_io` (100) - each a face image
    // (SetTexture(file, id) loads it, ChangeTexture(kind, id) puts it on), its brightness
    // (SetColor: the one not speaking 0.5), its io animation forward on opening and back on
    // closing (Delete: EndCutInWindow's table - 0, 1, 2, every one, 3, 4)
    static constexpr int kCutIns = 5;
    static constexpr float kCutInSeconds = 0.2f; // (the io animators' range)
    struct Portrait {
        bool open = false, closing = false;
        float time = 0; // into the io animation
        int texture = -1;
        float bright = 1;
        float age = 0; // (since Create, uncapped: CR3's centre one - CutInCenterMenu - fades and flashes by it)
        bool shop = false; // (CutInMenu +0x40 - ChangeCutInPosition(true), a shop's clerk: 0.87 lower)
    };
    // CR3's centre cut-in closes in 4 updates (CutInCenterMenu::InterfaceCloseAnimeSet: 4, a quarter
    // of its alpha off each Pose)
    static constexpr int kCentreCutIn = 1;
    static constexpr float kCentreCloseSeconds = 4 * (1.0f / 30.0f);
    const Portrait& cutIn(int kind) const { return cutIns_[size_t(kind)]; }
    // The gene lab (InterfaceMain::CreateGeneSynthesisMain - GeneSynthesisMain 0x9f, its
    // HeaderMenu 0x42, GeneSynthesisFutter 0xa0, comBtnMenu 0x2c -: InterfaceWindow.
    // CreateGeneSynthesisMenu(clerk) from a clerk's script, c115_GeneSynthesis). The clerk's
    // portrait the left cut-in at the shop's place (SetClerkCutIn: ReadCutInWindowTexture(clerk, 1),
    // SetCutInMenu(2), ChangeCutInPosition(true)); the script loops while CheckEndGeneSynthesisMenu,
    // running its talk when CheckSelectTalkGeneSynthesisMenu says Talk was chosen (back with
    // EndTalkNextGeneSynthesisMenu); the menu's Back - or ScriptSpecialEndGeneSynthesisMenu - ends it
    struct GeneLab {
        bool open = false;
        bool talk = false;    // (Talk chosen, not yet asked for)
        bool talking = false; // (the clerk's talk running: the menu set aside)
        std::string clerk;    // (the portrait's image: cutIn115_L06)
        bool vs = false;      // (opened from the VS mode - CreateGeneSynthesisMainToVS: GeneSynthesisMain +0x2c8 false, Talk off)
    };
    // the lab opened from outside a script (VsModeMain 10: CreateGeneSynthesisMainToVS)
    void openGeneLab(const std::string& clerk, bool vs);
    const GeneLab& geneLab() const { return geneLab_; }
    void geneLabTalk() { if (geneLab_.open && !geneLab_.talking) geneLab_.talk = true; }
    void geneLabEnd();
    // the clerk's cut-in away while a menu holds the screen (GeneSynthesisMain 9: EndCutInMenu(2))
    // and back after it (CreateGeneSynthesisClerk)
    void geneLabClerk(bool shown);
    // The Space Port (SpacePortMain, interface 0x7b - InterfaceWindow.CreateSpacePortMenuMain from a
    // town's ReserveSpacePortMenu: from black (Cr3FadeIn 5 frames), the header "Spaceport" (0x13),
    // Sally's portrait - cutIn121_L01 - the left cut-in at the shop's place (SetSugerCutIn), its
    // buttons and footer). The script loops while CheckQuestReturn is 0 (InterfaceMain +0x9c - 1
    // back, 2 a quest set off on - its id the global work's 24 -, 3 the world select's), running the
    // clerk's talk while CheckSpacePortSelectTalk (+0x1c, until EndSpacePortTalk) and quest_cancel
    // when SelectFreeSpacePort (+0x31, Explore answered yes - once)
    struct SpacePort {
        bool open = false;
        bool talk = false;    // (Talk chosen: the menu set aside until EndSpacePortTalk)
        bool explore = false; // (Explore's question answered yes, not yet asked for)
        int questReturn = 0;
    };
    const SpacePort& spacePort() const { return spacePort_; }
    void spacePortTalk();
    void spacePortExplore() { if (spacePort_.open) spacePort_.explore = true; }
    // Sally away while a quest's detail is up (SpacePortMain 6: EndCutInMenu(2)) and back after it
    // (SetSugerCutIn)
    void spacePortClerk(bool shown);
    // the menu done (SpacePortMain 0x17 / 0x18: back 1, a quest 2): faded out, the clerk away
    void spacePortEnd(int questReturn);
    // The world select (WorldSelectMenu, interface 0x2b): InterfaceWindow.CreateWorldSelectMenu - a
    // town's ReserveWorldSelectMenu, the end of quest_check - or the Space Port's Explore
    // (SpacePortMain's 0x22: `fromPort`, its back the Space Port again - 0x23). Chosen: CheckQuest
    // Return 3, GetSelectDungeonNum the point + 1 (-1 none), GetSelectDungeonLevel the level's index
    struct WorldSelect {
        bool open = false;
        bool fromPort = false;
    };
    const WorldSelect& worldSelect() const { return worldSelect_; }
    void openWorldSelect(bool fromPort);
    // done: chosen (CheckQuestReturn 3), or back from the Space Port's (its menu again)
    void worldSelectEnd(bool chosen);
    // a fade the menus start (Cr3FadeOut(0, n) / Cr3FadeIn: black over n frames)
    void menuFade(bool out, int frames) { fade_.start(0, frames, out ? 0 : 1, out ? 1 : 0); }
    std::string cutInImage(int id) const; // a loaded face's image ("" none)

    // The tutorials' pictures (Cr3FieldTutorial in the field and the battles; InterfaceWindow's
    // SetTutorialTexture ... in the manual - InstructionsWindowMenu's tutorialPic): the images
    // loaded (LoadTexture / SetTutorialTexture, five at most), the one shown (SetTexture /
    // SetTutorialTextureNumber: its number), shown at all (Visible / VisibleTutorialTexture)
    struct TutorialPicture {
        std::vector<std::string> images;
        int shown = -1;
        bool visible = false;
    };
    const TutorialPicture& tutorialPicture() const { return tutorialPicture_; }
    int tutorialId = 0; // (InterfaceWindow.GetTutorialID: the manual's choice - its +0x24)

    // where the event went at its end: BattleIN's battle (-1 none)
    int battle() const { return state_.next.battle; }

    const std::string& error() const { return error_; }
    std::map<std::string, int> stubCalls() const;

private:
    bool loadText(const std::string& name, TextData& into);
    const MvglArchive& archive_;
    GameState& state_;
    std::unique_ptr<ScriptVM> vm_;
    EventScene scene_;
    EventFade fade_;
    SpacePort spacePort_;
    WorldSelect worldSelect_;
    std::map<int, std::string> faces_;
    TextData text_, names_;
    std::map<int, TextData> windowText_; // (LoadMessageData(window, name): that window's own)
    TextData itemNames_;                 // (message\ItemName: SetItemMessage's)
    bool yesNoShown_ = false;
    int yesNo_ = 0;
    float yesNoAge_ = 0;
    int itemShown_ = 0;                  // (the item SetItemMessage named: its icon)
    TutorialPicture tutorialPicture_;
    std::set<int> openWindows_;
    std::set<int> skipOff_; // (SetSkipButtonEnable false)
    std::set<int> pageSendOff_; // (SetWaitButtonEnable false)
    bool skipButton_ = false;
    bool touched_ = false;
    float touchWait_ = 0;
    std::map<int, float> opened_, closing_; // (window -> seconds since)
    std::array<Portrait, kCutIns> cutIns_;
    GeneLab geneLab_;
    static constexpr int kClerkFace = 1000; // (the clerk's image's id among the cut-ins' - ReadCutInWindowTexture's 1)
    // the field camera as an event on the field moves it (Cr3FieldMapCamera: SetPos / CalcParameter's
    // end; GetPos_X / _Y) - where no field runtime (field/fieldscript) is attached to answer
    float fieldCameraX_ = 0, fieldCameraY_ = 0;
    // CR3's gift synthesis effect (GiftSynthesizeEffect: its figure ue003gift01's animation; ends
    // with it): the frame it ends at, -1 none
    int giftEffectEnd_ = -1;
    int choiceKind_ = 0, choiceAnswer_ = -1, choicesAsked_ = 0;
    float choiceAge_ = 0;
    bool inputOpen_ = false, inputOk_ = true;
    float inputAge_ = 0;
    std::string inputText_;
    bool tutorialMenuOpen_ = false;
    int mapName_ = -1, mapNameLevel_ = 0;
    float mapNameAge_ = 0;
    int tutorialMenuPicture_ = 0;
    TextData tutorialTexts_;
    std::vector<std::pair<int, std::string>> cutInImages_; // (id, image: the first of an id counts)
    int messageWindow_ = Vista;
    int message_ = 0, page_ = 0, revealed_ = 0, messagesSet_ = 0;
    float pageTime_ = 0;
    ParsedMessage parsed_;
    bool execution_ = false, executionEnd_ = false;
    bool terminated_ = false, drawSkip_ = false;
    int wait_ = 0, frame_ = 0;
    std::string error_;
};

} // namespace cr3
