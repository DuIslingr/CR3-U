#include "event/runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "assets/mbe.hpp"
#include "assets/skeleton.hpp"
#include "audio/sounddata.hpp"

namespace cr3 {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// the script's fade colours (Cr3FadeIn's table: 1..3 as they are, anything else black)
int fadeColour(int type) { return type >= 1 && type <= 3 ? type : 0; }
// the Space Port's clerk: Sally (SetSugerCutIn - ReadCutInWindowTexture("cutIn121_L01", 1), the
// shop's place); away while her talk runs (SpacePortMain 0: EndCutInMenu(2))
const char* const kSpacePortClerk = "cutIn121_L01";

} // namespace

void EventFade::start(int c, int frames, float from, float to) {
    colour = c;
    alpha = from, target = to;
    step = frames > 0 ? std::abs(to - from) / float(frames) : 1.0f;
}

void EventFade::update() {
    if (alpha < target) alpha = std::min(alpha + step, target);
    else if (alpha > target) alpha = std::max(alpha - step, target);
}

EventRuntime::EventRuntime(const MvglArchive& archive, GameState& state) : enteredNames(state.cr3Names), archive_(archive), state_(state) {}

bool EventRuntime::loadText(const std::string& name, TextData& into) {
    std::vector<uint8_t> d;
    return archive_.read("message\\" + lower(name) + ".dat", d) && into.parse(d);
}

void EventRuntime::bindNatives(ScriptVM& vm) {
    // WaitFrame(n): the script waits n updates (ScriptLib::WaitFrame; WaitProcessor)
    vm.setNative("", "WaitFrame", [this](NativeCall& c) {
        wait_ = std::max(1, c.integer(0, 1));
        c.suspend();
    });
    // the scene
    ScriptVM* owner = &vm;
    vm.setNative("EvtScene", "Init", [this, owner](NativeCall& c) {
        std::string err;
        if (!scene_.load(archive_, c.string(0), &err)) owner->appendError("EvtScene.Init: " + err + "\n");
        terminated_ = drawSkip_ = false;
    });
    vm.setNative("EvtScene", "Terminate", [this](NativeCall&) { terminated_ = true; });
    vm.setNative("EvtScene", "SetDrawSkip", [this](NativeCall& c) { drawSkip_ = c.boolean(0); });
    vm.setNative("EvtScene", "SetAnimator", [](NativeCall&) {}); // (loads an animation ahead)
    vm.setNative("EvtScene", "Visible", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->visible = c.boolean(1);
    });
    vm.setNative("EvtScene", "IsVisible", [this](NativeCall& c) {
        const EventModel* m = scene_.model(c.string(0));
        c.result(m && m->visible);
    });
    // ChangeAnimator(model, "e826", loop): the model plays <figure>e826 from its start
    vm.setNative("EvtScene", "ChangeAnimator", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) {
            m->animation = m->figure + c.string(1);
            m->loop = c.boolean(2, true);
            m->animTime = 0;
            m->blendSeconds = 0;
        }
    });
    // ChangeAnimatorBlend(model, "e007", loop, frames): the same, faded in from the motion before
    vm.setNative("EvtScene", "ChangeAnimatorBlend", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) {
            m->blendFrom = m->animation.empty() ? m->figure : m->animation;
            m->blendFromTime = m->animTime;
            m->blendSeconds = float(std::max(c.integer(3), 0)) * kTick;
            m->animation = m->figure + c.string(1);
            m->loop = c.boolean(2, true);
            m->animTime = 0;
        }
    });
    // CR3's motions: LoadMotion(model, name) stages <figure>_<name> (CrxModel::LoadAnimation);
    // SetMotion(model, loop) plays it from its start, SetBlendMotion(model, loop, frames) fades into
    // it from the motion before; the face's (slot 1) the same, kept apart
    vm.setNative("EvtScene", "LoadMotion", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->stagedMotion = m->figure + "_" + c.string(1);
    });
    vm.setNative("EvtScene", "SetMotion", [this](NativeCall& c) {
        EventModel* m = scene_.model(c.string(0));
        if (!m || m->stagedMotion.empty()) return;
        m->animation = m->stagedMotion, m->loop = c.boolean(1, true), m->animTime = 0, m->blendSeconds = 0;
        m->loopStart = m->loopEnd = 0; // (a new animator: its own loop)
    });
    vm.setNative("EvtScene", "SetBlendMotion", [this](NativeCall& c) {
        EventModel* m = scene_.model(c.string(0));
        if (!m || m->stagedMotion.empty()) return;
        m->blendFrom = m->animation.empty() ? m->figure : m->animation;
        m->blendFromTime = m->animTime;
        m->blendSeconds = float(std::max(c.integer(2), 0)) * kTick;
        m->animation = m->stagedMotion, m->loop = c.boolean(1, true), m->animTime = 0;
        m->loopStart = m->loopEnd = 0;
    });
    // (Cr3EvtSceneSetMotionTime: the motion at that frame; ..LoopPoint: CrxModel::SetAnimationLoopStart/End)
    vm.setNative("EvtScene", "SetMotionTime", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->animTime = c.number(1) * kTick, m->blendSeconds = 0;
    });
    vm.setNative("EvtScene", "SetMotionLoopPoint", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->loopStart = c.number(1) * kTick, m->loopEnd = c.number(2) * kTick;
    });
    vm.setNative("EvtScene", "ResetMotionLoopPoint", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->loopStart = m->loopEnd = 0;
    });
    vm.setNative("EvtScene", "LoadFaceMotion", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->stagedFace = m->figure + "_" + c.string(1);
    });
    auto faceMotion = [this](NativeCall& c) {
        EventModel* m = scene_.model(c.string(0));
        if (m && !m->stagedFace.empty()) m->faceMotion = m->stagedFace, m->faceLoop = c.boolean(1, true), m->faceTime = 0;
    };
    vm.setNative("EvtScene", "SetFaceMotion", faceMotion);
    vm.setNative("EvtScene", "SetBlendFaceMotion", faceMotion);
    vm.setNative("EvtScene", "SetModelLayer", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->layer = c.integer(1);
    });
    // CR3's full-screen sprites (EventSprite)
    vm.setNative("EvtScene", "CreateSprite", [this](NativeCall& c) {
        const int id = c.integer(0);
        if (!scene_.sprite(id)) scene_.sprites.push_back(EventSprite{id});
    });
    vm.setNative("EvtScene", "DeleteSprite", [this](NativeCall& c) {
        const int id = c.integer(0);
        scene_.sprites.erase(std::remove_if(scene_.sprites.begin(), scene_.sprites.end(), [id](const EventSprite& sp) { return sp.id == id; }), scene_.sprites.end());
    });
    vm.setNative("EvtScene", "SetSpriteLayer", [this](NativeCall& c) {
        if (EventSprite* sp = scene_.sprite(c.integer(0))) sp->layer = c.integer(1);
    });
    vm.setNative("EvtScene", "SetSpriteColor", [this](NativeCall& c) {
        if (EventSprite* sp = scene_.sprite(c.integer(0)))
            for (int k = 0; k < 3; ++k) sp->colour[k] = c.number(1 + k);
    });
    vm.setNative("EvtScene", "SetSpriteAlpha", [this](NativeCall& c) {
        if (EventSprite* sp = scene_.sprite(c.integer(0))) sp->alphaFrom = sp->alphaTo = c.number(1), sp->alphaSeconds = 0;
    });
    vm.setNative("EvtScene", "SetSpriteAlphaAnimation", [this](NativeCall& c) {
        if (EventSprite* sp = scene_.sprite(c.integer(0)))
            sp->alphaFrom = c.number(1), sp->alphaTo = c.number(2), sp->alphaSeconds = float(std::max(c.integer(3), 0)) * kTick, sp->alphaTime = 0;
    });
    // PlayEffect / PlayEffectLoop (Cr3EvtScenePlayEffect): shown, its own animation from the start,
    // once or looping
    auto effect = [this](NativeCall& c, bool loop) {
        if (EventModel* m = scene_.model(c.string(0))) m->visible = true, m->animTime = 0, m->loop = loop, m->blendSeconds = 0;
    };
    vm.setNative("EvtScene", "PlayEffect", [effect](NativeCall& c) { effect(c, false); });
    vm.setNative("EvtScene", "PlayEffectLoop", [effect](NativeCall& c) { effect(c, true); });
    // ChangeAlpha(model, from, to, frames) (Cr3EvtSceneSetAlpha); SetPosDirect(model, x, y, z) /
    // SetPosLocator(model, other) (Cr3EvtSceneSetEvtModelPosition*: an effect where another model is)
    vm.setNative("EvtScene", "ChangeAlpha", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0)))
            m->alphaFrom = c.number(1), m->alphaTo = c.number(2), m->alphaSeconds = float(std::max(c.integer(3), 0)) * kTick, m->alphaTime = 0;
    });
    vm.setNative("EvtScene", "SetPosDirect", [this](NativeCall& c) { scene_.pin(c.string(0), c.number(1), c.number(2), c.number(3)); });
    // (a vista's Cr3FieldMapCharacter.ChangeModel: no field player then - FldUtilGetFieldMapPlayerInstance
    // none - so nothing; the field's own binding, made after these, changes its leader)
    vm.setNative("Cr3FieldMapCharacter", "ChangeModel", [](NativeCall&) {});
    // (PauseMove(on): Fld2PuaseCharaMove - Fld2TaskTouch::SetIsEventPause, the touches' walking held
    // during the event; Fld2Main::SM_Event lets it go when the event ends. Here the field takes no
    // walking while an area script runs anyway - cr3FieldIdle)
    vm.setNative("Cr3FieldMapCharacter", "PauseMove", [](NativeCall&) {});
    vm.setNative("EvtScene", "Move", [this](NativeCall& c) { scene_.slide(c.string(0), c.integer(1), c.integer(2)); });
    vm.setNative("EvtScene", "ChangeModelColor", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->color = std::clamp(c.integer(1), -1, 3);
    });
    vm.setNative("EvtScene", "ShadowVisible", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->shadow = c.boolean(1);
    });
    vm.setNative("EvtScene", "IsShadowVisible", [this](NativeCall& c) {
        const EventModel* m = scene_.model(c.string(0));
        c.result(m && m->hasShadow() && m->shadow);
    });
    vm.setNative("EvtScene", "ChangeDirection", [this](NativeCall& c) { scene_.turnTo(c.string(0), c.string(1), c.integer(2)); });
    vm.setNative("EvtScene", "SetPosLocator", [this](NativeCall& c) { scene_.pinTo(c.string(0), c.string(1)); });
    vm.setNative("VistaEvent", "CameraQuake", [this](NativeCall& c) { scene_.quake(c.number(0), c.number(1), c.number(2), c.integer(3)); });
    vm.setNative("VistaEvent", "CameraQuakeStop", [this](NativeCall&) { scene_.quakeStop(); });
    // the light colours: DirectionalLightColorAnimStart(from intensity, to intensity, from colour,
    // to colour, frames) / DirectStart(intensity, colour) / DirectStop; the hemisphere's (sky from,
    // sky to, ground from, ground to, frames) / DirectStart(sky, ground) / DirectStop
    auto vec = [](NativeCall& c, int i, float* out, int n) {
        static const char* const kNames[4] = {"x", "y", "z", "w"};
        for (int k = 0; k < n; ++k) out[k] = c.member(i, kNames[k]);
    };
    vm.setNative("VistaEvent", "DirectionalLightColorAnimStart", [this, vec](NativeCall& c) {
        EventScene::LightColour& l = scene_.directional();
        vec(c, 2, l.from, 3), vec(c, 3, l.to, 3);
        l.from[3] = c.number(0), l.to[3] = c.number(1);
        l.anim = true, l.frame = 0, l.frames = std::max(c.integer(4), 1);
        std::copy(l.from, l.from + 4, l.now);
    });
    vm.setNative("VistaEvent", "DirectionalLightColorDirectStart", [this, vec](NativeCall& c) {
        EventScene::LightColour& l = scene_.directional();
        vec(c, 1, l.now, 3);
        l.now[3] = c.number(0), l.direct = true;
    });
    vm.setNative("VistaEvent", "DirectionalLightColorDirectStop", [this](NativeCall&) { scene_.directional().direct = false; });
    vm.setNative("VistaEvent", "HemisphereLightColorAnimStart", [this, vec](NativeCall& c) {
        EventScene::LightColour& l = scene_.hemisphere();
        vec(c, 0, l.from, 3), vec(c, 1, l.to, 3), vec(c, 2, l.groundFrom, 3), vec(c, 3, l.groundTo, 3);
        l.anim = true, l.frame = 0, l.frames = std::max(c.integer(4), 1);
        std::copy(l.from, l.from + 3, l.now), std::copy(l.groundFrom, l.groundFrom + 3, l.ground);
    });
    vm.setNative("VistaEvent", "HemisphereLightColorDirectStart", [this, vec](NativeCall& c) {
        EventScene::LightColour& l = scene_.hemisphere();
        vec(c, 0, l.now, 3), vec(c, 1, l.ground, 3);
        l.direct = true;
    });
    vm.setNative("VistaEvent", "HemisphereLightColorDirectStop", [this](NativeCall&) { scene_.hemisphere().direct = false; });
    // SetMapAnimPause(on): the vista map's own animation held (VistaMain +0x28)
    vm.setNative("VistaEvent", "SetMapAnimPause", [this](NativeCall& c) { mapAnimPaused = c.boolean(0); });
    vm.setNative("EvtScene", "SetFaceTexture", [this](NativeCall& c) { faces_[c.integer(0)] = c.string(1); });
    vm.setNative("EvtScene", "ChangeModelFace", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->face = c.integer(1, -1);
    });
    // the vista's timelines (in the scene's frames)
    vm.setNative("VistaEvent", "SetMove", [this](NativeCall& c) { scene_.setMove(c.string(0), c.number(1), c.number(2)); });
    vm.setNative("VistaEvent", "CameraMove", [this](NativeCall& c) { scene_.cameraMove(c.number(0), c.number(1)); });
    vm.setNative("VistaEvent", "LightMove", [this](NativeCall& c) { scene_.lightMove(c.number(0), c.number(1)); });
    // the player's slash (WaitPlayerSlash: ExecutionStart, then CheckExecution each frame until
    // the swipe; ExecutionEndStart the finishing one)
    vm.setNative("VistaEvent", "ExecutionStart", [this](NativeCall&) { execution_ = true, executionEnd_ = false, pageTime_ = 0; });
    vm.setNative("VistaEvent", "ExecutionEndStart", [this](NativeCall&) { execution_ = true, executionEnd_ = true, pageTime_ = 0; });
    vm.setNative("VistaEvent", "CheckExecution", [this](NativeCall& c) { c.result(!execution_); });
    // the message window
    auto textData = [this](NativeCall& c) { loadText(c.string(0), text_); };
    auto nameData = [this](NativeCall& c) { loadText(c.string(0), names_); };
    vm.setNative("EvtWindow", "LoadSharedTextData", textData);
    vm.setNative("EvtWindow", "UnloadSharedData", [this](NativeCall&) { text_ = TextData(); });
    // Cr3Touch.GetTouchState (Cr3UtilGetTouchState: the first finger's event bits): 1 once after a
    // tap - and, pages turning by themselves, once a call has gone on autoPage's seconds - else 0
    vm.setNative("Cr3Touch", "GetTouchState", [this](NativeCall& c) {
        if (!touched_ && autoPage > 0 && (touchWait_ += kTick) >= autoPage) touched_ = true;
        c.result(touched_ ? 1 : 0);
        if (touched_) touched_ = false, touchWait_ = 0;
    });
    vm.setNative("VistaEvent", "GetMapAnimPause", [this](NativeCall& c) { c.result(mapAnimPaused); });
    vm.setNative("VistaEvent", "LoadSecondMap", [this](NativeCall& c) {
        const std::string n = c.string(0);
        if (!n.empty() && n != "null") secondMap = n;
    });
    vm.setNative("VistaEvent", "UnloadSecondMap", [this](NativeCall&) { secondMap.clear(), mapChanged = false; });
    vm.setNative("VistaEvent", "ChangeMap", [this](NativeCall&) { mapChanged = !mapChanged; });
    // (LoadMessageData(name), or (window, name): the tutorials' AnnounceTutorialTxt for 3 / 4)
    vm.setNative("EvtWindow", "LoadMessageData", [this](NativeCall& c) {
        if (!c.string(0).empty() || c.count() < 2) loadText(c.string(0), text_);
        else loadText(c.string(1), windowText_[c.integer(0)]);
    });
    vm.setNative("EvtWindow", "UnloadData", [this](NativeCall& c) { windowText_.erase(c.integer(0, -1)); });
    // the tutorials' pictures (TutorialPicture)
    auto pictureAdd = [this](NativeCall& c) {
        if (tutorialPicture_.images.size() < 5) tutorialPicture_.images.push_back(c.string(0));
    };
    auto pictureShow = [this](NativeCall& c) { tutorialPicture_.shown = c.integer(0, -1); };
    // (LunchEvent: utils::SetupLunchEvent .. SetLunchBalloonFinish - LunchMain's)
    vm.setNative("LunchEvent", "Setup", [this](NativeCall&) {
        lunch_.setup = true, lunch_.balloons.clear();
        scene_.useOwnCamera();
        for (const EventModel& m : scene_.models()) {
            LunchBalloon b;
            b.model = m.name;
            lunch_.balloons.push_back(b);
        }
    });
    vm.setNative("LunchEvent", "Shutdown", [this](NativeCall&) { lunch_.shutdown = true; });
    auto balloon = [this](const std::string& model) -> LunchBalloon* {
        for (LunchBalloon& b : lunch_.balloons)
            if (b.model == model) return &b;
        return nullptr;
    };
    vm.setNative("LunchEvent", "SetBalloon", [balloon](NativeCall& c) {
        if (LunchBalloon* b = balloon(c.string(0))) {
            if (b->frame != 2) b->frame = c.boolean(2) ? 1 : 0;
            b->shown = c.boolean(1);
        }
    });
    vm.setNative("LunchEvent", "SetBalloonOffset", [balloon](NativeCall& c) {
        if (LunchBalloon* b = balloon(c.string(0)))
            for (int k = 0; k < 3; ++k) b->offset[k] = c.number(1 + k);
    });
    vm.setNative("LunchEvent", "SetBalloonFinish", [balloon](NativeCall& c) {
        if (LunchBalloon* b = balloon(c.string(0))) b->frame = 2;
    });
    vm.setNative("InterfaceWindow", "CreateTutorialMenuNum", [this](NativeCall& c) {
        if (tutorialTexts_.count() == 0) loadText("AnnounceTutorialTxt", tutorialTexts_);
        parsed_ = parseMessage(filterMessage(tutorialTexts_.message(c.integer(0), language)));
        messageWindow_ = TutorialMenu, message_ = parsed_.pages.empty() ? 0 : c.integer(0);
        page_ = 0, pageTime_ = 0, revealed_ = 0, stop_ = 0, stopTime_ = 0;
        tutorialMenuOpen_ = message_ > 0, tutorialMenuPicture_ = 0;
        if (message_ > 0) ++messagesSet_;
    });
    vm.setNative("EvtIntroduction", "SetDungeonNum", [this](NativeCall& c) { mapName_ = c.integer(0), mapNameAge_ = 0; });
    vm.setNative("EvtIntroduction", "SetDungeonLevel", [this](NativeCall& c) { mapNameLevel_ = c.integer(0); });
    vm.setNative("EvtIntroduction", "GetVisible", [this](NativeCall& c) { c.result(mapName_ >= 0); });
    vm.setNative("Cr3System", "SetDungeonLevel", [this](NativeCall& c) { dungeonLevel = c.integer(0); });
    vm.setNative("Cr3System", "GetDungeonLevel", [this](NativeCall& c) { c.result(dungeonLevel); });
    vm.setNative("InterfaceWindow", "SetPictureTutorialMenu", [this](NativeCall& c) { tutorialMenuPicture_ = c.integer(0); });
    vm.setNative("InterfaceWindow", "EndCheckTutorialMenu", [this](NativeCall& c) { c.result(!(message_ > 0 && messageWindow_ == TutorialMenu)); });
    vm.setNative("InterfaceWindow", "EndTutorialMenu", [this](NativeCall&) {
        tutorialMenuOpen_ = false;
        if (messageWindow_ == TutorialMenu) message_ = 0;
    });
    vm.setNative("InterfaceWindow", "CheckAliveTutorialMenu", [this](NativeCall& c) { c.result(tutorialMenuOpen_); });
    vm.setNative("Cr3FieldTutorial", "Create", [this](NativeCall&) { tutorialPicture_ = TutorialPicture(); });
    vm.setNative("Cr3FieldTutorial", "Delete", [this](NativeCall&) { tutorialPicture_ = TutorialPicture(); });
    vm.setNative("Cr3FieldTutorial", "LoadTexture", pictureAdd);
    vm.setNative("Cr3FieldTutorial", "SetTexture", pictureShow);
    vm.setNative("Cr3FieldTutorial", "Visible", [this](NativeCall& c) { tutorialPicture_.visible = c.boolean(0); });
    vm.setNative("InterfaceWindow", "GetTutorialID", [this](NativeCall& c) { c.result(tutorialId); });
    vm.setNative("InterfaceWindow", "SetTutorialTexture", pictureAdd);
    vm.setNative("InterfaceWindow", "SetTutorialTextureNumber", pictureShow);
    vm.setNative("InterfaceWindow", "VisibleTutorialTexture", [this](NativeCall&) { tutorialPicture_.visible = true; });
    vm.setNative("InterfaceWindow", "UnVisibleTutorialTexture", [this](NativeCall&) { tutorialPicture_.visible = false; });
    vm.setNative("EvtWindow", "LoadSharedNameData", nameData);
    vm.setNative("EvtWindow", "LoadNameData", nameData);
    // (a window argument of null: the vista's, Cr3Work 2's usual value)
    vm.setNative("EvtWindow", "Open", [this](NativeCall& c) {
        const int w = c.integer(0, Vista);
        if (openWindows_.insert(w).second) opened_[w] = 0, closing_.erase(w);
        skipOff_.erase(w), pageSendOff_.erase(w); // (EvtWindow::Open: the skip and wait buttons allowed again)
    });
    auto close = [this](int w) {
        if (openWindows_.erase(w)) opened_.erase(w), closing_[w] = 0;
        if (w == messageWindow_) message_ = 0;
    };
    vm.setNative("EvtWindow", "Close", [close](NativeCall& c) { close(c.integer(0, Vista)); });
    vm.setNative("EvtWindow", "UnloadAll", [this](NativeCall&) { openWindows_.clear(), opened_.clear(), closing_.clear(), message_ = 0; });
    // the cut-ins' portraits
    vm.setNative("EvtFace", "SetTexture", [this](NativeCall& c) { cutInImages_.push_back({c.integer(1), c.string(0)}); });
    vm.setNative("EvtFace", "DeleteTexture", [this](NativeCall&) { cutInImages_.clear(); });
    // the gene lab (GeneLab in runtime.hpp)
    auto clerk = [this](const std::string& image) {
        geneLab_.clerk = image;
        cutInImages_.insert(cutInImages_.begin(), {kClerkFace, image});
        Portrait p{true, false, 0, kClerkFace, 1};
        p.shop = true;
        cutIns_[2] = p;
    };
    vm.setNative("InterfaceWindow", "CreateGeneSynthesisMenu", [this](NativeCall& c) { openGeneLab(c.string(0), false); });
    vm.setNative("InterfaceWindow", "SetGeneSynthesisClerkName", [clerk](NativeCall& c) { clerk(c.string(0)); });
    vm.setNative("InterfaceWindow", "CheckEndGeneSynthesisMenu", [this](NativeCall& c) { c.result(geneLab_.open); });
    vm.setNative("InterfaceWindow", "CheckSelectTalkGeneSynthesisMenu", [this](NativeCall& c) {
        const bool talk = geneLab_.talk;
        if (talk) geneLab_.talk = false, geneLab_.talking = true;
        c.result(talk);
    });
    vm.setNative("InterfaceWindow", "EndTalkNextGeneSynthesisMenu", [this, clerk](NativeCall&) {
        geneLab_.talking = false;
        if (geneLab_.open) clerk(geneLab_.clerk); // (the clerk's portrait back - the talk's own cut-ins closed)
    });
    vm.setNative("InterfaceWindow", "ScriptSpecialEndGeneSynthesisMenu", [this](NativeCall&) { geneLabEnd(); });
    vm.setNative("InterfaceWindow", "RefreshGeneSynthesisMenuButton", [](NativeCall&) {});
    // the Space Port (SpacePort in runtime.hpp)
    vm.setNative("InterfaceWindow", "CreateSpacePortMenuMain", [this, clerk](NativeCall&) {
        spacePort_ = SpacePort();
        spacePort_.open = true;
        menuFade(false, 5);
        clerk(kSpacePortClerk);
    });
    vm.setNative("InterfaceWindow", "CheckQuestReturn", [this](NativeCall& c) { c.result(spacePort_.questReturn); });
    vm.setNative("InterfaceWindow", "CheckSpacePortSelectTalk", [this](NativeCall& c) { c.result(spacePort_.talk); });
    vm.setNative("InterfaceWindow", "EndSpacePortTalk", [this, clerk](NativeCall&) {
        if (!spacePort_.talk) return;
        spacePort_.talk = false;
        if (spacePort_.open) clerk(kSpacePortClerk); // (SpacePortMain 4: the header, Sally and the buttons again)
    });
    vm.setNative("InterfaceWindow", "CreateWorldSelectMenu", [this](NativeCall&) { openWorldSelect(false); });
    // the gene obtained window (GeneGet in runtime.hpp)
    vm.setNative("InterfaceWindow", "CreateGeneGetWindow", [this](NativeCall&) { openGeneGet(0, 1, false, false); });
    vm.setNative("InterfaceWindow", "SetGeneParameter", [this](NativeCall& c) { openGeneGet(c.integer(0), c.integer(1, 1), c.boolean(6), true); });
    vm.setNative("InterfaceWindow", "SetGeneParameter_attr", [this](NativeCall& c) { openGeneGet(c.integer(0), c.integer(1, 1), false, true); });
    vm.setNative("InterfaceWindow", "SetGeneParameter_Card", [this](NativeCall& c) {
        int level = 1; // (MbGeneInfo::GetLevel of its EXP)
        if (state_.cr3Db) {
            Cr3Gene g;
            g.master = c.integer(0), g.exp = c.integer(1);
            level = state_.cr3Db->geneLevel(g);
        }
        openGeneGet(c.integer(0), level, c.boolean(2), false);
    });
    // a town's shop (Shop in runtime.hpp)
    vm.setNative("InterfaceWindow", "CreateShopMenu", [this, clerk](NativeCall& c) {
        shop_ = Shop();
        shop_.open = askYesNo && !autoMenus, shop_.id = c.string(0), shop_.clerk = c.string(1);
        if (shop_.open) clerk(shop_.clerk);
    });
    vm.setNative("InterfaceWindow", "CheckEndShopMenu", [this](NativeCall& c) { c.result(shop_.open); });
    vm.setNative("InterfaceWindow", "CheckSelectTalkShopMenu", [this](NativeCall& c) {
        const bool talk = shop_.talk;
        if (talk) shop_.talk = false, shop_.talking = true;
        c.result(talk);
    });
    vm.setNative("InterfaceWindow", "EndTalkNextShopMenu", [this, clerk](NativeCall&) {
        shop_.talking = false;
        if (shop_.open) clerk(shop_.clerk); // (the clerk back - the talk's own cut-ins closed)
    });
    // the alteration shop (CreateReModelingMenu(clerk) - RemodelingMain, InterfaceMain::
    // CreateReModelingMain: the town shop's way - its clerk, its talk, done when it's left)
    vm.setNative("InterfaceWindow", "CreateReModelingMenu", [this, clerk](NativeCall& c) {
        shop_ = Shop();
        shop_.open = askYesNo && !autoMenus, shop_.clerk = c.string(0), shop_.alter = true;
        if (shop_.open) clerk(shop_.clerk);
    });
    vm.setNative("InterfaceWindow", "CheckEndReModelingMenu", [this](NativeCall& c) { c.result(shop_.open); });
    vm.setNative("InterfaceWindow", "CheckSelectTalkReModelingMenu", [this](NativeCall& c) {
        const bool talk = shop_.talk;
        if (talk) shop_.talk = false, shop_.talking = true;
        c.result(talk);
    });
    vm.setNative("InterfaceWindow", "EndTalkNextReModelingMenu", [this, clerk](NativeCall&) {
        shop_.talking = false;
        if (shop_.open) clerk(shop_.clerk);
    });
    // a dungeon's collection (CollectList in runtime.hpp)
    vm.setNative("InterfaceWindow", "CreateCollectListMain", [this](NativeCall& c) {
        collectListOpen(c.integer(0), c.integer(1, -1), c.integer(2, -1));
    });
    vm.setNative("InterfaceWindow", "CheckEndCollectList", [this](NativeCall& c) {
        if (!askYesNo || autoMenus) collectList_.open = false; // (headless: no one to close it)
        c.result(!collectList_.open);
    });
    vm.setNative("InterfaceWindow", "CheckGeneGetWindow", [this](NativeCall& c) {
        if (!askYesNo) geneGet_.open = false; // (headless: no one to tap it)
        c.result(geneGet_.open);
    });
    vm.setNative("InterfaceWindow", "SelectFreeSpacePort", [this](NativeCall& c) {
        c.result(spacePort_.explore);
        spacePort_.explore = false;
    });
    vm.setNative("EvtFace", "Create", [this](NativeCall& c) {
        const int k = c.integer(0, -1);
        if (k >= 0 && k < kCutIns) cutIns_[size_t(k)] = Portrait{true, false, 0, -1, 1};
    });
    auto endCutIn = [this](int n) {
        static const int kEnd[6] = {0, 1, 2, -1, 3, 4}; // (EndCutInWindow: 3 every one)
        if (n < 0 || n > 5) return;
        for (int k = 0; k < kCutIns; ++k)
            if ((n == 3 || kEnd[n] == k) && cutIns_[size_t(k)].open && !cutIns_[size_t(k)].closing) {
                cutIns_[size_t(k)].closing = true;
                if (k == kCentreCutIn) cutIns_[size_t(k)].time = kCentreCloseSeconds;
            }
    };
    vm.setNative("EvtFace", "Delete", [endCutIn](NativeCall& c) { endCutIn(c.integer(0, -1)); });
    vm.setNative("EvtFace", "DispOff", [endCutIn](NativeCall&) { endCutIn(3); });
    vm.setNative("Cr3FieldMapCamera", "SetPos", [this](NativeCall& c) { fieldCameraX_ = c.number(0), fieldCameraY_ = c.number(1); });
    vm.setNative("Cr3FieldMapCamera", "CalcParameter", [this](NativeCall& c) { fieldCameraX_ = c.number(3), fieldCameraY_ = c.number(4); });
    vm.setNative("Cr3FieldMapCamera", "GetPos_X", [this](NativeCall& c) { c.result(fieldCameraX_); });
    vm.setNative("Cr3FieldMapCamera", "GetPos_Y", [this](NativeCall& c) { c.result(fieldCameraY_); });
    // CR3's gift synthesis (InterfaceWindow.CreateGiftSynthesizeEffectData -> GiftSynthesizeEffect,
    // figure ue003gift01): over when its animation ends (the game: PartsBase::IsEndCurrentAnime);
    // CheckEndGiftSynthesizeEffect true from then (and with no effect: InterfaceMain finds none)
    vm.setNative("InterfaceWindow", "CreateGiftSynthesizeEffectData", [this](NativeCall&) {
        float seconds = 6.0f;
        std::vector<uint8_t> d;
        Animation a;
        if (archive_.read("ue003gift01.anim", d) && a.parse(d)) seconds = a.duration();
        giftEffectEnd_ = frame_ + int(seconds / kTick + 0.999f);
    });
    vm.setNative("InterfaceWindow", "CheckEndGiftSynthesizeEffect", [this](NativeCall& c) { c.result(giftEffectEnd_ < 0 || frame_ >= giftEffectEnd_); });
    vm.setNative("InterfaceWindow", "EndGiftSynthesizeEffect", [this](NativeCall&) { giftEffectEnd_ = -1; });
    // The staff roll (StaffrollMenu): its scrolling is the UI's to come; InterfaceMain answers
    // CheckToBeContinueStaffrollMenu true where no staff roll menu is up - as here
    vm.setNative("InterfaceWindow", "CheckToBeContinueStaffrollMenu", [](NativeCall& c) { c.result(true); });
    // CR3's choice menus and text entry (runtime.hpp)
    for (int kind : {2, 3}) {
        const std::string n = kind == 2 ? "Two" : "Three";
        vm.setNative("InterfaceWindow", "CreateSelect" + n + "ChoiceMenu", [this, kind](NativeCall&) {
            choiceKind_ = kind, choiceAnswer_ = -1, choiceAge_ = 0, choiceClosing_ = -1, ++choicesAsked_;
            choiceText_ = TextData(), choiceIds_.clear();
        });
        vm.setNative("InterfaceWindow", "SetTextDataSelect" + n + "ChoiceMenu", [this](NativeCall& c) { loadText(c.string(0), choiceText_); });
        vm.setNative("InterfaceWindow", "SetTextSelect" + n + "ChoiceMenu", [this, kind](NativeCall& c) {
            choiceIds_.clear();
            for (int k = 0; k < kind; ++k) choiceIds_.push_back(c.integer(k, 0));
        });
        vm.setNative("InterfaceWindow", "GetTapSelect" + n + "ChoiceMenu", [this](NativeCall& c) { c.result(choiceAnswer_); });
        // (SetParamNumber 2: closing - Update waits for the tapped button's comBtnEffe to end)
        vm.setNative("InterfaceWindow", "DeleteSelect" + n + "ChoiceMenu", [this](NativeCall&) {
            if (choiceKind_ == 0 || choiceClosing_ >= 0) return;
            float seconds = 0;
            std::vector<uint8_t> d;
            Animation a;
            if (choiceAnswer_ >= 0 && archive_.read("combtneffe.anim", d) && a.parse(d)) seconds = a.duration();
            choiceClosing_ = 0, choiceCloseSeconds_ = seconds;
        });
        vm.setNative("InterfaceWindow", "GetAliveSelect" + n + "ChoiceMenu", [this, kind](NativeCall& c) { c.result(choiceKind_ == kind); });
    }
    // The team's level-up banner (InterfaceWindow.CreateLevelUpMenu - LevelUpMenu, interface 0xb6:
    // btRsltUpPositon on its _in, faded in over 0.2 s, btRsltLvUpMenu at its call_btRsltLvUpMenu):
    // CheckAliveLevelUpMenu true until btRsltLvUpMenu's animation ends (Update: IsEndCurrentAnime)
    vm.setNative("InterfaceWindow", "CreateLevelUpMenu", [this](NativeCall&) {
        float seconds = 2.0f;
        std::vector<uint8_t> d;
        Animation a;
        if (archive_.read("btrsltlvupmenu.anim", d) && a.parse(d)) seconds = a.duration();
        levelUpAge_ = 0, levelUpSeconds_ = seconds;
    });
    vm.setNative("InterfaceWindow", "CheckAliveLevelUpMenu", [this](NativeCall& c) { c.result(levelUpAge_ >= 0); });
    // The gold key shop (InterfaceWindow.CreateFieldKeyBuyMenu - FieldBuyMenu, interface 0xcd, for
    // GOLDKEY_MASTER_ID: a gold box tapped without a key): GetAliveFieldKeyBuyMenu true until it's done
    // (answerKeyBuy - the viewer's -, or with askYesNo and autoPage a second after that long, nothing bought)
    vm.setNative("InterfaceWindow", "CreateFieldKeyBuyMenu", [this](NativeCall&) { keyBuyAge_ = 0; });
    vm.setNative("InterfaceWindow", "GetAliveFieldKeyBuyMenu", [this](NativeCall& c) { c.result(keyBuyAge_ >= 0); });
    // Cr3System.GetPlatform (platform::GetPlatform): 1 the iOS game, 2 Android's - quest 273 says
    // its notice by it (AnnounceField 34 or 113). The Vita's taken as iOS's
    vm.setNative("Cr3System", "GetPlatform", [this](NativeCall& c) { c.result(archive_.kind() == MvglArchive::Kind::Android ? 2 : 1); });
    // The cleared quest's detail (ClearQuestMenu(id): InterfaceMain::SetClearQuestDetailMenu -
    // QuestDitailMenu as interface 0x7e, the quest's title, text and message, its clear mark):
    // CheckAliveQuestDetailMenu true until its OK closes it (answerClearQuest - the viewer's -, or
    // with askYesNo and autoPage a second after that long)
    vm.setNative("InterfaceWindow", "CreateClearQuestDetailMenu", [this](NativeCall& c) { clearQuest_ = c.integer(0, 0), clearQuestAge_ = 0; });
    vm.setNative("InterfaceWindow", "CheckAliveQuestDetailMenu", [this](NativeCall& c) { c.result(clearQuest_ > 0); });
    vm.setNative("Cr3System", "SetInputDialogText", [this](NativeCall& c) { inputText_ = c.string(0); });
    // (utils::ShowInputDialog(kind): the dialog starts with what's there - 0 the name, 1 the team, 3
    // the pet, else the food)
    vm.setNative("Cr3System", "ShowInputDialog", [this](NativeCall& c) {
        const int kind = c.integer(0, 0);
        inputText_ = enteredNames[kind == 0 ? "name" : kind == 1 ? "team" : kind == 3 ? "pet" : "food"];
        inputOpen_ = true, inputOk_ = true, inputAge_ = 0;
    });
    vm.setNative("Cr3System", "CheckInputDialog", [this](NativeCall& c) { c.result(inputOpen_); });
    vm.setNative("Cr3System", "GetInputDialogResult", [this](NativeCall& c) { c.result(inputOk_); });
    vm.setNative("Cr3System", "GetInputDialogResultNG", [this](NativeCall& c) { c.result(!inputOk_); });
    vm.setNative("Cr3System", "GetInputDialogText", [this](NativeCall& c) { c.result(inputText_); });
    for (const auto& [native, key] : {std::pair<const char*, const char*>{"SetNameFromInputDialogText", "name"}, {"SetTeamNameFromInputDialogText", "team"},
                                      {"SetPetNameFromInputDialogText", "pet"}, {"SetFoodNameFromInputDialogText", "food"}}) {
        const std::string k = key;
        vm.setNative("Cr3System", native, [this, k](NativeCall&) { enteredNames[k] = inputText_; });
    }
    // CR3's Cr3FaceCheckInScreen: !InterfaceMain::CheckCutInNoScreen - a cut-in still on screen
    // (open, or sliding out); Cutin_Delete waits on it turning false
    vm.setNative("EvtFace", "CheckInScreen", [this](NativeCall& c) {
        bool any = false;
        for (const Portrait& p : cutIns_) any = any || p.open;
        c.result(any);
    });
    vm.setNative("EvtFace", "ChangeTexture", [this](NativeCall& c) {
        const int k = c.integer(0, -1), id = c.integer(1, -1);
        if (k >= 0 && k < kCutIns && !cutInImage(id).empty()) cutIns_[size_t(k)].texture = id;
    });
    vm.setNative("EvtFace", "SetColor", [this](NativeCall& c) {
        const int k = c.integer(0, -1);
        if (k >= 0 && k < kCutIns) cutIns_[size_t(k)].bright = c.number(1, 1);
    });
    vm.setNative("EvtWindow", "SetMessage", [this](NativeCall& c) {
        messageWindow_ = c.integer(0, Vista);
        message_ = c.integer(1);
        const auto own = windowText_.find(messageWindow_);
        const TextData& td = own != windowText_.end() ? own->second : text_;
        parsed_ = parseMessage(warnKeys(takeParameters(messageWindow_, filterMessage(td.message(message_, language)))));
        // (its voice - the text's trailer: EvtWindow::PlayVoice as its page starts, the last one stopped)
        if (const int v = td.voice(message_); v >= 0 && soundHooks().playVoice) {
            if (soundHooks().stopAllVoices) soundHooks().stopAllVoices(0);
            soundHooks().playVoice(v);
        }
        page_ = 0, pageTime_ = 0, revealed_ = 0, stop_ = 0, stopTime_ = 0;
        if (parsed_.pages.empty()) message_ = 0;
        else ++messagesSet_;
    });
    // PutParameterI / S(window, key, value): the window's next message has each key ("{next}") in it
    // replaced by the value (EvtWindowMessage::PutParameter - "%d" for a number -, FilterMessage:
    // after the names, then the window's parameters are gone)
    vm.setNative("EvtWindow", "PutParameterI", [this](NativeCall& c) { parameters_[c.integer(0, Vista)].push_back({c.string(1), std::to_string(c.integer(2, 0))}); });
    vm.setNative("EvtWindow", "PutParameterS", [this](NativeCall& c) { parameters_[c.integer(0, Vista)].push_back({c.string(1), c.string(2)}); });
    // SetMessageText(window, text): the text itself as the message (EvtWindow::SetMessage(char*))
    vm.setNative("EvtWindow", "SetMessageText", [this](NativeCall& c) {
        messageWindow_ = c.integer(0, Vista), message_ = 0x7fff;
        parsed_ = parseMessage(warnKeys(takeParameters(messageWindow_, filterMessage(c.string(1)))));
        page_ = 0, pageTime_ = 0, revealed_ = 0, stop_ = 0, stopTime_ = 0;
        if (parsed_.pages.empty()) message_ = 0;
        else ++messagesSet_;
    });
    // SetWaitButtonEnable(window, on) (PermitPageSend): off, a tap or the page time doesn't turn the
    // page - the script ends it (each Open allows it again)
    vm.setNative("EvtWindow", "SetWaitButtonEnable", [this](NativeCall& c) {
        if (c.boolean(1)) pageSendOff_.erase(c.integer(0, Vista));
        else pageSendOff_.insert(c.integer(0, Vista));
    });
    // SetItemMessage(window, item): announce 0x75 ("... %s ...") with the item's name (EvtWindow::SetItemMessage)
    vm.setNative("EvtWindow", "SetItemMessage", [this](NativeCall& c) {
        const int w = c.integer(0, 7), id = c.integer(1, 0);
        if (id <= 0) return;
        if (itemNames_.count() == 0) loadText("ItemName", itemNames_);
        const auto own = windowText_.find(w);
        TextData announce;
        if (own == windowText_.end()) loadText("AnnounceBattle", announce);
        std::string t = (own != windowText_.end() ? own->second : announce).message(0x75, language);
        if (const size_t p = t.find("%s"); p != std::string::npos) t.replace(p, 2, itemNames_.message(id, language));
        messageWindow_ = w, message_ = 0x75, itemShown_ = id;
        parsed_ = parseMessage(warnKeys(takeParameters(w, filterMessage(t))));
        page_ = 0, pageTime_ = 0, revealed_ = 0, stop_ = 0, stopTime_ = 0;
        if (parsed_.pages.empty()) message_ = 0;
        else ++messagesSet_;
    });
    // SetItemMessageWithCategory(window, category, id, n) (EvtWindow::SetItemMessageWithCategory -
    // the treasure boxes' tbox_msg): AnnounceField 0x11 "{item.name}" (n under 2) or 0x13 with
    // "{item.count}", the name item_name.mbe's - 0 ItemName .. 6 GiftName by the id -; category 10
    // gold, 0x12 "{gold}" the id
    vm.setNative("EvtWindow", "SetItemMessageWithCategory", [this](NativeCall& c) {
        const int w = c.integer(0, 7), category = c.integer(1, 0), id = c.integer(2, 0), n = c.integer(3, 1);
        if (id < 1) return;
        const auto own = windowText_.find(w);
        TextData announce;
        if (own == windowText_.end()) loadText("AnnounceField", announce);
        const TextData& from = own != windowText_.end() ? own->second : announce;
        auto put = [](std::string& t, const std::string& key, const std::string& with) {
            for (size_t at = t.find(key); at != std::string::npos; at = t.find(key, at + with.size())) t.replace(at, key.size(), with);
        };
        int message = 0x12;
        std::string t;
        if (category == 10) {
            t = from.message(message, language), put(t, "{gold}", std::to_string(id));
        } else if (category >= 0 && category <= 6) {
            static const char* const kNames[7] = {"ItemName", "WeaponName", "ArmorName", "AccessoryName", "CheetName", "EventName", "GiftName"};
            static MbeFile names;
            if (names.tables().empty()) {
                std::vector<uint8_t> d;
                if (archive_.read("data\\item_name.mbe", d)) names.parse(d);
            }
            MbeFile::Row r;
            std::string name;
            if (names.find(kNames[category], id, "ipp", r)) name = language == 0 || r.s(2).empty() ? r.s(1) : r.s(2);
            message = n < 2 ? 0x11 : 0x13;
            t = from.message(message, language), put(t, "{item.name}", name), put(t, "{item.count}", std::to_string(n));
        } else return;
        messageWindow_ = w, message_ = message, itemShown_ = category == 10 ? -10 : category * 100000 + id;
        parsed_ = parseMessage(warnKeys(takeParameters(w, filterMessage(t))));
        page_ = 0, pageTime_ = 0, revealed_ = 0, stop_ = 0, stopTime_ = 0;
        if (parsed_.pages.empty()) message_ = 0;
        else ++messagesSet_;
    });
    vm.setNative("EvtWindow", "CheckMessage", [this](NativeCall& c) { c.result(message_ == 0); });
    vm.setNative("EvtWindow", "ShowYesNoButton", [this](NativeCall&) { yesNoShown_ = true, yesNo_ = 0, yesNoAge_ = 0; });
    vm.setNative("EvtWindow", "HideYesNoButton", [this](NativeCall&) { yesNoShown_ = false; });
    vm.setNative("EvtWindow", "CheckYesNoButton", [this](NativeCall& c) {
        if (askYesNo) c.result(yesNo_);
    });
    // SkipMessage: the page shown whole at once (EvtWindowMessage::SkipMessage - InterfaceMain::
    // SkipMessageWindowStringDisplay: its typing skipped; the message stays till a tap or Close)
    vm.setNative("EvtWindow", "SetSkipButtonEnable", [this](NativeCall& c) {
        if (c.boolean(1)) skipOff_.erase(c.integer(0, Vista));
        else skipOff_.insert(c.integer(0, Vista));
    });
    vm.setNative("EvtWindow", "SkipMessage", [this](NativeCall&) { if (message_ > 0) showWhole(); });
    // the fades: (type, frames), the 2s (type, frames, from %, to %)
    vm.setNative("Cr3Fade", "FadeIn", [this](NativeCall& c) { fade_.start(fadeColour(c.integer(0)), c.integer(1), 1, 0); });
    vm.setNative("Cr3Fade", "FadeOut", [this](NativeCall& c) { fade_.start(fadeColour(c.integer(0)), c.integer(1), 0, 1); });
    auto fade2 = [this](NativeCall& c) {
        fade_.start(fadeColour(c.integer(0)), c.integer(1), c.number(2) / 100.0f, c.number(3) / 100.0f);
    };
    vm.setNative("Cr3Fade", "SetEffectLine", [this](NativeCall&) { effectLine = true; });
    vm.setNative("Cr3Fade", "DeleteEffectLine", [this](NativeCall&) { effectLine = false; });
    vm.setNative("Cr3Fade", "FadeIn2", fade2);
    vm.setNative("Cr3Fade", "FadeOut2", fade2);
    vm.setNative("Cr3Fade", "BackFadeIn", [this](NativeCall& c) { backFade_.start(fadeColour(c.integer(0)), c.integer(1), 1, 0); });
    vm.setNative("Cr3Fade", "BackFadeOut", [this](NativeCall& c) { backFade_.start(fadeColour(c.integer(0)), c.integer(1), 0, 1); });
    auto backFade2 = [this](NativeCall& c) {
        backFade_.start(fadeColour(c.integer(0)), c.integer(1), c.number(2) / 100.0f, c.number(3) / 100.0f);
    };
    vm.setNative("Cr3Fade", "BackFadeIn2", backFade2);
    vm.setNative("Cr3Fade", "BackFadeOut2", backFade2);
    // the vista's skip button (EvtSkip): held - the viewer's fastForward while it shows
    vm.setNative("EvtSkip", "CheckButtonTouch", [this](NativeCall& c) { c.result(skipHeld()); });
    vm.setNative("EvtSkip", "ButtonCreate", [this](NativeCall&) { skipButton_ = true; });
    vm.setNative("EvtSkip", "ButtonDelete", [this](NativeCall&) { skipButton_ = false; });
    vm.setNative("EvtSkip", "ButtonSetVisible", [this](NativeCall& c) { skipButton_ = c.boolean(0); });
    // The fast-forward window (InterfaceWindow.CreateFastForwardWindow - FastForwardWindow, interface
    // 0xd0: a FastForwardButton - speedUp - whose hold Cr3EvtSkipButtonCheckTouch counts as the skip
    // button's, CheckPressFastForwardWindow): shown from SetVisibleFastForwardWindow(true) till
    // (false) or EndDeleteFastForwardWindow - here the skip button's
    vm.setNative("InterfaceWindow", "CreateFastForwardWindow", [this](NativeCall&) { fastWindow_ = 1; });
    vm.setNative("InterfaceWindow", "SetVisibleFastForwardWindow", [this](NativeCall& c) {
        if (fastWindow_) fastWindow_ = c.boolean(0) ? 2 : 1;
    });
    vm.setNative("InterfaceWindow", "EndDeleteFastForwardWindow", [this](NativeCall&) { fastWindow_ = 0; });
    vm.setNative("InterfaceWindow", "GetAliveFastForwardWindow", [this](NativeCall& c) { c.result(fastWindow_ != 0); });
    vm.setNative("InterfaceWindow", "CheckPressFastForwardWindow", [this](NativeCall& c) { c.result(fastForward && fastWindow_ == 2); });
    // (SetVistaSkipButtonEnable(on): InterfaceMain::SetFastForwardButtonVistaMessageWindow - the
    // vista window's own speedUp, as SetSkipButtonEnable(1, on))
    vm.setNative("EvtWindow", "SetVistaSkipButtonEnable", [this](NativeCall& c) {
        if (c.boolean(0)) skipOff_.erase(Vista);
        else skipOff_.insert(Vista);
    });
    // the sound (Cr3Sound -> utils::Sound*: soundHooks, the player's; none set - silence, GetState
    // -2): CR3's by name - PlaySE(name) / PlaySE_Repeat(name: looping) / PlaySE_Fade(name, frames
    // up, loop), StopSE(name, frames), StopSEAll(frames), PlayVoice(name), StopVoiceAll(frames),
    // IsPlaying(name) -, PlayBGM(track, fade frames: m<NN>, looping) / PlayOnceBGM (once),
    // StopBGM(frames), ReleaseBGM, GetPlayBgmNo, GetState(0 the music, 2 a voice: 2 on, else 1). The
    // numbered forms (CR2-U's banks) where no named hook is set
    vm.setNative("Cr3Sound", "PlayBGM", [](NativeCall& c) {
        if (soundHooks().playBgmOnce) soundHooks().playBgmOnce(c.integer(0), c.integer(1), true);
        else if (soundHooks().playBgm) soundHooks().playBgm(c.integer(0), c.integer(1));
    });
    vm.setNative("Cr3Sound", "PlayOnceBGM", [](NativeCall& c) {
        if (soundHooks().playBgmOnce) soundHooks().playBgmOnce(c.integer(0), c.integer(1), false);
    });
    vm.setNative("Cr3Sound", "StopBGM", [](NativeCall& c) {
        if (soundHooks().stopBgm) soundHooks().stopBgm(c.integer(0));
    });
    vm.setNative("Cr3Sound", "ReleaseBGM", [](NativeCall&) {
        if (soundHooks().releaseBgm) soundHooks().releaseBgm();
    });
    vm.setNative("Cr3Sound", "GetPlayBgmNo", [](NativeCall& c) { c.result(soundHooks().bgmNo ? soundHooks().bgmNo() : -1); });
    vm.setNative("Cr3Sound", "GetState", [](NativeCall& c) { c.result(soundHooks().state ? soundHooks().state(c.integer(0)) : -2); });
    const auto playSe = [](NativeCall& c, bool loop, int fade) {
        if (soundHooks().playSeNamed) c.result(soundHooks().playSeNamed(c.string(0), loop, fade));
        else c.result(soundHooks().playSe ? soundHooks().playSe(c.integer(0), loop) : -1);
    };
    vm.setNative("Cr3Sound", "PlaySE", [playSe](NativeCall& c) { playSe(c, false, 0); });
    vm.setNative("Cr3Sound", "PlaySE_Repeat", [playSe](NativeCall& c) { playSe(c, true, 0); });
    vm.setNative("Cr3Sound", "PlaySE_Fade", [playSe](NativeCall& c) { playSe(c, c.boolean(2), c.integer(1)); });
    vm.setNative("Cr3Sound", "StopSE", [](NativeCall& c) {
        if (soundHooks().stopSeNamed) soundHooks().stopSeNamed(c.string(0), c.integer(1));
        else if (soundHooks().stopSe) soundHooks().stopSe(c.integer(0, -1), c.integer(1));
    });
    vm.setNative("Cr3Sound", "StopSEAll", [](NativeCall& c) {
        if (soundHooks().stopAllSe) soundHooks().stopAllSe(c.integer(0));
    });
    vm.setNative("Cr3Sound", "PlayVoice", [](NativeCall& c) {
        if (soundHooks().playVoiceNamed) c.result(soundHooks().playVoiceNamed(c.string(0)));
        else c.result(soundHooks().playVoice ? soundHooks().playVoice(c.integer(0)) : -1);
    });
    vm.setNative("Cr3Sound", "StopVoiceAll", [](NativeCall& c) {
        if (soundHooks().stopAllVoices) soundHooks().stopAllVoices(c.integer(0));
    });
    vm.setNative("Cr3Sound", "IsPlaying", [](NativeCall& c) { c.result(soundHooks().playingNamed && soundHooks().playingNamed(c.string(0))); });
}

void EventRuntime::reset() {
    scene_ = EventScene();
    fade_ = EventFade(), backFade_ = EventFade();
    faces_.clear();
    text_ = TextData(), names_ = TextData();
    windowText_.clear(), tutorialPicture_ = TutorialPicture();
    openWindows_.clear(), opened_.clear(), closing_.clear();
    yesNoShown_ = false, yesNo_ = 0;
    cutIns_ = {}, cutInImages_.clear();
    geneLab_ = GeneLab();
    lunch_ = Lunch();
    messageWindow_ = Vista, message_ = page_ = revealed_ = 0, pageTime_ = stopTime_ = 0, stop_ = 0;
    execution_ = false;
    terminated_ = drawSkip_ = false;
    wait_ = frame_ = 0;
}

bool EventRuntime::start(const std::string& function) { return start(function, function); }

bool EventRuntime::start(const std::string& name, const std::string& function) {
    error_.clear();
    reset();
    state_.next.battle = -1;
    vm_ = std::make_unique<ScriptVM>();
    bindGameState(*vm_, state_);
    bindNatives(*vm_);
    if (extraNatives) extraNatives(*vm_);
    std::vector<uint8_t> bytecode;
    const std::string script = "script\\" + lower(name) + ".nut";
    if (!archive_.read(script, bytecode, &error_) || !vm_->load(bytecode, script) || !vm_->runMain()) {
        if (error_.empty()) error_ = script + ": " + vm_->error();
        return false;
    }
    const std::string fn = vm_->findFunction(function);
    if (fn.empty()) return error_ = script + ": no function " + function, false;
    if (!vm_->call(fn)) return error_ = fn + ": " + vm_->error(), false;
    return true;
}

bool EventRuntime::call(const std::string& function) {
    error_.clear();
    if (!vm_ || vm_->waiting()) return error_ = "the script is still running", false;
    const std::string fn = vm_->findFunction(function);
    if (fn.empty()) return error_ = "no function " + function, false;
    if (!vm_->call(fn)) return error_ = fn + ": " + vm_->error(), false;
    return true;
}

bool EventRuntime::running() const { return vm_ && vm_->waiting(); }
std::string EventRuntime::where() const { return vm_ ? vm_->where() : std::string(); }

bool EventRuntime::tick() { return update(vm_.get()); }

// a page's voice still speaking (a page turning by itself waits for it - the game's wait for a tap)
static bool voiceSpeaking() { return soundHooks().voicePlaying && soundHooks().voicePlaying(); }

bool EventRuntime::update(ScriptVM* vm) {
    ++frame_;
    scene_.update(kTick);
    if (vm && vm->waiting() && --wait_ <= 0) {
        wait_ = 0;
        if (!vm->resume()) return error_ = vm->error(), false;
    }
    if (!parsed_.noSkip && ((fastForward && fastForwardShown()) || (skipHeld() && message_ > 0))) { // (a button held: the page whole, then the next at once - EvtWindow::IsSkip)
        if (!typed()) showWhole();
        else advance();
    } else if (message_ > 0 && !typed()) { // (ProvisionalMenuText::Step: speed 1 a drawn frame - the game's 60 a second, two of our updates' worth; the page's time counts once it's typed)
        if (const TextStop* s = held(); !s) revealed_ += 2;
        else if (s->wait > 0 && (stopTime_ += kTick) * 30 >= float(s->wait)) ++stop_, stopTime_ = 0; // (\w: its n in 30ths of a second)
        else if (s->wait == 0 && autoPage > 0 && !voiceSpeaking() && (pageTime_ += kTick) >= autoPage) advance(); // (a tap mark: as a page)
    } else if (message_ > 0 && ending()) message_ = 0, page_ = 0; // (\*: its end by itself)
    else if ((message_ > 0 || execution_) && autoPage > 0 && !voiceSpeaking() && (pageTime_ += kTick) >= autoPage) advance(); // (its voice heard out first)
    fade_.update(), backFade_.update();
    if (yesNoShown_ && (yesNoAge_ += kTick) >= autoPage && autoPage > 0 && askYesNo && !yesNo_) yesNo_ = 1; // (pages by themselves: yes)
    if (choiceKind_ && choiceAnswer_ < 0 && (choiceAge_ += kTick) >= autoPage && autoPage > 0 && askYesNo) choiceAnswer_ = (choicesAsked_ - 1) % choiceKind_; // (the first, then the next ...)
    else if (choiceKind_ && choiceAnswer_ >= 0 && choiceClosing_ < 0) choiceAge_ += kTick;
    if (choiceKind_ && choiceClosing_ >= 0 && (choiceClosing_ += kTick) >= choiceCloseSeconds_) choiceKind_ = 0, choiceClosing_ = -1;
    if (levelUpAge_ >= 0 && (levelUpAge_ += kTick) >= levelUpSeconds_) levelUpAge_ = -1;
    if (keyBuyAge_ >= 0 && (keyBuyAge_ += kTick) >= autoPage + 1 && autoPage > 0 && askYesNo) keyBuyAge_ = -1;
    if (clearQuest_ > 0 && (clearQuestAge_ += kTick) >= autoPage + 1 && autoPage > 0 && askYesNo) clearQuest_ = 0; // (pages by themselves: its OK)
    if (inputOpen_ && (inputAge_ += kTick) >= autoPage && autoPage > 0 && askYesNo) inputOpen_ = false; // (its text as given, accepted)
    if (geneGet_.open && (geneGet_.age += kTick) >= autoPage + 1 && autoPage > 0 && askYesNo) geneGet_.open = false; // (pages by themselves: a tap)
    for (auto& [w, t] : opened_) t += kTick;
    if (mapName_ >= 0 && (mapNameAge_ += kTick) >= kMapNameSeconds) mapName_ = -1;
    for (auto it = closing_.begin(); it != closing_.end();) // (out animations: a second at most)
        it = (it->second += kTick) > 1.0f ? closing_.erase(it) : std::next(it);
    for (Portrait& c : cutIns_) {
        if (!c.open) continue;
        c.age += kTick;
        if (!c.closing) c.time = std::min(c.time + kTick, kCutInSeconds);
        else if ((c.time -= kTick) <= 0) c = Portrait(); // (back to its start: gone)
    }
    return true;
}

float EventRuntime::windowAge(int w) const {
    if (auto it = opened_.find(w); it != opened_.end()) return it->second;
    if (auto it = closing_.find(w); it != closing_.end()) return it->second;
    return -1;
}

void EventRuntime::openGeneLab(const std::string& image, bool vs) {
    geneLab_ = GeneLab();
    geneLab_.open = true, geneLab_.vs = vs;
    geneLab_.clerk = image;
    cutInImages_.insert(cutInImages_.begin(), {kClerkFace, image});
    Portrait p{true, false, 0, kClerkFace, 1};
    p.shop = true;
    cutIns_[2] = p;
}

void EventRuntime::spacePortTalk() {
    if (!spacePort_.open || spacePort_.talk) return;
    spacePort_.talk = true;
    if (cutIns_[2].open && cutIns_[2].shop) cutIns_[2].closing = true;
}

void EventRuntime::spacePortClerk(bool shown) {
    if (!spacePort_.open) return;
    if (!shown) {
        if (cutIns_[2].open && cutIns_[2].shop) cutIns_[2].closing = true;
        return;
    }
    if (cutIns_[2].open && cutIns_[2].shop && !cutIns_[2].closing) return;
    Portrait p{true, false, 0, kClerkFace, 1};
    p.shop = true;
    cutIns_[2] = p;
}

void EventRuntime::openWorldSelect(bool fromPort) {
    worldSelect_ = WorldSelect();
    worldSelect_.open = true, worldSelect_.fromPort = fromPort;
    spacePort_.questReturn = 0; // (InterfaceMain +0x9c: 0 - Update 0x22)
}

void EventRuntime::worldSelectEnd(bool chosen) {
    if (!worldSelect_.open) return;
    const bool fromPort = worldSelect_.fromPort;
    worldSelect_.open = false;
    if (chosen || !fromPort) {
        spacePort_.questReturn = 3;
        return;
    }
    spacePort_ = SpacePort(); // (0x23: CreateSpacePortMenuMain - the script still loops on it)
    spacePort_.open = true;
    menuFade(false, 5);
    cutInImages_.insert(cutInImages_.begin(), {kClerkFace, kSpacePortClerk});
    Portrait p{true, false, 0, kClerkFace, 1};
    p.shop = true;
    cutIns_[2] = p;
}

void EventRuntime::spacePortEnd(int questReturn) {
    if (!spacePort_.open) return;
    spacePort_.open = spacePort_.talk = false;
    spacePort_.questReturn = questReturn;
    if (cutIns_[2].open && cutIns_[2].shop) cutIns_[2].closing = true;
}

void EventRuntime::geneLabEnd() {
    if (!geneLab_.open) return;
    geneLab_.open = geneLab_.talk = geneLab_.talking = false;
    if (cutIns_[2].open && cutIns_[2].shop) cutIns_[2].closing = true;
}

void EventRuntime::geneLabClerk(bool shown) {
    if (!geneLab_.open) return;
    if (!shown) {
        if (cutIns_[2].open && cutIns_[2].shop) cutIns_[2].closing = true;
        return;
    }
    if (cutIns_[2].open && cutIns_[2].shop && !cutIns_[2].closing) return;
    Portrait p{true, false, 0, kClerkFace, 1};
    p.shop = true;
    cutIns_[2] = p;
}

std::string EventRuntime::cutInImage(int id) const {
    for (const auto& [i, image] : cutInImages_)
        if (i == id) return image;
    return {};
}

void EventRuntime::advance() {
    pageTime_ = 0;
    if (execution_) {
        execution_ = false;
        return;
    }
    if (message_ <= 0) return;
    if (!typed()) {
        const std::vector<TextStop>& stops = parsed_.pages[size_t(page_)].stops;
        if (const TextStop* s = held()) { // (at a tap mark: the page's next part; a timed one reads no tap)
            if (s->wait == 0) ++stop_;
            return;
        }
        // (else EvtWindowPage::Skip: on to the next tap mark, past the timed ones)
        while (stop_ < stops.size() && stops[stop_].wait > 0) ++stop_;
        if (stop_ < stops.size()) revealed_ = std::max(revealed_, stops[stop_].at), stopTime_ = 0;
        else showWhole();
        return;
    }
    if (pageSendOff_.count(messageWindow_)) return; // (SetWaitButtonEnable false: the script's to end)
    revealed_ = 0, stop_ = 0, stopTime_ = 0;
    if (++page_ >= int(parsed_.pages.size())) message_ = 0, page_ = 0;
}

bool EventRuntime::typed() const {
    if (message_ <= 0 || page_ >= int(parsed_.pages.size())) return true;
    const TextPage& p = parsed_.pages[size_t(page_)];
    int n = 0;
    for (const TextLine& l : p.lines)
        for (unsigned char c : l.text) n += (c & 0xc0) != 0x80; // (code points)
    return revealed_ >= n && stop_ >= p.stops.size();
}

void EventRuntime::showWhole() {
    revealed_ = 1 << 20, stopTime_ = 0;
    if (message_ > 0 && page_ < int(parsed_.pages.size())) stop_ = parsed_.pages[size_t(page_)].stops.size();
}

bool EventRuntime::ending() const { return parsed_.ends && page_ + 1 >= int(parsed_.pages.size()) && typed(); }

const TextStop* EventRuntime::held() const {
    if (message_ <= 0 || page_ >= int(parsed_.pages.size())) return nullptr;
    const std::vector<TextStop>& stops = parsed_.pages[size_t(page_)].stops;
    return stop_ < stops.size() && revealed_ >= stops[stop_].at ? &stops[stop_] : nullptr;
}

bool EventRuntime::awaitingTap() const {
    const TextStop* s = held();
    return (typed() && !ending()) || (s && s->wait == 0);
}

std::vector<std::vector<std::string>> EventRuntime::choiceLines() const {
    std::vector<std::vector<std::string>> out;
    for (int id : choiceIds_) {
        std::vector<std::string> lines;
        const ParsedMessage m = parseMessage(filterMessage(choiceText_.message(id, language)));
        if (!m.pages.empty())
            for (const TextLine& l : m.pages[0].lines)
                if (lines.size() < 2) lines.push_back(l.text);
        out.push_back(lines);
    }
    return out;
}

std::string EventRuntime::takeParameters(int window, std::string text) {
    const auto it = parameters_.find(window);
    if (it == parameters_.end()) return text;
    for (const auto& [key, with] : it->second)
        if (!key.empty())
            for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + with.size())) text.replace(at, key.size(), with);
    parameters_.erase(it);
    return text;
}

std::string EventRuntime::warnKeys(std::string text) const {
    static const bool trace = std::getenv("CR3_STUBS") != nullptr; // (tests: a key no parameter or name filled)
    if (trace)
        for (size_t at = text.find('{'); at != std::string::npos; at = text.find('{', at + 1))
            if (const size_t end = text.find('}', at); end != std::string::npos) std::fprintf(stderr, "message key left: %s\n", text.substr(at, end + 1 - at).c_str());
    return text;
}

std::string EventRuntime::filterMessage(std::string text) const {
    for (const char* key : {"name", "team", "pet", "food"}) {
        const std::string tag = std::string("{") + key + "}";
        const auto it = enteredNames.find(key);
        const std::string with = it != enteredNames.end() ? it->second : std::string();
        for (size_t at = text.find(tag); at != std::string::npos; at = text.find(tag, at + with.size())) text.replace(at, tag.size(), with);
    }
    if (const size_t at = text.find("{goldkey}"); at != std::string::npos) { // (iOS's: the gold keys held - GOLDKEY_MASTER_ID)
        const int id = state_.cr3Db ? state_.cr3Db->constant("GOLDKEY_MASTER_ID", 400005) : 400005;
        const auto held = state_.cr3Items.find(id);
        const std::string with = std::to_string(held != state_.cr3Items.end() ? std::max(held->second, 0) : 0);
        for (size_t p = at; p != std::string::npos; p = text.find("{goldkey}", p + with.size())) text.replace(p, 9, with);
    }
    return text;
}

std::string EventRuntime::speakerName() const {
    if (parsed_.speaker < 0) return {};
    const ParsedMessage n = parseMessage(filterMessage(names_.message(parsed_.speaker + 1, language)));
    return n.pages.empty() || n.pages[0].lines.empty() ? std::string() : n.pages[0].lines[0].text;
}

std::map<std::string, int> EventRuntime::stubCalls() const { return vm_ ? vm_->stubCalls() : std::map<std::string, int>(); }

void EventRuntime::shopTalk() {
    if (!shop_.open || shop_.talking) return;
    shop_.talk = true;
    if (cutIns_[2].open && cutIns_[2].shop) cutIns_[2].closing = true;
}

void EventRuntime::shopEnd() {
    if (!shop_.open) return;
    shop_.open = shop_.talk = shop_.talking = false;
    if (cutIns_[2].open && cutIns_[2].shop) cutIns_[2].closing = true;
}

void EventRuntime::shopClerk(bool shown) {
    if (!shop_.open) return;
    if (!shown) {
        if (cutIns_[2].open && cutIns_[2].shop) cutIns_[2].closing = true;
        return;
    }
    if (cutIns_[2].open && cutIns_[2].shop && !cutIns_[2].closing) return;
    Portrait p{true, false, 0, kClerkFace, 1};
    p.shop = true;
    cutIns_[2] = p;
}

void EventRuntime::openGeneGet(int gene, int level, bool isNew, bool se) {
    if (!geneGet_.open) geneGet_ = GeneGet(), geneGet_.open = true;
    if (gene > 0) geneGet_.gene = gene, geneGet_.level = std::max(level, 1), geneGet_.isNew = isNew, geneGet_.se = se, geneGet_.age = 0;
}

} // namespace cr3
