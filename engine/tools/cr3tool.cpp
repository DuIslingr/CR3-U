// cr3tool: CR3-U's command-line checks on your own copy of the game's data.
//
//   cr3tool check <archive.mvgl | .obb>              every file decodes to its size
//   (an <archive> may be its folder; an <executable> "-": the maps' table found beside the archive -
//   the iPad's executable, the Vita's eboot.bin, the Android release's APK beside its OBB)
//   cr3tool scripts <archive> [name]                 every script (script\*.nut) loads in the
//                                                    Squirrel 2.2.4 VM and its main runs; the
//                                                    native stubs it calls
//   cr3tool run <archive> <script> [function]        loads one script, runs main, then its
//                                                    function (default: the script's name) with
//                                                    every native a stub; the stubs it reached
//   cr3tool models <archive>                         every mesh file parses; the textures its
//                                                    materials name are in the archive
//   cr3tool compare-models <archive> <archive>       the mesh files both have, decoded: the
//                                                    largest differences in positions, uvs, normals
//   cr3tool animations <archive>                     skeletons: base pose * inverse bind = identity
//                                                    for the skinned joints; every animation:
//                                                    unit rotations, frame set boundaries continuous
//   cr3tool pose <archive> <figure> <anim> <frame>   the animation's pose at a frame (joint: R, T, S)
//   cr3tool images <archive> [pattern [out dir]]     every texture decodes (formats counted);
//                                                    with a folder, those matching as .tga
//   cr3tool walk <archive> <executable> [map]       the field player (field/character.hpp, in the
//                                                    game's updates) on every map's floors: dropped
//                                                    at each pop point (else defaultPos), run 4 s in
//                                                    8 directions - on a floor throughout, never
//                                                    beyond a wall
//   cr3tool exits <archive> <executable>             every map's exits: the map each leads to (the
//                                                    pop point's first five letters) is there and
//                                                    has that point
//   cr3tool fieldscript <archive> <executable> [map] every map's entry (its dungeon script's
//                                                    evt000, a new game's state): runs or not, the
//                                                    gimmicks it shows, the stub natives it reached
//   cr3tool event <archive> <script> [function]     an event's timeline: its native calls frame by
//                                                    frame (the waits met at once)
//   cr3tool play <archive> <event | all>             an event played by the runtime (event/
//                                                    runtime.hpp, its pages turning by themselves):
//                                                    its messages (numbers, not text), fades, camera,
//                                                    end; "all": every event script's, to its end
//   cr3tool shaders <archive>                        the materials' shaders: the features their
//                                                    GLSL has, and where the key decoder (gfx/
//                                                    material.cpp) disagrees with it, per bit
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "assets/geom.hpp"
#include "battle/battledata.hpp"
#include "battle/glass.hpp"
#include "script/gamesave.hpp"
#include "battle/rules.hpp"
#include "assets/image.hpp"
#include "assets/skeleton.hpp"
#include "assets/language.hpp"
#include "assets/text.hpp"
#include "assets/mvgl.hpp"
#include "assets/release.hpp"
#include "battle/script.hpp"
#include "battle/cr3battle.hpp"
#include "battle/cr3rules.hpp"
#include "db/cr3db.hpp"
#include "field/character.hpp"
#include "field/fieldmap.hpp"
#include "event/runtime.hpp"
#include "field/fieldscript.hpp"
#include "gfx/material.hpp"
#include "script/vm.hpp"
#include "audio/cr3sound.hpp"
#include "audio/sounddata.hpp"

namespace {

bool openArchive(cr3::MvglArchive& a, const char* path);

// how alike two runs of a stream's frames are: 1 - the RMS of their difference over the RMS of
// the first (1 the same; ~0 unrelated)
double alike(cr3::MusicStream& s, int64_t a, int64_t b, int n) {
    const int ch = s.channels();
    std::vector<int16_t> x(size_t(n) * size_t(ch)), y(x.size());
    if (!s.seek(uint32_t(a)) || s.read(x.data(), n) != n || !s.seek(uint32_t(b)) || s.read(y.data(), n) != n) return -1;
    double e = 0, r = 0;
    for (size_t i = 0; i < x.size(); ++i) e += double(x[i] - y[i]) * (x[i] - y[i]), r += double(x[i]) * x[i];
    return r > 0 ? 1.0 - std::sqrt(e / r) : 1.0;
}

// where the frames after `b` best line up with those after `a`: the offset from b (within
// +-range) and how alike (alike()'s measure) - 0 where the loop's points are exact
std::pair<int, double> bestLag(cr3::MusicStream& s, int64_t a, int64_t b, int n, int range) {
    const int ch = s.channels();
    std::vector<int16_t> x(size_t(n) * size_t(ch)), y(size_t(n + 2 * range) * size_t(ch));
    if (b - range < 0 || !s.seek(uint32_t(a)) || s.read(x.data(), n) != n || !s.seek(uint32_t(b - range)) || s.read(y.data(), n + 2 * range) != n + 2 * range)
        return {0, -1};
    double r = 0;
    for (int16_t v : x) r += double(v) * v;
    std::pair<int, double> best{0, -1e9};
    for (int lag = -range; lag <= range; ++lag) {
        double e = 0;
        const int16_t* q = &y[size_t(lag + range) * size_t(ch)];
        for (size_t i = 0; i < x.size(); ++i) e += double(x[i] - q[i]) * (x[i] - q[i]);
        const double m = r > 0 ? 1.0 - std::sqrt(e / r) : 1.0;
        if (m > best.second) best = {lag, m};
    }
    return best;
}

// the release's sound (audio/cr3sound.hpp): every track opens and its loop is seamless (the
// frames after its end repeat those after its start), every effect decodes, voices too (a
// sample of them; all with --full)
int sound(const char* path, bool full) {
    cr3::Database db;
    if (!openArchive(db, path)) return 1;
    cr3::Cr3Sound snd;
    std::string err;
    if (!snd.open(db, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::printf("%s release: %zu tracks, %zu effects, %zu voices\n", db.kindName(), snd.tracks().size(), snd.effects().size(), snd.voices().size());
    int bad = 0, seamless = 0, looped = 0, opened = 0;
    for (const cr3::BgmTrack& t : snd.tracks()) {
        std::unique_ptr<cr3::MusicStream> s = snd.openTrack(t.name, &err);
        if (!s) {
            std::printf("%s: %s\n", t.name.c_str(), err.c_str());
            ++bad;
            continue;
        }
        ++opened;
        std::string loop = "no loop";
        if (s->loopStart >= 0) {
            ++looped;
            const int n = s->rate() / 10; // (0.1 s)
            const double m = s->loopEnd + n <= int64_t(s->frames()) ? alike(*s, s->loopStart, s->loopEnd, n) : -2;
            const auto lag = m == -2 ? std::pair<int, double>{0, -2} : bestLag(*s, s->loopStart, s->loopEnd, n / 4, 256);
            char b[160];
            std::snprintf(b, sizeof b, "loop %lld..%lld, %s %.3f (best %.3f at %+d)", static_cast<long long>(s->loopStart),
                          static_cast<long long>(s->loopEnd), m == -2 ? "(ends at its loop)" : "repeat match", m, lag.second, lag.first);
            loop = b;
            if (m >= 0.9 || m == -2) ++seamless;
        }
        std::printf("%s: %d Hz x%d, %.1f s, %s\n", t.name.c_str(), s->rate(), s->channels(), double(s->frames()) / s->rate(), loop.c_str());
    }
    int effects = 0;
    double seconds = 0;
    for (const std::string& e : snd.effects()) {
        cr3::Cr3Sound::Clip c;
        if (snd.effect(e, c, &err)) ++effects, seconds += double(c.pcm.size()) / double(c.rate * c.channels);
        else ++bad, std::printf("effect %s: %s\n", e.c_str(), err.c_str());
    }
    std::printf("effects: %d / %zu decode, %.0f s\n", effects, snd.effects().size(), seconds);
    int voices = 0, tried = 0;
    seconds = 0;
    const size_t step = full ? 1 : std::max<size_t>(1, snd.voices().size() / 200);
    for (size_t i = 0; i < snd.voices().size(); i += step) {
        cr3::Cr3Sound::Clip c;
        ++tried;
        if (snd.voice(snd.voices()[i], c, &err)) ++voices, seconds += double(c.pcm.size()) / double(c.rate * c.channels);
        else ++bad, std::printf("voice %s: %s\n", snd.voices()[i].c_str(), err.c_str());
    }
    std::printf("voices: %d / %d tried decode (%s), %.0f s\n", voices, tried, full ? "all" : "a sample", seconds);
    std::printf("tracks: %d / %zu open, %d of %d loops seamless; %d failures\n", opened, snd.tracks().size(), seamless, looped, bad);
    return bad ? 1 : 0;
}

// CR3's events: every area script's event functions (named for their message files:
// s212_d08_01501, c111_s102_t01_00510 ...), each played by the runtime from its start; how many end,
// fail, or are still waiting at the budget - and on which native the waiting ones wait
int events(const char* path, const char* only, int seconds) {
    cr3::Database a;
    if (!openArchive(a, path)) return 1;
    const std::regex eventName("^([a-z][0-9]{3}_)+[a-z][0-9]{2}_[0-9]{5}.*$");
    std::map<std::string, int> outcomes, waitingOn;
    int total = 0;
    std::vector<uint8_t> data;
    // vs.nut repeats many of the areas' events without their helpers; the game loads it only for
    // VStuto_Clear (DBSystem::SM_SkipVSTuto) and runs an area's events from the area's own script
    // (EvtSystem::LoadScript with the area's name): those copies are not entry points
    std::set<std::string> elsewhere;
    for (const auto& e : a.entries()) {
        const std::string k = cr3::Database::key(e.name);
        if (k.compare(0, 7, "script\\") != 0 || k.size() < 5 || k.compare(k.size() - 4, 4, ".nut") != 0 || k == "script\\vs.nut") continue;
        cr3::ScriptVM vm;
        if (a.read(*a.find(e.name), data) && vm.load(data, e.name) && vm.runMain())
            for (const std::string& fn : vm.rootFunctions()) elsewhere.insert(fn);
    }
    for (const auto& e : a.entries()) {
        const std::string k = cr3::Database::key(e.name);
        if (k.compare(0, 7, "script\\") != 0 || k.size() < 5 || k.compare(k.size() - 4, 4, ".nut") != 0) continue;
        const std::string script = k.substr(7, k.size() - 11);
        if (script.compare(0, 7, "battle_") == 0 || script.compare(0, 5, "test_") == 0) continue;
        if (only && script != only) continue;
        cr3::ScriptVM vm;
        if (!a.read(*a.find(e.name), data) || !vm.load(data, e.name) || !vm.runMain()) continue;
        for (const std::string& fn : vm.rootFunctions()) {
            if (!std::regex_match(fn, eventName) || (script == "vs" && elsewhere.count(fn))) continue;
            ++total;
            cr3::GameState state;
            cr3::EventRuntime rt(a, state);
            rt.autoPage = 1.0f;
    rt.askYesNo = true; // (questions, choices and text entry answered as a player confirming would)
            if (!rt.start(script, fn)) {
                ++outcomes["failed to start"];
                continue;
            }
            bool ok = true;
            std::map<std::string, int> before; // (the stubs called by its last second's start)
            while (ok && rt.running() && rt.frame() < 20 * seconds) {
                if (rt.frame() == 20 * seconds - 20) before = rt.stubCalls();
                ok = rt.tick();
            }
            std::string why;
            if (!ok) {
                ++outcomes["failed"];
                std::string err = rt.error();
                const size_t mark = err.find("AN ERROR HAS OCCURED");
                const size_t at = err.find('[', mark == std::string::npos ? 0 : mark);
                why = "error: " + (at != std::string::npos ? err.substr(at, err.find(']', at) - at + 1) : err.substr(0, 60));
                ++waitingOn[why];
            } else if (rt.running()) {
                ++outcomes["still waiting"];
                // (the wait: the stub its last second called most)
                std::string most;
                int n = 0;
                for (const auto& [name, c] : rt.stubCalls()) {
                    const auto b = before.find(name);
                    const int d = c - (b == before.end() ? 0 : b->second);
                    if (d > n) most = name, n = d;
                }
                why = "waits on " + (most.empty() ? std::string("(no stub in its last second)") : most);
                ++waitingOn[why];
            } else {
                ++outcomes["ended"];
            }
            if (!why.empty() && std::getenv("CR3_EVENTS_LIST")) std::printf("%s:%s  %s\n", script.c_str(), fn.c_str(), why.c_str());
        }
    }
    std::printf("%d events:", total);
    for (const auto& [k, n] : outcomes) std::printf(" %s %d", k.c_str(), n);
    std::printf("\n");
    std::vector<std::pair<int, std::string>> top;
    for (const auto& [k, n] : waitingOn) top.emplace_back(n, k);
    std::sort(top.rbegin(), top.rend());
    for (size_t i = 0; i < top.size() && i < 25; ++i) std::printf("  %5d  %s\n", top[i].first, top[i].second.c_str());
    return 0;
}

// The offline database (db/cr3db.hpp): the server's tables the release holds, the requests the
// Vita plays offline, then each DBEmu rule played once from a new game (ids and counts, never
// names): an item sheet's boxes to its whole prize, the shop, the boxes, the presents, gifts,
// quests, tickets and an event's gifts
int db(const char* path, const char* sheet) {
    cr3::Database a;
    if (!openArchive(a, path)) return 1;
    cr3::Cr3OfflineDb d;
    std::string err;
    if (!d.load(a, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    int offline = 0;
    for (int k = 0; k < cr3::kCr3RequestCount; ++k) offline += cr3::cr3Request(k).offline;
    std::printf("%zu server tables, %zu item sheets, %zu quests; %d requests, %d played offline\n", d.tables().size(),
                d.itemSheets().size(), d.quests().size(), cr3::kCr3RequestCount, offline);
    for (const auto& [name, t] : d.tables()) std::printf("  %-22s %zu rows\n", name.c_str(), t.size());
    std::printf("constants: GOLDKEY_MASTER_ID %d, GOLD_MAX %d, GENE_CAPACITY %d; item 101 sells for %d\n", d.constant("GOLDKEY_MASTER_ID"),
                d.constant("GOLD_MAX"), d.constant("GENE_CAPACITY"), d.itemPrice(101));
    cr3::GameState s;
    s.cr3Db = &d;
    d.seed(1);
    auto items = [&] {
        int n = 0;
        for (const auto& [id, c] : s.cr3Items) n += c;
        return n;
    };
    // a login on 2026-10-19 12:00 (GetThisMonthMaster's dated rows), then the next day (each
    // call made before its numbers are printed)
    std::tm day{};
    day.tm_year = 126, day.tm_mon = 9, day.tm_mday = 19, day.tm_hour = 12, day.tm_isdst = -1;
    const std::time_t now = std::mktime(&day);
    int r = d.login(s, now);
    std::printf("login: %d; VIP x%.1f exp, gene boxes on offer", r, d.month().expBonus);
    for (const int b : d.month().geneBoxes) std::printf(" %d", b);
    std::printf("; VS sheets %s / %s; GP %d, presents %zu\n", d.month().vsScoreSheet.c_str(), d.month().vsSeasonSheet.c_str(), s.cr3GP,
                s.cr3Presents.size());
    d.dateChanged(s, now + 60);
    std::printf("same day again: GP %d; ", s.cr3GP);
    d.dateChanged(s, now + 86400);
    std::printf("next day: GP %d, presents %zu; ", s.cr3GP, s.cr3Presents.size());
    r = d.receivePresents(s);
    std::printf("presents taken: %d - Oz coins %d, items %d\n", r, s.cr3Coins, items());
    // an item sheet
    const std::string sh = sheet ? sheet : "d02";
    const int k = d.findItemSheet(sh);
    if (k >= 0) {
        const cr3::Cr3ItemSheet& t = d.itemSheets()[size_t(k)];
        std::printf("sheet %s: %zu entries, its whole prize %s %d\n", t.id.c_str(), t.entries.size(), t.result.gene ? "gene" : "item", t.result.item);
        for (size_t n = 1; n <= t.entries.size() + 2; ++n) {
            r = d.openItemSheet(s, sh, false, -1);
            const cr3::Cr3ItemSheet::Prize* p = d.openedPrize();
            std::printf("  box %2zu: %d -> entry %2d, %s %d; sheet done %d\n", n, r, d.opened().empty() ? -1 : d.opened()[0],
                        p && p->gene ? "gene" : "item", p ? p->item : 0, s.cr3Sheets[t.id].completed);
        }
    }
    // the shop: T01_1's goods 1 (item 101 for 60 gold) x 3, one sold, then 99 more - too dear
    s.cr3Money = 1000;
    const int shop = d.findShop("T01_1");
    const int had101 = s.cr3Items[101];
    r = d.buyGoods(s, shop, 1, 3);
    std::printf("shop T01_1 (%d): buy 3 of goods 1 %d, gold %d, item 101 +%d; ", shop, r, s.cr3Money, s.cr3Items[101] - had101);
    r = d.sellItem(s, 101, 1);
    std::printf("sell 1 %d, gold %d; ", r, s.cr3Money);
    r = d.buyGoods(s, shop, 1, 99);
    std::printf("99 more %d\n", r);
    r = d.getDailyItem(s, 0);
    const int again = d.getDailyItem(s, 0);
    s.cr3Coins += 10, s.cr3GP += 300;
    const int stock = d.buyGeneStock(s);
    std::printf("daily item 0: %d, again %d; gene stock: %d (room %d)\n", r, again, stock, s.cr3GeneStock);
    // the boxes
    r = d.openGPBox(s, 1, 200);
    std::printf("GP box (200 GP) %d: sheet %s, %zu won; ", r, d.openedSheet().c_str(), d.opened().size());
    r = d.openLuckyBox(s, "d03", 1, 1);
    std::printf("lucky box d03 (1 coin) %d: %zu won; ", r, d.opened().size());
    const size_t g0 = s.cr3Genes.size();
    r = d.openGeneBox(s, 1, 3, 2);
    std::printf("gene box 1 x 3 (2 coins) %d: %zu genes (", r, s.cr3Genes.size() - g0);
    for (const cr3::Cr3Gene& g : d.lastGenes()) std::printf(" %d/%d", g.master, g.exp);
    r = d.openPrizeBox(s, "ocean");
    std::printf(" ); ocean %d: item %d x%d, flag 4026 %d\n", r, d.prizeBox().item, d.prizeBox().count, s.flag(0xfba));
    // presents, gifts, tickets, an event's gifts
    size_t genes = s.cr3Genes.size();
    r = d.earnPresent(s, 1);
    const int taken = d.receivePresent(s, int(s.cr3Presents.size()) - 1);
    std::printf("present 1 earned %d, taken %d - genes +%zu; ", r, taken, s.cr3Genes.size() - genes);
    s.cr3Items[600001] = 1;
    r = d.itemizeGift(s, 600001);
    std::printf("itemize gift 600001 %d (item 131 x%d, the gift x%d); ", r, s.cr3Items[131], s.cr3Items[600001]);
    genes = s.cr3Genes.size();
    r = d.mergeGift(s, 1, {});
    std::printf("merge gift 1 %d (genes +%zu)\n", r, s.cr3Genes.size() - genes);
    d.gotTicket(s);
    const bool had = d.ticketReceived();
    r = d.receiveTicket(s);
    const int twice = d.receiveTicket(s);
    std::printf("ticket: had %d, receive %d, again %d (item 118 x%d); ", had, r, twice, s.cr3Items[118]);
    r = d.sync2(s, "s272_t01_00401_gift_32_50");
    std::printf("Sync2 s272_t01_00401_gift_32_50 %d (600032 x%d, 600050 x%d), ", r, s.cr3Items[600032], s.cr3Items[600050]);
    r = d.sync2(s, "nothing");
    std::printf("unknown %d\n", r);
    // quests: flag 2095 set (the first quests' condition), which hold; then one begun, its prize,
    // ended
    s.setFlag(2095, true);
    r = d.findQuests(s, now);
    std::printf("quests found %d: %zu standing, %zu offered", r, d.standingQuests().size(), d.foundQuests().size());
    if (!d.foundQuests().empty() || !d.standingQuests().empty()) {
        const int q = d.quests()[size_t(d.foundQuests().empty() ? d.standingQuests()[0] : d.foundQuests()[0])].id;
        const int gold = s.cr3Money;
        const int begun = d.beginQuest(s, q), prize = d.earnQuestRewards(s, q);
        const int gained = s.cr3Money - gold, again2 = d.earnQuestRewards(s, q), ended = d.endQuest(s, q);
        std::printf("; quest %d begun %d, prize %d (gold +%d), again %d, ended %d", q, begun, prize, gained, again2, ended);
    }
    // genes: one at level 10 (its curve's EXP, its skills), EXP added, merged, enhanced, a seed,
    // sold
    std::printf("\n");
    auto show = [&](const char* what, const cr3::Cr3Gene& g) {
        std::printf("  %s: gene %d lv %d (EXP %d), slots", what, g.master, d.geneLevel(g), g.exp);
        for (const auto& sl : g.slots)
            if (sl[1] != -1) std::printf(" %d:%d", sl[0], sl[1]);
        std::printf("; adds %d %d %d %d\n", g.adds[0], g.adds[1], g.adds[2], g.adds[3]);
    };
    s.cr3Genes.clear();
    for (const int id : {2, 70, 92}) {
        cr3::Cr3Gene g(id);
        g.exp = d.geneExp(id, 10);
        d.initGene(g);
        s.cr3Genes.push_back(g);
    }
    show("gene 2 at 10", s.cr3Genes[0]);
    d.addGeneLevels(s.cr3Genes[0], 25);
    show("+25 levels", s.cr3Genes[0]);
    d.addGeneExp(s.cr3Genes[0], 99999999);
    show("EXP past its top", s.cr3Genes[0]);
    r = d.previewMergeGene(s, 0);
    std::printf("  merge previews %d: %zu", r, d.mergePreviews().size());
    for (const auto& p : d.mergePreviews()) std::printf(" (with %d -> gene %d, %d gold)", s.cr3Genes[size_t(p.partner)].master, p.result.master, p.gold);
    std::printf("\n");
    if (!d.mergePreviews().empty()) show("merge 0 makes", d.mergePreviews()[0].result);
    s.cr3Money = 100000;
    r = d.mergeGene(s, 0, false);
    std::printf("  merged %d: genes %zu, gold %d\n", r, s.cr3Genes.size(), s.cr3Money);
    for (const int id : {1, 1, 4}) s.cr3Genes.push_back(cr3::Cr3Gene(id, d.geneExp(id, 5)));
    const int target = int(s.cr3Genes.size()) - 3;
    show("enhance target", s.cr3Genes[size_t(target)]);
    r = d.enhanceGene(s, target, {target + 1, target + 2});
    std::printf("  enhanced %d: gold %d, genes %zu\n", r, s.cr3Money, s.cr3Genes.size());
    show("after", s.cr3Genes[size_t(target)]);
    s.cr3Items[d.constant("POWER_SEED2_MASTER_ID")] = 3;
    r = d.enforceGene(s, target, 1, d.constant("POWER_SEED2_MASTER_ID"), 3);
    std::printf("  power seed 2 x 3: %d; ", r);
    r = d.enforceGene(s, target, 1, d.constant("POWER_SEED2_MASTER_ID"), 9);
    std::printf("x 9 more %d (capped at GENE_ADD_MAX %d)\n", r, d.constant("GENE_ADD_MAX"));
    show("seeded", s.cr3Genes[size_t(target)]);
    const int gold0 = s.cr3Money;
    r = d.sellGenes(s, {target});
    std::printf("  sold %d: +%d gold, genes %zu", r, s.cr3Money - gold0, s.cr3Genes.size());
    std::printf("\nfinally: %d items (%zu kinds), %zu genes, %zu presents, gold %d, coins %d (spent %d), GP %d (spent %d)\n", items(),
                s.cr3Items.size(), s.cr3Genes.size(), s.cr3Presents.size(), s.cr3Money, s.cr3Coins, s.cr3CoinsSpent, s.cr3GP, s.cr3GPSpent);
    return 0;
}

int usage() {
    std::fprintf(stderr,
                 "cr3tool [--patch <fan translation>] <command> ...\n"
                 "cr3tool release <release>\n"
                 "cr3tool texts <release>\n"
                 "cr3tool events <release> [script | all] [seconds]\n"
                 "cr3tool check <archive>\n"
                 "cr3tool scripts <archive> [name]\n"
                 "cr3tool run <archive> <script> [function]\n"
                 "cr3tool images <archive> [pattern [out dir]]\n"
                 "cr3tool models <archive>\n"
                 "cr3tool compare-models <archive> <archive>\n"
                 "cr3tool animations <archive>\n"
                 "cr3tool shaders <archive>\n"
                 "cr3tool walk <archive> <executable> [map]\n"
                 "cr3tool exits <archive> <executable>\n"
                 "cr3tool fieldscript <archive> <executable> [map]\n"
                 "cr3tool event <archive> <script> [function]\n"
                 "cr3tool play <archive> <event | all>\n"
                 "cr3tool battle <archive> <coupling | all>\n"
                 "cr3tool fight <archive> <coupling> [solo | pair] [seed] [a,b]\n"
                 "cr3tool sound <release> [--full]\n"
                 "cr3tool glass <seeds>\n"
                 "cr3tool save-import <game.dat | cont.dat> [system.dat]  (a CR3-U save on stdout)\n"
                 "cr3tool save-export <archive> <CR3-U save> <game.dat>\n"
                 "cr3tool db <release> [item sheet]  (the offline database: tables, requests, a sheet's boxes)\n");
    return 2;
}

// --patch <folder>: a fan translation over the release, active (Database::addOverlay)
const char* gPatch = nullptr;

bool openArchive(cr3::MvglArchive& a, const char* path) {
    std::string err;
    if (!a.open(path, &err)) { // (a release's folder, one of its archives, an .apk / .apks)
        std::fprintf(stderr, "%s\n", err.c_str());
        return false;
    }
    if (gPatch) {
        if (!a.addOverlay(gPatch, &err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return false;
        }
        a.setOverlayActive(true);
    }
    return true;
}

// what a release holds: its kind, archives, files by extension, loose files, an overlay's
int release(const char* path) {
    cr3::Database a;
    if (!openArchive(a, path)) return 1;
    std::map<std::string, int> byExt;
    int loose = 0;
    for (const auto& e : a.entries()) {
        const size_t dot = e.name.rfind('.');
        ++byExt[dot == std::string::npos ? std::string("-") : e.name.substr(dot + 1)];
        if (e.file < 0) ++loose;
    }
    std::printf("%s release at %s: %zu archives, %zu files (%d loose)\n", a.kindName(), a.root().c_str(), a.archiveCount(),
                a.entries().size(), loose);
    for (const auto& [ext, n] : byExt) std::printf("  %-5s %6d\n", ext.c_str(), n);
    if (a.hasOverlay()) {
        int over = 0;
        for (const auto& e : a.entries())
            if (a.find(e.name)->overlay) ++over;
        std::printf("overlay %s: replaces %d of them\n", a.overlayPath().c_str(), over);
    }
    std::printf("languages:");
    for (const cr3::LanguageChoice& l : cr3::releaseLanguages(a)) std::printf(" [%s: slot %d%s]", l.label, l.slot, l.overlay ? ", overlay" : "");
    std::printf("\n");
    return 0;
}

// every message file in each of the release's languages: parses, how many messages have text,
// how much of it is ASCII (no text shown)
int texts(const char* path) {
    cr3::Database a;
    if (!openArchive(a, path)) return 1;
    int bad = 0;
    for (const cr3::LanguageChoice& l : cr3::releaseLanguages(a)) {
        cr3::applyLanguage(a, l.id);
        size_t files = 0, broken = 0, messages = 0, withText = 0, bytes = 0, ascii = 0, fromOverlay = 0;
        std::vector<uint8_t> d;
        for (const auto& base : a.entries()) {
            if (cr3::Database::key(base.name).compare(0, 8, "message\\") != 0) continue;
            const auto& e = *a.find(base.name);
            cr3::TextData t;
            ++files, fromOverlay += e.overlay;
            if (!a.read(e, d) || !t.parse(d)) {
                ++broken;
                continue;
            }
            for (int n = 1; n <= t.count(); ++n) {
                const std::string m = t.message(n, l.slot);
                ++messages, withText += !m.empty(), bytes += m.size();
                for (unsigned char c : m) ascii += c < 0x80;
            }
        }
        std::printf("%-26s %zu files (%zu the overlay's), %zu broken; %zu messages, %zu with text; %.1f%% ASCII\n", l.label, files,
                    fromOverlay, broken, messages, withText, bytes ? 100.0 * double(ascii) / double(bytes) : 0.0);
        bad += int(broken);
    }
    return bad ? 1 : 0;
}

int check(const char* path) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    size_t ok = 0, bad = 0;
    std::vector<uint8_t> data;
    for (const auto& base : a.entries()) {
        const auto& e = *a.find(base.name);
        std::string err;
        if (a.read(e, data, &err) && data.size() == e.size) ++ok;
        else ++bad, std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
    }
    std::printf("%s: %zu files decode, %zu bad\n", path, ok, bad);
    return bad ? 1 : 0;
}

int scripts(const char* path, const char* only) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    size_t loaded = 0, ran = 0, total = 0;
    std::map<std::string, int> stubs;
    std::vector<uint8_t> data;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 4 || e.name.compare(e.name.size() - 4, 4, ".nut") != 0) continue;
        if (only && e.name.find(only) == std::string::npos) continue;
        ++total;
        if (!a.read(e, data)) {
            std::printf("%s: unreadable\n", e.name.c_str());
            continue;
        }
        cr3::ScriptVM vm; // (one VM per script: each defines the same helper library)
        if (!vm.load(data, e.name)) {
            std::printf("%s: %s\n", e.name.c_str(), vm.error().c_str());
            continue;
        }
        ++loaded;
        if (!vm.runMain()) {
            std::printf("%s: main failed: %s\n", e.name.c_str(), vm.error().c_str());
            continue;
        }
        ++ran;
        for (const auto& [k, n] : vm.stubCalls()) stubs[k] += n;
    }
    std::printf("%zu scripts: %zu load, %zu run main\n", total, loaded, ran);
    for (const auto& [k, n] : stubs) std::printf("  stub called in main: %s x%d\n", k.c_str(), n);
    return loaded == total && ran == total ? 0 : 1;
}

int run(const char* path, const char* script, const char* function) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::string name = script;
    if (name.find('.') == std::string::npos) name += ".nut";
    if (name.find('\\') == std::string::npos) name = "script\\" + name;
    std::vector<uint8_t> data;
    std::string err;
    if (!a.read(name, data, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cr3::ScriptVM vm;
    if (!vm.load(data, name) || !vm.runMain()) return std::fprintf(stderr, "%s\n", vm.error().c_str()), 1;
    std::string fn = function ? function : name.substr(name.rfind('\\') + 1, name.rfind('.') - name.rfind('\\') - 1);
    if (const std::string f = vm.findFunction(fn); !f.empty()) fn = f;
    const bool ok = vm.call(fn);
    std::printf("%s.%s: %s\n", name.c_str(), fn.c_str(), ok ? "returned" : ("failed: " + vm.error()).c_str());
    for (const auto& [k, n] : vm.stubCalls()) std::printf("  %s x%d\n", k.c_str(), n);
    return ok ? 0 : 1;
}

// An event's timeline: the script run frame by frame (WaitFrame waits), its native calls in order
// with their arguments - the messages taken as read at once, no button touched, sounds done
int traceEvent(const char* path, const char* script, const char* function, int maxFrames) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::string name = script;
    if (name.find('.') == std::string::npos) name += ".nut";
    if (name.find('\\') == std::string::npos) name = "script\\" + name;
    std::vector<uint8_t> data;
    std::string err;
    if (!a.read(name, data, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cr3::ScriptVM vm;
    if (!vm.load(data, name) || !vm.runMain()) return std::fprintf(stderr, "%s\n", vm.error().c_str()), 1;
    std::string fn = function ? function : name.substr(name.rfind('\\') + 1, name.rfind('.') - name.rfind('\\') - 1);
    if (const std::string f = vm.findFunction(fn); !f.empty()) fn = f;
    int frame = 0, wait = 0;
    std::string last;
    int repeats = 0;
    vm.setNative("", "WaitFrame", [&](cr3::NativeCall& c) {
        wait = std::max(1, c.integer(0, 1));
        c.suspend();
    });
    vm.setStubHook([&](const std::string& n, cr3::NativeCall& c) {
        std::string line = n + "(";
        for (int i = 0; i < c.count(); ++i) line += (i ? ", " : "") + c.text(i);
        line += ")";
        if (line == last) {
            ++repeats;
        } else {
            if (repeats) std::printf("         (x%d more)\n", repeats);
            std::printf("%6d  %s\n", frame, line.c_str());
            last = line;
            repeats = 0;
        }
        // the waits' conditions: met at once
        if (n == "EvtWindow.CheckMessage" || n == "EvtFade.Check") c.result(true);
        else if (n == "Cr3Sound.GetState" || n == "EvtSkip.CheckButtonTouch" || n == "Cr3Touch.GetTouchState") c.result(0);
        else if (n == "EvtWindow.CheckYesNoButton") c.result(1);
        else if (n == "EvtIntroduction.GetVisible") c.result(false);
        else if (n == "VistaEvent.CheckExecution") c.result(false);
    });
    bool ok = vm.call(fn);
    while (ok && vm.waiting() && frame < maxFrames) {
        frame += wait;
        wait = 0;
        ok = vm.resume();
    }
    if (repeats) std::printf("         (x%d more)\n", repeats);
    std::printf("%s.%s: %s at frame %d (%.1f s at 20 frames a second)\n", name.c_str(), fn.c_str(),
                !ok ? ("failed: " + vm.error()).c_str() : vm.waiting() ? "still running" : "finished", frame, frame / 20.0);
    return ok ? 0 : 1;
}

// the release's offline database (db/cr3db.hpp), loaded once; null where it has none
cr3::Cr3OfflineDb* offlineDb(const cr3::MvglArchive& a) {
    static std::map<const cr3::MvglArchive*, std::unique_ptr<cr3::Cr3OfflineDb>> dbs;
    auto& db = dbs[&a];
    if (!db) {
        db = std::make_unique<cr3::Cr3OfflineDb>();
        std::string err;
        if (!db->load(a, &err)) std::fprintf(stderr, "database: %s\n", err.c_str());
    }
    return db->loaded() ? db.get() : nullptr;
}

// An event to its end through the runtime: what it shows, per change (a message's number and
// pages, never its text); `quiet` only the outcome. 0: it ended
int playEvent(const cr3::MvglArchive& a, const std::string& event, bool quiet, std::string* outcome = nullptr,
              std::map<std::string, int>* stubsOut = nullptr) {
    cr3::GameState state;
    state.cr3Db = offlineDb(a);
    cr3::EventRuntime rt(a, state);
    rt.autoPage = 1.0f;
    rt.askYesNo = true; // (questions, choices and text entry answered as a player confirming would)
    // CR3 keeps an area's events in one script: "<script>:<function>" (t01:s212_d08_01501)
    const size_t colon = event.find(':');
    if (colon != std::string::npos ? !rt.start(event.substr(0, colon), event.substr(colon + 1)) : !rt.start(event)) {
        if (outcome) *outcome = "failed: " + rt.error();
        if (!quiet) std::printf("%s: %s\n", event.c_str(), rt.error().c_str());
        return 1;
    }
    int lastMessage = -1, lastColour = -1, lastVisible = -1;
    float lastTarget = -1;
    bool ok = true;
    const int maxFrames = 20 * 60 * 20;
    while (ok && rt.running() && rt.frame() < maxFrames) {
        ok = rt.tick();
        if (quiet) continue;
        const double t = rt.frame() * cr3::EventRuntime::kTick;
        const int msg = rt.messageShown() ? 1 : 0;
        if (msg != lastMessage && msg)
            std::printf("%6d %6.2f s  message: %zu pages, speaker %d\n", rt.frame(), t, rt.message().pages.size(), rt.message().speaker);
        lastMessage = msg;
        if (rt.fade().colour != lastColour || rt.fade().target != lastTarget)
            std::printf("%6d %6.2f s  fade: colour %d to %.2f\n", rt.frame(), t, rt.fade().colour, rt.fade().target);
        lastColour = rt.fade().colour, lastTarget = rt.fade().target;
        int visible = 0;
        for (const cr3::EventModel& m : rt.scene().models()) visible += m.visible;
        if (visible != lastVisible) std::printf("%6d %6.2f s  models shown: %d of %zu\n", rt.frame(), t, visible, rt.scene().models().size());
        lastVisible = visible;
        if (rt.frame() % 20 == 0) {
            cr3::Mat34 w;
            float fov, zn, zf;
            if (rt.scene().camera(w, fov, zn, zf))
                std::printf("%6d %6.2f s  camera (%.3f %.3f %.3f) fov %.1f, near %.3f far %.1f\n", rt.frame(), t, w.m[3], w.m[7], w.m[11], fov, zn, zf);
            if (std::getenv("CR3_PLAY_MODELS"))
                std::printf("               camera looks along (%.3f %.3f %.3f), up (%.3f %.3f %.3f)\n", -w.m[2], -w.m[6], -w.m[10], w.m[1], w.m[5], w.m[9]);
            if (std::getenv("CR3_PLAY_MODELS"))
                for (const cr3::EventModel& m : rt.scene().models())
                    if (m.visible)
                        std::printf("               %-8s at (%.3f %.3f %.3f) scale %.3f, %s %.2f s\n", m.name.c_str(), m.place.m[3], m.place.m[7], m.place.m[11],
                                    std::sqrt(m.place.m[0] * m.place.m[0] + m.place.m[4] * m.place.m[4] + m.place.m[8] * m.place.m[8]),
                                    m.animation.empty() ? m.figure.c_str() : m.animation.c_str(), m.animTime);
        }
    }
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s at frame %d (%.1f s)%s", !ok ? ("failed: " + rt.error()).c_str() : rt.running() ? "still running" : "ended",
                  rt.frame(), rt.frame() * cr3::EventRuntime::kTick, rt.battle() >= 0 ? (", battle " + std::to_string(rt.battle())).c_str() : "");
    if (outcome) *outcome = buf;
    if (stubsOut)
        for (const auto& [n, k] : rt.stubCalls()) (*stubsOut)[n] += k;
    if (!quiet) {
        std::printf("%s: %s\n", event.c_str(), buf);
        std::string stubs;
        for (const auto& [n, k] : rt.stubCalls()) stubs += " " + n + " " + std::to_string(k);
        std::printf("stubs:%s\n", stubs.c_str());
    }
    return ok && !rt.running() ? 0 : 1;
}

// CR3's battle scripts (script\battle_NNNN.nut, BtlScriptSystem): each one's Battle_Boot_Base,
// Battle_Start_Base and Battle_Victory_Base played to their ends (pages turning by themselves)
int battleScripts(const char* path) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::map<std::string, int> stubs;
    int n = 0, ended = 0;
    for (const auto& e : a.entries()) {
        const std::string& f = e.name;
        if (f.compare(0, 14, "script\\battle_") != 0 || f.size() < 18 || f.compare(f.size() - 4, 4, ".nut") != 0) continue;
        const std::string script = f.substr(7, f.size() - 11);
        for (const char* fn : {"Battle_Boot_Base", "Battle_Start_Base", "Battle_Victory_Base"}) {
            std::string outcome;
            ++n;
            const bool ok = playEvent(a, script + ":" + fn, true, &outcome, &stubs) == 0;
            ended += ok;
            if (!ok) std::printf("%s:%s: %s\n", script.c_str(), fn, outcome.c_str());
        }
    }
    std::printf("%d battle script calls: %d ended\n", n, ended);
    return ended == n ? 0 : 1;
}

int play(const char* path, const char* event) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    if (std::strcmp(event, "all") != 0) return playEvent(a, event, false);
    // every event script: script\<name>.nut whose name ends in _evt (its function the same, but for case)
    std::map<std::string, int> outcomes, stubs;
    int n = 0, ended = 0;
    for (const auto& e : a.entries()) {
        const std::string& f = e.name;
        if (f.compare(0, 7, "script\\") != 0 || f.size() < 12 || f.compare(f.size() - 8, 8, "_evt.nut") != 0) continue;
        std::string outcome;
        ++n;
        ended += playEvent(a, f.substr(7, f.size() - 11), true, &outcome, &stubs) == 0;
        const std::string kind = outcome.substr(0, outcome.find(" at frame"));
        if (kind != "ended") std::printf("%s: %s\n", f.c_str(), outcome.c_str());
        ++outcomes[kind.substr(0, 40)];
    }
    std::printf("%d events: %d ended\n", n, ended);
    for (const auto& [k, c] : outcomes) std::printf("  %s: %d\n", k.c_str(), c);
    std::printf("  stub natives reached (calls):");
    for (const auto& [k, c] : stubs) std::printf(" %s %d", k.c_str(), c);
    std::printf("\n");
    return ended == n ? 0 : 1;
}

bool writeTga(const std::string& path, const cr3::Image& img) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint8_t h[18] = {};
    h[2] = 2; // uncompressed true-colour
    h[12] = uint8_t(img.width), h[13] = uint8_t(img.width >> 8), h[14] = uint8_t(img.height), h[15] = uint8_t(img.height >> 8);
    h[16] = 32;
    h[17] = uint8_t(8 | (img.bottomUp ? 0 : 0x20)); // 8 alpha bits; origin top-left unless stored bottom-up
    std::fwrite(h, 1, sizeof h, f);
    std::vector<uint8_t> row(size_t(img.width) * 4);
    for (int y = 0; y < img.height; ++y) {
        const uint8_t* s = &img.rgba[size_t(y) * img.width * 4];
        for (int x = 0; x < img.width; ++x) row[x * 4] = s[x * 4 + 2], row[x * 4 + 1] = s[x * 4 + 1], row[x * 4 + 2] = s[x * 4], row[x * 4 + 3] = s[x * 4 + 3];
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

int images(const char* path, const char* pattern, const char* outDir) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::map<std::string, int> formats;
    size_t ok = 0, bad = 0, written = 0;
    std::vector<uint8_t> data;
    for (const auto& base : a.entries()) {
        const auto& e = *a.find(base.name); // (an overlay's where it has one)
        if (e.name.size() < 4 || e.name.compare(e.name.size() - 4, 4, ".img") != 0) continue;
        if (pattern && e.name.find(pattern) == std::string::npos) continue;
        cr3::Image img;
        std::string err;
        if (!a.read(e, data, &err) || !cr3::decodeImg(data, img, &err)) {
            ++bad;
            std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
            continue;
        }
        ++ok;
        ++formats[img.format + (img.bottomUp ? " (bottom-up)" : "")];
        if (outDir) {
            std::string n = e.name.substr(e.name.rfind('\\') + 1);
            n = n.substr(0, n.size() - 4);
            written += writeTga(std::string(outDir) + "/" + n + ".tga", img);
        }
    }
    std::printf("%zu textures decode, %zu bad%s\n", ok, bad, outDir ? (", " + std::to_string(written) + " written").c_str() : "");
    for (const auto& [k, n] : formats) std::printf("  %-32s %d\n", k.c_str(), n);
    return bad ? 1 : 0;
}

int models(const char* path) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    size_t ok = 0, bad = 0, meshes = 0, vertices = 0, triangles = 0, textures = 0, missing = 0;
    std::map<std::string, int> missingNames;
    std::vector<uint8_t> data;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".geom") != 0) continue;
        cr3::Geom g;
        std::string err;
        if (!a.read(e, data, &err) || !cr3::parseGeom(data, g, &err)) {
            ++bad;
            std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
            continue;
        }
        ++ok;
        for (const auto& m : g.meshes) meshes++, vertices += m.vertexCount, triangles += m.triangles().size() / 3;
        for (const auto& mat : g.materials)
            for (const auto& [id, name] : mat.textures) {
                ++textures;
                if (!a.contains("images\\" + name + ".img")) ++missing, ++missingNames[name];
            }
    }
    std::printf("%zu mesh files parse, %zu bad: %zu meshes, %zu vertices, %zu triangles; %zu texture references, %zu not in the archive\n",
                ok, bad, meshes, vertices, triangles, textures, missing);
    int shown = 0;
    for (const auto& [n, k] : missingNames)
        if (shown++ < 12) std::printf("  missing %s x%d\n", n.c_str(), k);
    return bad ? 1 : 0;
}

// The shaders' GLSL against the key decoder: per SHADE_* bit (and feature left out), what the
// source says (featuresFromGlsl) and what the key says (decodeShaderKey), for every key the
// materials name
int shaders(const char* path) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    static const char* kShade[] = {"on", "texture", "normal map", "lit", "DirLamp01", "hemisphere", "CLUT", "specular",
                                   "spec vertex alpha", "obscure", "obscure spec", "tint", "tint alpha", "velvet", "fuzzy",
                                   "alpha test", "fog", "light map", "scroll", "offset", "spec map alpha", "uv1",
                                   "scroll2", "offset2", "distortion", "overlay", "point light"};
    static const char* kUnsupported[] = {"billboard", "upright"}; // (the VertexPlacing bits)
    constexpr int kShadeBits = int(sizeof kShade / sizeof kShade[0]), kUnsupportedBits = 2;
    std::map<std::array<uint32_t, 4>, int> keys;
    size_t materials = 0, builtIn = 0, alphaNoRef = 0, extraClash = 0;
    std::vector<uint8_t> data;
    cr3::ShaderLibrary library(a);
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".geom") != 0) continue;
        cr3::Geom g;
        if (!a.read(e, data) || !cr3::parseGeom(data, g)) continue;
        for (const auto& m : g.materials) {
            ++materials;
            const cr3::ShaderFeatures f = library.features(m.shaderKey);
            if (!f.shade) {
                ++builtIn;
                continue;
            }
            ++keys[m.shaderKey];
            uint32_t ref;
            if ((f.shade & SHADE_ALPHA_TEST) && !m.state(0x78, 0, ref)) ++alphaNoRef;
            if ((f.shade & SHADE_CLUT) && (f.shade & SHADE_LIGHT_MAP)) ++extraClash;
        }
    }
    size_t checked = 0, noSource = 0, wrongKeys = 0;
    size_t have[kShadeBits + kUnsupportedBits] = {}, wrong[kShadeBits + kUnsupportedBits] = {};
    for (const auto& [key, uses] : keys) {
        char name[64];
        std::snprintf(name, sizeof name, "shaders\\%08x_%08x_%08x_%08x", key[0], key[1], key[2], key[3]);
        std::vector<uint8_t> fpData, vpData;
        const bool read = a.read(std::string(name) + "_fp.shad", fpData) && a.read(std::string(name) + "_vp.shad", vpData);
        const std::string fp(fpData.begin(), fpData.end()), vp(vpData.begin(), vpData.end());
        if (!read || fp.find("gl_FragColor") == std::string::npos) { // (the Vita's: compiled programs)
            ++noSource;
            continue;
        }
        ++checked;
        const cr3::ShaderFeatures src = cr3::featuresFromGlsl(vp, fp), dec = cr3::decodeShaderKey(key);
        bool bad = false;
        for (int k = 0; k < kShadeBits + kUnsupportedBits; ++k) {
            auto bit = [&](const cr3::ShaderFeatures& f) {
                const uint32_t other = f.vertex;
                return k < kShadeBits ? (f.shade >> k & 1) : (other >> (k - kShadeBits) & 1);
            };
            const bool s = bit(src), d = bit(dec);
            have[k] += s;
            if (s != d) ++wrong[k], bad = true;
        }
        wrongKeys += bad;
    }
    std::printf("%zu materials: %zu with built-in Figure shaders (key 0), the rest %zu keys; %zu keys' GLSL read, "
                "%zu without (the Vita's are compiled: their keys decoded)\n",
                materials, builtIn, keys.size(), checked, noSource);
    std::printf("  the GLSL has:");
    for (int k = 1; k < kShadeBits + kUnsupportedBits; ++k)
        std::printf("%s %s %zu", k > 1 ? "," : "", k < kShadeBits ? kShade[k] : kUnsupported[k - kShadeBits], have[k]);
    std::printf("\n  the key decoder (Android / Vita layout) disagrees with the GLSL on %zu keys", wrongKeys);
    for (int k = 0; k < kShadeBits + kUnsupportedBits; ++k)
        if (wrong[k]) std::printf(" - %s %zu", k < kShadeBits ? kShade[k] : kUnsupported[k - kShadeBits], wrong[k]);
    std::printf("\n  alpha-tested materials without an alpha_func state (AlphaRef 0.5): %zu; CLUT + light map: %zu\n",
                alphaNoRef, extraClash);
    return extraClash ? 1 : 0;
}

// the maps' table: from `binary` ("-": the release's own, found beside the archive - the iPad's
// executable, the Vita's eboot.bin, the Android release's APK)
bool readTable(const char* path, const char* binary, std::vector<cr3::MapInfo>& table) {
    if (cr3::releaseMapTable(path, std::strcmp(binary, "-") ? binary : "", table)) return true;
    std::fprintf(stderr, "%s: no maps' table\n", binary);
    return false;
}

int fieldScripts(const char* path, const char* binary, const char* only) {
    cr3::MvglArchive a;
    std::vector<cr3::MapInfo> table;
    if (!openArchive(a, path) || !readTable(path, binary, table)) return 1;
    size_t maps = 0, ran = 0, npcs = 0, npcsShown = 0;
    size_t events = 0, reserved = 0, eventBoxes = 0, eventFunctions = 0, faded = 0;
    size_t taps = 0, tapFunctions = 0, tapsEnded = 0, tapsTalking = 0;
    std::map<std::string, int> stubs;
    for (const cr3::MapInfo& info : table) {
        if (only && info.name != only) continue;
        cr3::FieldMap f;
        if (!f.load(a, info)) continue;
        ++maps;
        cr3::GameState state; // (a new game's: every flag clear)
        state.cr3Db = offlineDb(a);
        cr3::FieldScript script(a, state);
        // (to its end: an area's set-up may play an event, its pages turning by themselves)
        bool ok = script.enter(info.name, f.gimmicks());
        const bool waited = ok && script.running();
        if (ok) ok = script.runToEnd();
        if (!ok && script.error().empty()) {
            std::printf("  %s: still running after 10 minutes of the game's updates\n", info.name.c_str());
        }
        ran += ok;
        events += waited;
        if (ok && script.events().fade().alpha > 0.01f) { // (a fade the set-up leaves on the screen)
            ++faded;
            if (only) std::printf("  fade left: colour %d at %.0f%%\n", script.events().fade().colour, script.events().fade().alpha * 100);
        }
        if (!state.next.vistaFunction.empty() || state.next.battle >= 0 || !state.next.popPoint.empty()) {
            ++reserved;
            if (only) std::printf("  next: vista %s (%s), battle %d, pop point %s\n", state.next.vistaFunction.c_str(),
                                  state.next.vistaMap.c_str(), state.next.battle, state.next.popPoint.c_str());
        }
        for (const cr3::Gimmick& g : f.gimmicks()) // (the event boxes' functions: in the script?)
            if (g.kind == cr3::Gimmick::Event) {
                ++eventBoxes;
                if (script.hasFunction(g.function)) ++eventFunctions;
                else if (only) std::printf("  event box %s: no function %s\n", g.name.c_str(), g.function.c_str());
            }
        // what the leader could tap there now (a _check gimmick shown, its event on): its function
        // called on the area as its entry left it, played to its end
        for (const cr3::Gimmick& g : f.gimmicks()) {
            if (!ok || g.checkLength() <= 0 || !script.visible(g)) continue;
            const auto st = script.gimmicks().find(g.id);
            if (st != script.gimmicks().end() && !st->second.event) continue;
            ++taps;
            if (!script.hasFunction(g.function)) {
                if (only) std::printf("  %s: no function %s\n", g.name.c_str(), g.function.c_str());
                continue;
            }
            ++tapFunctions;
            cr3::GameState tapState = state;
            cr3::FieldScript tapScript(a, tapState);
            bool tapped = tapScript.enter(info.name, f.gimmicks()) && tapScript.runToEnd();
            const int before = tapScript.events().messagesSet();
            tapped = tapped && tapScript.call(g.function) && tapScript.runToEnd();
            tapsEnded += tapped;
            const int said = tapScript.events().messagesSet() - before;
            tapsTalking += said > 0;
            if (only || !tapped)
                std::printf("  %s: tapped %s (at %.3f %.3f %.3f) - %s: %d messages, %s\n", info.name.c_str(), g.name.c_str(), g.position[0],
                            g.position[1], g.position[2], g.function.c_str(), said, tapped ? "ran to its end" : tapScript.error().empty() ? "still running" : tapScript.error().c_str());
            for (const auto& [k, n] : tapScript.stubCalls()) stubs[k] += n;
        }
        std::string shown;
        for (const cr3::Gimmick& g : f.gimmicks()) {
            if (g.kind != cr3::Gimmick::Npc) continue;
            ++npcs;
            if (script.visible(g)) ++npcsShown, shown += " " + g.id;
        }
        if (!ok || only) std::printf("  %s: %s%s%s\n", info.name.c_str(), ok ? "ran" : script.error().c_str(), shown.empty() ? "" : "; NPCs shown:",
                                    shown.c_str());
        for (const auto& [k, n] : script.stubCalls()) stubs[k] += n;
    }
    std::printf("%zu event boxes, %zu with their function in the dungeon's script\n", eventBoxes, eventFunctions);
    std::printf("%zu things to tap (a new game's), %zu with their function, %zu ran to their end, %zu said something\n", taps, tapFunctions,
                tapsEnded, tapsTalking);
    std::printf("%zu maps: evt000 ran to its end on %zu (%zu waiting on the way, %zu reserving what comes next, %zu leaving a fade); NPCs %zu, shown %zu (a new game's flags)\n",
                maps, ran, events, reserved, faded, npcs, npcsShown);
    std::vector<std::pair<int, std::string>> top;
    for (const auto& [k, n] : stubs) top.push_back({n, k});
    std::sort(top.rbegin(), top.rend());
    std::printf("  stub natives reached (calls):");
    for (size_t k = 0; k < top.size() && k < 30; ++k) std::printf(" %s %d", top[k].second.c_str(), top[k].first);
    std::printf("\n");
    return 0;
}

int exits(const char* path, const char* binary) {
    cr3::MvglArchive a;
    std::vector<cr3::MapInfo> table;
    if (!openArchive(a, path) || !readTable(path, binary, table)) return 1;
    std::map<std::string, cr3::FieldMap> loaded;
    auto load = [&](const std::string& name) -> cr3::FieldMap* {
        if (auto it = loaded.find(name); it != loaded.end()) return &it->second;
        const auto row = std::find_if(table.begin(), table.end(), [&](const cr3::MapInfo& m) { return m.name == name; });
        if (row == table.end()) return nullptr;
        cr3::FieldMap& f = loaded[name];
        if (!f.load(a, *row)) {
            loaded.erase(name);
            return nullptr;
        }
        return &f;
    };
    size_t maps = 0, total = 0, good = 0;
    std::map<std::string, int> elsewhere; // exits to what isn't a field (events, the world map)
    for (const cr3::MapInfo& info : table) {
        cr3::FieldMap* f = load(info.name);
        if (!f) continue;
        ++maps;
        for (const std::string& e : f->exitNames()) {
            ++total;
            cr3::FieldMap* to = e.size() >= 5 ? load(e.substr(0, 5)) : nullptr;
            float p[3];
            if (to && to->point(e, p)) {
                ++good;
            } else {
                ++elsewhere[e];
                std::printf("  %s: exit %s - %s\n", info.name.c_str(), e.c_str(), to ? "the map has no such point" : "not a field map");
            }
        }
    }
    std::printf("%zu maps, %zu exits: %zu lead to a field map's point, %zu elsewhere\n", maps, total, good, total - good);
    return 0;
}

int walk(const char* path, const char* binary, const char* only) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<cr3::MapInfo> table;
    if (!readTable(path, binary, table)) return 1;
    size_t maps = 0, settled = 0, points = 0, runs = 0, offFloor = 0, tunnels = 0, stuck = 0, skipped = 0;
    for (const cr3::MapInfo& info : table) {
        if (only && info.name != only) continue;
        cr3::FieldMap field;
        std::string err;
        float start[3];
        if (!field.load(a, info, &err) || !field.point("defaultPos", start) || !field.collision().triangles()) {
            if (only) std::printf("  %s: %s\n", info.name.c_str(), err.empty() ? "no defaultPos or no collision" : err.c_str());
            ++skipped;
            continue;
        }
        ++maps;
        const cr3::CollisionWorld& w = field.collision();
        auto floorBelow = [&](const cr3::Vec3& p) { return !w.surfaceBelow(p, 0.5f).empty(); };
        // along the controller's own path in an update (playerStep): up a step, across at that
        // height, down - not the straight line, which cuts the corners of the steps it climbs down.
        // A hit: the wall's plane (a point on it, its normal towards where the player came from)
        auto throughWall = [&](const cr3::Vec3& from, const cr3::Vec3& to, cr3::Vec3* at = nullptr, cr3::Vec3* normal = nullptr) {
            const float up = 0.01f; // (the step height)
            const float across = std::max(from[1], to[1]) + up;
            const cr3::Vec3 path[4] = {from, {from[0], across, from[2]}, {to[0], across, to[2]}, to};
            for (int k = 0; k < 3; ++k) {
                cr3::CollisionWorld::Hit h;
                if (w.sweep(path[k], path[k + 1], 0.001f, {0, 1, 0}, -2.0f, h) && std::fabs(h.normal[1]) < 0.7f) {
                    if (at)
                        for (int c = 0; c < 3; ++c) (*at)[c] = path[k][c] + (path[k + 1][c] - path[k][c]) * h.fraction;
                    if (normal) *normal = h.normal;
                    return true;
                }
            }
            return false;
        };
        // from where the game puts the player: each pop point (an exit's arrival), else defaultPos
        std::vector<std::pair<std::string, cr3::Vec3>> starts;
        for (const cr3::Gimmick& g : field.gimmicks())
            if (g.kind == cr3::Gimmick::PopPoint) starts.push_back({g.name, {g.position[0], g.position[1], g.position[2]}});
        if (starts.empty()) starts.push_back({"defaultPos", {start[0], start[1], start[2]}});
        bool allSettled = true;
        for (const auto& [from, at] : starts) {
            cr3::CharacterController c(w);
            c.setPosition({at[0], at[1] + 0.17f, at[2]});
            for (int k = 0; k < 60; ++k) c.update({0, 0, 0}); // (3 s of the game's updates)
            const cr3::Vec3 rest = c.position();
            ++points;
            if (!(c.onGround() && floorBelow(rest))) {
                allSettled = false;
                std::printf("  %s: not on a floor after dropping at %s (%.3f %.3f %.3f -> %.3f %.3f %.3f)\n", info.name.c_str(),
                            from.c_str(), at[0], at[1], at[2], rest[0], rest[1], rest[2]);
                continue;
            }
            for (int dir = 0; dir < 8; ++dir) {
                ++runs;
                c.setPosition(rest);
                const float ang = float(dir) * 0.785398f;
                const cr3::Vec3 v{0.48f * std::sin(ang), 0, 0.48f * std::cos(ang)};
                bool off = false, tunnel = false;
                // a crossing counts when the player stays beyond the wall's plane: the next 10 updates
                // all past it (Bullet's controller jitters in creases at the game's step, its centre
                // dipping past a wall and straight back)
                bool pending = false;
                int pendingFor = 0;
                cr3::Vec3 wallAt{}, wallNormal{};
                auto beyond = [&](const cr3::Vec3& p) {
                    return (p[0] - wallAt[0]) * wallNormal[0] + (p[1] - wallAt[1]) * wallNormal[1] + (p[2] - wallAt[2]) * wallNormal[2] < -0.002f;
                };
                for (int k = 0; k < 80; ++k) { // (4 s of the game's updates)
                    const cr3::Vec3 before = c.position();
                    c.update(v);
                    if (!off && !floorBelow(c.position())) off = true;
                    if (pending) {
                        if (!beyond(c.position())) pending = false; // (back)
                        else if (++pendingFor >= 10) tunnel = true, pending = false;
                    } else if (!tunnel && throughWall(before, c.position(), &wallAt, &wallNormal)) {
                        pending = beyond(c.position()), pendingFor = 0;
                    }
                }
                if (pending) tunnel = true;
                if (tunnel && std::getenv("CR3_WALK_DEBUG"))
                    std::printf("    the wall at (%.4f %.4f %.4f), normal (%.3f %.3f %.3f); ends (%.4f %.4f %.4f)\n", wallAt[0], wallAt[1],
                                wallAt[2], wallNormal[0], wallNormal[1], wallNormal[2], c.position()[0], c.position()[1], c.position()[2]);
                const cr3::Vec3 end = c.position();
                offFloor += off;
                tunnels += tunnel;
                stuck += std::hypot(end[0] - rest[0], end[2] - rest[2]) < 0.01f;
                if (off || tunnel)
                    std::printf("  %s from %s, direction %d: %s%s (ends %.3f %.3f %.3f)\n", info.name.c_str(), from.c_str(), dir,
                                off ? "left the floor " : "", tunnel ? "through a wall" : "", end[0], end[1], end[2]);
            }
        }
        settled += allSettled;
    }
    std::printf("%zu maps walked (%zu without collision or defaultPos), from %zu points: %zu maps settled on a floor at all; "
                "%zu runs, %zu left the floor, %zu went through a wall, %zu didn't move (against a wall at once)\n",
                maps, skipped, points, settled, runs, offFloor, tunnels, stuck);
    return offFloor || tunnels ? 1 : 0;
}

int compareModels(const char* pa, const char* pb) {
    cr3::MvglArchive a, b;
    if (!openArchive(a, pa) || !openArchive(b, pb)) return 1;
    size_t files = 0, alike = 0, shapeDiffers = 0;
    double dPos = 0, dUv = 0, dNorm = 0;
    std::string worstPos, worstUv, worstNorm;
    std::vector<uint8_t> da, db;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".geom") != 0 || !b.contains(e.name)) continue;
        cr3::Geom ga, gb;
        if (!a.read(e, da) || !b.read(e.name, db) || !cr3::parseGeom(da, ga) || !cr3::parseGeom(db, gb)) continue;
        ++files;
        bool same = ga.meshes.size() == gb.meshes.size();
        for (size_t i = 0; same && i < ga.meshes.size(); ++i) {
            const auto &ma = ga.meshes[i], &mb = gb.meshes[i];
            if (ma.vertexCount != mb.vertexCount || ma.strip != mb.strip) { same = false; break; }
            auto cmp = [](const std::vector<float>& x, const std::vector<float>& y, double& worst, std::string& at, const std::string& name) {
                if (x.size() != y.size()) return;
                for (size_t k = 0; k < x.size(); ++k)
                    if (std::abs(double(x[k]) - y[k]) > worst) worst = std::abs(double(x[k]) - y[k]), at = name;
            };
            cmp(ma.position, mb.position, dPos, worstPos, e.name);
            cmp(ma.uv, mb.uv, dUv, worstUv, e.name);
            cmp(ma.normal, mb.normal, dNorm, worstNorm, e.name);
        }
        same ? ++alike : ++shapeDiffers;
    }
    std::printf("%zu mesh files in both: %zu with the same meshes, vertex counts and strips, %zu not\n", files, alike, shapeDiffers);
    std::printf("  largest differences: position %.6f (%s), uv %.6f (%s), normal %.6f (%s)\n", dPos, worstPos.c_str(), dUv,
                worstUv.c_str(), dNorm, worstNorm.c_str());
    return 0;
}

int animations(const char* path) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> data;
    // skeletons against their figures' inverse binds
    size_t figures = 0, skinned = 0, bound = 0;
    std::map<std::string, cr3::Skeleton> skeletons;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".skel") != 0) continue;
        cr3::Skeleton sk;
        std::string err;
        if (!a.read(e, data) || !cr3::parseSkeleton(data, sk, &err)) {
            std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
            continue;
        }
        const std::string base = e.name.substr(0, e.name.size() - 5);
        cr3::Geom g;
        if (a.read(base + ".geom", data) && cr3::parseGeom(data, g) && g.inverseBind.size() == sk.basePose.size()) {
            ++figures;
            const auto world = sk.world(sk.basePose);
            std::vector<bool> used(sk.basePose.size(), false);
            for (const auto& m : g.meshes)
                for (uint32_t j : m.palette)
                    if (j < used.size()) used[j] = true;
            for (size_t j = 0; j < used.size(); ++j) {
                if (!used[j]) continue;
                const cr3::Mat34 m = world[j] * g.inverseBind[j];
                float err = 0;
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 4; ++c) err = std::max(err, std::fabs(m.m[r * 4 + c] - (r == c ? 1.0f : 0.0f)));
                ++skinned;
                bound += err <= 1e-3f;
            }
        }
        skeletons[base] = std::move(sk);
    }
    std::printf("%zu skeletons; %zu figures, %zu skinned joints: %zu with base pose * inverse bind = identity (%.1f%%)\n",
                skeletons.size(), figures, skinned, bound, 100.0 * double(bound) / double(std::max<size_t>(skinned, 1)));
    // animations
    size_t anims = 0, bad = 0, unitBad = 0, boundaries = 0;
    double worst = 0;
    std::string worstAt;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".anim") != 0) continue;
        cr3::Animation an;
        std::string err;
        if (!a.read(e, data) || !an.parse(data, &err)) {
            ++bad;
            std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
            continue;
        }
        ++anims;
        std::vector<cr3::JointTransform> base(size_t(an.joints()));
        for (size_t cut = e.name.size() - 5; cut > 0; --cut) { // the figure: the name before the motion's
            const auto it = skeletons.find(e.name.substr(0, cut));
            if (it != skeletons.end() && it->second.basePose.size() == base.size()) { base = it->second.basePose; break; }
        }
        // driven joints: the ones some rotation channel sets (base poses of UI skeletons aren't unit)
        std::vector<cr3::JointTransform> probe = base;
        for (auto& t : probe) t.rotation[0] = t.rotation[1] = t.rotation[2] = t.rotation[3] = 0;
        an.evaluate(0, probe);
        for (int fr = 0; fr < an.frames(); fr += std::max(1, an.frames() / 16)) {
            std::vector<cr3::JointTransform> p = base;
            an.evaluate(float(fr), p);
            for (size_t j = 0; j < p.size(); ++j) {
                const float* q = probe[j].rotation;
                if (q[0] == 0 && q[1] == 0 && q[2] == 0 && q[3] == 0) continue;
                const float* r = p[j].rotation;
                if (std::fabs(std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]) - 1) > 1e-3f) { ++unitBad; break; }
            }
        }
        // continuity across frame sets: just before a set's start and at it
        for (int fr = 1; fr < an.frames(); ++fr) {
            std::vector<cr3::JointTransform> p0 = base, p1 = base, pm = base;
            an.evaluate(float(fr) - 1e-4f, p0);
            an.evaluate(float(fr), p1);
            an.evaluate(float(fr) - 2e-4f, pm);
            // a jump: the step at the frame far bigger than the step just before it
            for (size_t j = 0; j < p0.size(); ++j)
                for (int c = 0; c < 3; ++c) {
                    const double step = std::fabs(double(p1[j].translation[c]) - p0[j].translation[c]);
                    const double before = std::fabs(double(p0[j].translation[c]) - pm[j].translation[c]);
                    const double rel = (step - 2 * before) / std::max(1.0, std::fabs(double(p1[j].translation[c])));
                    if (rel > worst) worst = rel, worstAt = e.name + " frame " + std::to_string(fr);
                }
            ++boundaries;
        }
    }
    std::printf("%zu animations parse, %zu bad; driven rotations off unit length in %zu sampled frames; "
                "%zu frames checked for jumps, the largest translation jump %.2g (%s)\n",
                anims, bad, unitBad, boundaries, worst, worstAt.c_str());
    return bad ? 1 : 0;
}

int pose(const char* path, const std::string& figure, const std::string& animName, float frame) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> data;
    cr3::Skeleton sk;
    cr3::Animation an;
    std::string err;
    if (!a.read(figure + ".skel", data, &err) || !cr3::parseSkeleton(data, sk, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    if (!a.read(animName + ".anim", data, &err) || !an.parse(data, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::vector<cr3::JointTransform> p = sk.basePose;
    an.evaluate(frame, p);
    for (size_t j = 0; j < p.size(); ++j)
        std::printf("%zu: base R %.4f %.4f %.4f %.4f  T %.4f %.4f %.4f  S %.4f %.4f %.4f\n", j, sk.basePose[j].rotation[0],
                    sk.basePose[j].rotation[1], sk.basePose[j].rotation[2], sk.basePose[j].rotation[3], sk.basePose[j].translation[0],
                    sk.basePose[j].translation[1], sk.basePose[j].translation[2], sk.basePose[j].scale[0], sk.basePose[j].scale[1],
                    sk.basePose[j].scale[2]);
    for (size_t j = 0; j < p.size(); ++j)
        std::printf("%zu: R %.4f %.4f %.4f %.4f  T %.4f %.4f %.4f  S %.4f %.4f %.4f\n", j, p[j].rotation[0], p[j].rotation[1],
                    p[j].rotation[2], p[j].rotation[3], p[j].translation[0], p[j].translation[1], p[j].translation[2], p[j].scale[0],
                    p[j].scale[1], p[j].scale[2]);
    return 0;
}

// CR3's battle (battle/cr3battle.hpp): a coupling's set-up on a battle field - its units, where
// they stand, the cameras of the largest's size, and whether Cam_bt holds them
int cr3Battle(const char* path, int coupling, int fieldId, const char* partyArg, uint32_t seed = 1, const char* commandsArg = nullptr) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::string err;
    cr3::Cr3BattleTables t;
    if (!t.load(a, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::vector<int> party;
    for (const char* p = partyArg; p && *p;) {
        party.push_back(std::atoi(p));
        p = std::strchr(p, ',');
        if (p) ++p;
    }
    cr3::Cr3Battle b;
    if (!b.setup(t, coupling, fieldId, party, 1, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::printf("coupling %d on field %d: %s (%s)\n", coupling, fieldId, b.field.c_str(), a.contains(b.field + ".geom") ? "found" : "MISSING");
    for (const cr3::Cr3BattleUnit& u : b.units)
        std::printf("  %s %-5s figure %-8s%s%s variant %-2s width %.2f depth %.2f scale %.2f camera size %d  home (%.3f %.3f %.3f) yaw %.0f%s\n",
                    u.player ? "party  " : "monster", u.name.c_str(), u.figure.c_str(), u.head.empty() ? "" : " + ", u.head.c_str(),
                    u.variant.empty() ? "-" : u.variant.c_str(), u.width, u.depth, u.scale, u.cameraSize, u.home[0], u.home[1], u.home[2],
                    u.yaw * 57.29578f, a.contains(u.figure + ".geom") ? "" : "  (figure MISSING)");
    cr3::Cr3BattleRules rules;
    std::vector<int> commands; // (the party's first member's command each turn; its attack after)
    for (const char* p = commandsArg; p && *p;) {
        commands.push_back(std::atoi(p));
        p = std::strchr(p, ',');
        if (p) ++p;
    }
    if (rules.setup(t, b, 3, seed, &err)) {
        for (const cr3::Cr3Fighter& f : rules.fighters) {
            cr3::Cr3Command c;
            rules.command(f.attackCommand, c);
            std::printf("  fighter %d: HP %d MP %d atk %d def %d spd %d luck %d level %d gene level %d; attack %d (power %d x%d, formula %d, spread %d, motion %d, direction %d)\n",
                        f.id, f.maxHp, f.maxMp, f.attack, f.defence, f.speed, f.luck, f.level, f.geneLevel, f.attackCommand, c.power, c.hits, c.formula,
                        c.spread, c.motion, c.direction);
            std::printf("    weapon constant %d x%d, consumption %.2f; resistances physical %d magic %d; skills", f.weaponConstant, f.weaponHits,
                        f.consumption, f.physical, f.magic);
            for (int sk : f.skills) std::printf(" %d", sk);
            std::printf("\n");
            std::vector<int> listed = f.geneCommands;
            for (const auto& [mc, ratio] : f.commands) listed.push_back(mc);
            for (int g : listed) {
                cr3::Cr3Command gc;
                rules.command(g, gc);
                std::printf("    %s command %d: type %d, damage %d, target kind %d, power %d x%d%s, MP %d, element %d\n", f.player ? "gene" : "monster", g,
                            gc.type, gc.damage, gc.target, gc.power, gc.hits, gc.all ? " (all)" : "", rules.mpCost(f, gc), gc.element);
            }
        }
        for (int turn = 1; turn <= 8 && rules.outcome() == 0; ++turn) {
            std::vector<std::array<int, 3>> choices;
            for (size_t k = 0; k < rules.fighters.size(); ++k)
                if (rules.fighters[k].player && !rules.fighters[k].dead())
                    choices.push_back({int(k), k == 0 && size_t(turn - 1) < commands.size() ? commands[size_t(turn - 1)] : rules.fighters[k].attackCommand, -1});
            for (const cr3::Cr3Action& x : rules.turn(choices)) {
                int sum = 0;
                std::string each;
                if (!x.targets.empty())
                    for (size_t h = 0; h < x.targets[0].hits.size(); ++h) {
                        const int v = x.targets[0].hits[h];
                        sum += v;
                        const uint8_t fl = h < x.targets[0].flags.size() ? x.targets[0].flags[h] : 0;
                        each += " " + ((fl & 4) ? std::string("miss") : std::to_string(v) + ((fl & 1) ? "!" : "") + ((fl & 2) ? "v" : ""));
                    }
                std::printf("    turn %d: %d -> %d command %d: %zu hits, %d (HP %d%s) [%s ] gauge %d\n", turn, x.source, x.target, x.command,
                            x.hits.size(), sum, x.hpAfter, x.killed ? ", dead" : "", each.c_str(), x.gaugeAfter);
            }
            for (const auto& [fi, change] : rules.endTurn()) std::printf("    turn %d end: %d HP %+d\n", turn, fi, change);
        }
        std::printf("  outcome %d\n", rules.outcome());
    }
    cr3::Cr3BattleCamera cam;
    const bool camLoaded = cam.load(a, "Cam_bt");
    std::printf("camera size %d, get-set end %.3f s; Cam_bt %s (%.2f s)\n", b.cameraSize, b.getSetEnd, camLoaded ? "loaded" : "MISSING", cam.duration());
    for (size_t k = 0; k < b.cameras.size(); ++k) {
        const std::string first = b.cameras[k].substr(0, b.cameras[k].find('\n'));
        cr3::Mat34 w;
        float fov = 0, zn = 0, zf = 0;
        cr3::Cr3BattleCamera own;
        const bool ok = first.find("Cam_bt") != std::string::npos ? cam.at(first, 0, w, fov, zn, zf)
                                                                   : own.load(a, first) && own.at(first, 0, w, fov, zn, zf);
        std::printf("  %zu %-22s %s", k, first.c_str(), ok ? "" : "not found\n");
        if (ok) std::printf("at (%.2f %.2f %.2f) fov %.1f near %.2f far %.0f\n", w.m[3], w.m[7], w.m[11], fov, zn, zf);
    }
    return 0;
}

// The battles: each coupling's set-up (battle/battledata.cpp) against the archive - its map, its
// figures and their first motions, the battledpos joints they stand on, the commands' camera
int battles(const char* path, const char* which) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> d;
    std::string err;
    cr3::BattleData data;
    if (!a.read("data\\battledata.dat", d, &err) || !data.parse(d, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cr3::Skeleton dpos;
    if (!a.read("battledpos.skel", d, &err) || !cr3::parseSkeleton(d, dpos, &err)) return std::fprintf(stderr, "battledpos: %s\n", err.c_str()), 1;
    std::vector<uint32_t> cameras;
    for (const char* f : {"battlecamera_move.geom", "battlecamera_nomove.geom"}) {
        cr3::Geom g;
        if (a.read(f, d) && cr3::parseGeom(d, g))
            for (const cr3::GeomCamera& c : g.cameras) cameras.push_back(c.hash);
    }
    const bool all = !std::strcmp(which, "all");
    const int party[2] = {1, 2}; // (Darwin and Orlando)
    int set = 0, used = 0, complete = 0;
    std::map<std::string, int> missing;
    for (int id = all ? 1 : std::atoi(which); id <= (all ? data.couplings() : std::atoi(which)); ++id) {
        const cr3::BattleData::Coupling* c = data.coupling(id);
        if (!c || c->monsters[0] <= 0) continue; // (an empty record)
        ++used;
        cr3::BattleSetup b;
        if (!cr3::setupBattle(data, id, party, b, &err)) {
            std::printf("  %d: %s\n", id, err.c_str());
            continue;
        }
        ++set;
        std::vector<std::string> lacks;
        if (!a.contains(b.map + ".geom")) lacks.push_back("map " + b.map);
        for (const std::string& cam : {b.camera, b.startCamera})
            if (std::find(cameras.begin(), cameras.end(), cr3::nameHash(cam.c_str())) == cameras.end()) lacks.push_back("camera " + cam);
        auto unit = [&](const cr3::BattleSetup::Unit& u) {
            if (!a.contains(u.figure + ".geom")) lacks.push_back("figure " + u.figure);
            else if (!a.contains(u.figure + u.idle + ".anim") && !a.contains(u.figure + "bb01.anim")) lacks.push_back("motion " + u.figure + u.idle);
            if (dpos.joint(cr3::nameHash(u.position.c_str())) < 0) lacks.push_back("position " + u.position);
        };
        for (const auto& u : b.party) unit(u);
        for (const auto& u : b.monsters) unit(u);
        complete += lacks.empty();
        for (const std::string& l : lacks) ++missing[l];
        if (!all || !lacks.empty()) {
            std::printf("  %d: map %s, music %d, pattern %s:", id, b.map.c_str(), c->music, b.patternName.c_str());
            for (const auto& u : b.monsters) {
                const cr3::BattleData::Monster* m = data.monster(u.id);
                std::printf(" %s (%s%s, %d HP) at %s;", m->name[1].c_str(), u.figure.c_str(), u.colour.c_str(), m->hp, u.position.c_str());
            }
            for (const auto& u : b.party) std::printf(" %s at %s;", data.character(u.id)->name[1].c_str(), u.position.c_str());
            std::printf(" camera %s", b.camera.c_str());
            for (const std::string& l : lacks) std::printf("; no %s", l.c_str());
            std::printf("\n");
        }
    }
    std::printf("%d couplings with monsters: %d set up, %d with everything they use in the archive\n", used, set, complete);
    for (const auto& [k, n] : missing) std::printf("  missing %s (%d)\n", k.c_str(), n);
    return 0;
}

// A battle played by its rules (battle/rules.cpp): the party attacking every turn - solo, each at
// the first monster standing, or as a pair - the monsters choosing their own, to the end
int fight(const char* path, int coupling, bool pair, uint32_t seed, const int party[2]) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> d;
    std::string err;
    cr3::BattleData data;
    if (!a.read("data\\battledata.dat", d, &err) || !data.parse(d, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cr3::BattleRules r(data, seed);
    if (!r.start(coupling, party, 0, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    // the battle's script: battle.nut's Battle_Init, the encounter's own then (its calls run to
    // their ends here, the pages turning by themselves)
    cr3::GameState state;
    const int chars[2] = {party[0] - 1, party[1] - 1}, monsters[3] = {r.fighter(2).id, r.fighter(3).id, r.fighter(4).id};
    cr3::BattleScript script(a, state, r, coupling, chars, monsters);
    bool scriptOk = script.begin() && script.runToEnd();
    r.script = [&](const std::string& f) {
        if (script.own() && scriptOk) scriptOk = script.call(f) && script.runToEnd();
    };
    if (script.own() && scriptOk) scriptOk = script.call("Battle_Start") && script.runToEnd();
    if (!scriptOk) std::printf("script: %s\n", script.error().c_str());
    std::printf("script: %s%s; charge %d\n", script.name().c_str(), script.own() ? " (its own)" : "", r.charge());
    if (r.advantage()) std::printf("start: %s\n", r.advantage() == 1 ? "the party strikes first" : "the monsters' assault");
    for (int i = 0; i < 5; ++i) {
        const cr3::Fighter& f = r.fighter(i);
        if (!f.present) continue;
        std::printf("%d %-12s lv %3d  HP %5d  MP %4d  Atk %4d Mag %4d Def %4d Spd %4d %4d\n", i, f.name.c_str(), f.level, f.maxHp, f.maxMp,
                    f.param[0], f.param[1], f.param[2], f.param[3], f.param[4]);
    }
    auto name = [&](int i) { return i >= 0 ? r.fighter(i).name : std::string("-"); };
    while (!r.outcome() && !r.exited() && r.turn() < 50) {
        std::vector<cr3::BattleRules::Order> orders;
        for (int k = 0; k < 2; ++k) {
            const cr3::Fighter& f = r.fighter(k);
            if (!f.alive) continue;
            cr3::BattleRules::Order o;
            o.actor = k;
            o.command = f.attack; // (BtlCommand::StepCommandLoop: "attack" is the character's +0x44 either way)
            for (int m = 2; m < 5; ++m)
                if (r.fighter(m).alive) {
                    o.target = m;
                    break;
                }
            orders.push_back(o);
        }
        const int turn = r.turn() + 1;
        for (const auto& e : r.playTurn(pair, orders)) {
            const cr3::BattleData::Command* c = data.command(e.command);
            const std::string what = e.failed ? "not done" : e.blocked ? "blocked" : e.hit ? std::to_string(e.damage) + " damage" : "missed";
            std::printf("turn %2d: %s%s%s %s%s -> %s: %s%s%s%s\n", turn, name(e.actor).c_str(), e.partner >= 0 ? " + " : "",
                        e.partner >= 0 ? name(e.partner).c_str() : "", c ? c->name[1].c_str() : "?", e.counter ? " (counter)" : "",
                        name(e.target).c_str(), what.c_str(), e.critical ? " (critical)" : e.weak ? " (weak)" : "", e.defeated ? ", down" : "",
                        e.regenerated ? (", back with " + std::to_string(e.regenHp) + " HP").c_str() : "");
        }
        std::printf("         charge %d, gauge %d%s; HP:", r.charge(), r.gauge(), r.breakState() == 1 ? " (the party's turn break)" : r.breakState() == 2 ? " (the monsters' turn break)" : r.breakState() == 3 ? " (a break coming)" : "");
        for (int i = 0; i < 5; ++i)
            if (r.fighter(i).present) std::printf(" %s %d", r.fighter(i).name.c_str(), r.fighter(i).hp);
        std::printf("\n");
        if (r.exitTimer() >= 0) r.exitBattle(); // (its timer: up before the next turn's input)
    }
    if (r.outcome() == 1 && !r.exited()) r.script("Battle_End"); // (BtlResult)
    std::printf("%s after %d turns\n", r.exited() ? "exited by the script" : r.outcome() == 1 ? "won" : r.outcome() == 2 ? "lost" : "undecided",
                r.turn());
    std::printf("script calls:");
    for (const std::string& c : script.calls()) std::printf(" %s", c.c_str());
    std::printf("\nstub natives:");
    for (const auto& [n, k] : script.stubCalls()) std::printf(" %s %d", n.c_str(), k);
    std::printf("\n");
    return 0;
}

// The screen breaking away (battle/glass.hpp), for seeds 1..n: its shards within the points' hull,
// none over another (their areas' sum at most its area - the game's tight super triangle takes the
// hull's thinnest), each triangle's circle holding no other point (Delaunay), how much of the
// screen they cover, and how many are still on it as it ends
int glass(int seeds) {
    int bad = 0;
    double cover = 0, tiles = 0, left = 0;
    size_t shards = 0;
    for (int seed = 1; seed <= seeds; ++seed) {
        cr3::Rand48 rng;
        rng.seed(uint32_t(seed));
        cr3::GlassFragments g;
        g.build(512, 320, rng);
        std::vector<std::pair<double, double>> pts;
        double area = 0;
        for (const cr3::GlassShard& s : g.shards) {
            area += std::fabs((s.x[1] - s.x[0]) * (s.y[2] - s.y[0]) - (s.x[2] - s.x[0]) * (s.y[1] - s.y[0])) / 2;
            for (int c = 0; c < 3; ++c) pts.push_back({s.x[c], s.y[c]});
        }
        std::sort(pts.begin(), pts.end());
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
        std::vector<std::pair<double, double>> hull(pts.size() * 2); // (Andrew's monotone chain)
        size_t k = 0;
        auto cross = [](auto& o, auto& a, auto& b) { return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first); };
        for (size_t i = 0; i < pts.size(); hull[k++] = pts[i++])
            while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        for (size_t i = pts.size() - 1, t = k + 1; i > 0; hull[k++] = pts[--i])
            while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 0) --k;
        hull.resize(k - 1);
        double hullArea = 0;
        for (size_t i = 0; i < hull.size(); ++i) {
            const auto& a = hull[i];
            const auto& b = hull[(i + 1) % hull.size()];
            hullArea += a.first * b.second - b.first * a.second;
        }
        hullArea = std::fabs(hullArea) / 2;
        int empty = 0;
        for (const cr3::GlassShard& s : g.shards) {
            const double ax = s.x[0], ay = s.y[0], bx = s.x[1], by = s.y[1], cx = s.x[2], cy = s.y[2];
            const double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
            const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
            const double ox = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d, oy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
            const double r2 = (ax - ox) * (ax - ox) + (ay - oy) * (ay - oy);
            for (const auto& q : pts)
                if ((q.first - ox) * (q.first - ox) + (q.second - oy) * (q.second - oy) < r2 * (1 - 1e-6)) {
                    ++empty;
                    break;
                }
        }
        const bool tiled = area <= hullArea * (1 + 1e-6) && area >= hullArea * 0.9; // (none over another; the hull's thinnest lost)
        g.advanceTo(cr3::GlassFragments::kLength);
        int shown = 0;
        for (const cr3::GlassShard& s : g.shards)
            for (int c = 0; c < 3; ++c)
                if (std::fabs(s.x[c]) < 512 && std::fabs(s.y[c]) < 320) {
                    ++shown;
                    break;
                }
        if (!tiled || empty || pts.size() != size_t(cr3::GlassFragments::kPoints + 4)) {
            ++bad;
            std::printf("seed %d: %zu points, %zu shards, area %.1f of the hull's %.1f, %d with a point in their circle\n", seed,
                        pts.size(), g.shards.size(), area, hullArea, empty);
        }
        cover += area / (1024.0 * 640.0), tiles += area / hullArea, left += shown, shards += g.shards.size();
    }
    std::printf("%d seeds: %.1f shards each, covering %.2f%% of the screen (%.2f%% of their hull); %.2f on it at the end; %d bad\n",
                seeds, double(shards) / seeds, cover * 100 / seeds, tiles * 100 / seeds, left / seeds, bad);
    return bad ? 1 : 0;
}

// The game's own saves (script/gamesave.hpp): game.dat / cont.dat (and its system.dat) as a CR3-U
// save on stdout; a CR3-U save as game.dat (its records' HP, MP and parameters by the battle data)
bool readFile(const char* path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
int saveImport(const char* game, const char* system) {
    std::vector<uint8_t> d;
    if (!readFile(game, d)) return std::fprintf(stderr, "%s: can't read\n", game), 1;
    cr3::GameState g;
    cr3::GameSavePlace place;
    bool crc = false;
    std::string err;
    if (!cr3::readGameSave(d, g, place, &crc, &err)) return std::fprintf(stderr, "%s: %s\n", game, err.c_str()), 1;
    if (!crc) std::fprintf(stderr, "%s: its CRC doesn't hold (read anyway)\n", game);
    std::map<std::string, std::string> extra;
    if (system) {
        uint32_t options[6];
        if (!readFile(system, d) || !cr3::readSystemSave(d, g, options, &crc, &err)) return std::fprintf(stderr, "%s: %s\n", system, err.c_str()), 1;
        if (!crc) std::fprintf(stderr, "%s: its CRC doesn't hold (read anyway)\n", system);
    }
    char pos[96];
    std::snprintf(pos, sizeof pos, "%.6f %.6f %.6f %.6f", place.position[0], place.position[1], place.position[2], place.heading);
    extra["map"] = place.map, extra["place"] = pos, extra["playtime"] = std::to_string(place.playFrames);
    std::fputs(g.save(extra).c_str(), stdout);
    return 0;
}
int saveExport(const char* path, const char* save, const char* out) {
    cr3::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> d;
    std::string err;
    cr3::BattleData data;
    if (!a.read("data\\battledata.dat", d, &err) || !data.parse(d, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    if (!readFile(save, d)) return std::fprintf(stderr, "%s: can't read\n", save), 1;
    cr3::GameState g;
    std::map<std::string, std::string> extra;
    if (!g.load(std::string(d.begin(), d.end()), &extra, &err)) return std::fprintf(stderr, "%s: %s\n", save, err.c_str()), 1;
    cr3::GameSavePlace place;
    if (!extra["map"].empty()) place.map = extra["map"];
    std::sscanf(extra["place"].c_str(), "%f %f %f %f", &place.position[0], &place.position[1], &place.position[2], &place.heading);
    place.playFrames = uint32_t(std::atol(extra["playtime"].c_str()));
    const auto bytes = cr3::writeGameSave(g, place, [&](int ch, int& hp, int& mp, int param[5]) {
        int eq[8];
        for (int e = 0; e < 8; ++e) eq[e] = g.equip[ch][e];
        const auto st = cr3::BattleRules::playerStats(data, ch + 1, std::max(g.level[ch], 1), eq, g.sopiaLevel, g.seed[ch], g.itemCount);
        hp = st.maxHp, mp = st.maxMp;
        for (int k = 0; k < 5; ++k) param[k] = st.param[k];
    });
    std::FILE* f = std::fopen(out, "wb");
    if (!f || std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) return std::fprintf(stderr, "%s: can't write\n", out), 1;
    std::fclose(f);
    std::printf("%s: %zu bytes (map %s)\n", out, bytes.size(), place.map.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2 && !std::strcmp(argv[1], "--patch")) gPatch = argv[2], argc -= 2, argv += 2;
    if (argc < 3) return usage();
    if (!std::strcmp(argv[1], "release")) return release(argv[2]);
    if (!std::strcmp(argv[1], "db")) return db(argv[2], argc > 3 ? argv[3] : nullptr);
    if (!std::strcmp(argv[1], "texts")) return texts(argv[2]);
    if (!std::strcmp(argv[1], "events")) return events(argv[2], argc > 3 && std::strcmp(argv[3], "all") ? argv[3] : nullptr, argc > 4 ? std::atoi(argv[4]) : 300);
    if (!std::strcmp(argv[1], "check")) return check(argv[2]);
    if (!std::strcmp(argv[1], "scripts")) return scripts(argv[2], argc > 3 ? argv[3] : nullptr);
    if (!std::strcmp(argv[1], "models")) return models(argv[2]);
    if (!std::strcmp(argv[1], "animations")) return animations(argv[2]);
    if (!std::strcmp(argv[1], "pose") && argc > 5) return pose(argv[2], argv[3], argv[4], float(std::atof(argv[5])));
    if (!std::strcmp(argv[1], "compare-models") && argc > 3) return compareModels(argv[2], argv[3]);
    if (!std::strcmp(argv[1], "shaders")) return shaders(argv[2]);
    if (!std::strcmp(argv[1], "event") && argc > 3) return traceEvent(argv[2], argv[3], argc > 4 ? argv[4] : nullptr, 20 * 60 * 30);
    if (!std::strcmp(argv[1], "play") && argc > 3) return play(argv[2], argv[3]);
    if (!std::strcmp(argv[1], "battlescripts")) return battleScripts(argv[2]);
    if (!std::strcmp(argv[1], "exits") && argc > 3) return exits(argv[2], argv[3]);
    if (!std::strcmp(argv[1], "fieldscript") && argc > 3) return fieldScripts(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "walk") && argc > 3) return walk(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "images")) return images(argv[2], argc > 3 ? argv[3] : nullptr, argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "battle") && argc > 3) return battles(argv[2], argv[3]);
    if (!std::strcmp(argv[1], "cr3battle") && argc > 4) return cr3Battle(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), argc > 5 ? argv[5] : "6",
                                                                       argc > 6 ? uint32_t(std::strtoul(argv[6], nullptr, 10)) : 1u, argc > 7 ? argv[7] : nullptr);
    if (!std::strcmp(argv[1], "fight") && argc > 3) {
        int party[2] = {1, 2};
        if (argc > 6) std::sscanf(argv[6], "%d,%d", &party[0], &party[1]);
        return fight(argv[2], std::atoi(argv[3]), argc > 4 && !std::strcmp(argv[4], "pair"), argc > 5 ? uint32_t(std::atoi(argv[5])) : 1, party);
    }
    if (!std::strcmp(argv[1], "run") && argc > 3) return run(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "sound")) return sound(argv[2], argc > 3 && !std::strcmp(argv[3], "--full"));
    if (!std::strcmp(argv[1], "glass")) return glass(std::max(std::atoi(argv[2]), 1));
    if (!std::strcmp(argv[1], "save-import")) return saveImport(argv[2], argc > 3 ? argv[3] : nullptr);
    if (!std::strcmp(argv[1], "save-export") && argc > 4) return saveExport(argv[2], argv[3], argv[4]);
    return usage();
}
