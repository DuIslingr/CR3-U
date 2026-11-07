// The field tab's buttons (Cr3TabMenu.AddTabMenu bits: 0 the encounters' switch, 1 Battle) kept
// by CR3-U's save and the game's own (cr3_field_data +0x34), and an older save's made from the
// flags the scripts keep beside them (4107, 4106 - MenuTabAddALL)
#include "script/gamesave.hpp"
#include "script/gamestate.hpp"
#include <cstdio>
#include <map>
#include <string>

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) ++failures, std::printf("tab buttons FAILED: %s\n", what);
}

int main() {
    const std::map<std::string, std::string> extra{{"area", "d0201 0 0 0 0"}};
    {
        cr3::GameState g;
        g.tabButtons = 2;
        cr3::GameState back;
        std::string error;
        check(back.load(g.save(extra), nullptr, &error), "a save with its buttons loads");
        check(back.tabButtons == 2, "CR3-U's save keeps the buttons");
    }
    {
        cr3::GameState g;
        g.setFlag(4107, true);
        std::string text = g.save(extra);
        const size_t at = text.find("tabs ");
        check(at != std::string::npos, "the buttons are written");
        if (at != std::string::npos) text.erase(at, text.find('\n', at) - at + 1); // (an older save: no buttons line)
        cr3::GameState back;
        back.tabButtons = 7;
        std::string error;
        check(back.load(text, nullptr, &error), "an older save loads");
        check(back.tabButtons == 1, "an older save's buttons come from flags 4107 / 4106");
    }
    {
        cr3::GameState g;
        g.tabButtons = 3;
        cr3::GameSavePlace place;
        const std::vector<uint8_t> d = cr3::writeGameSave(g, place, nullptr);
        cr3::GameState back;
        cr3::GameSavePlace placeBack;
        check(cr3::readGameSave(d, back, placeBack), "game.dat reads back");
        check(back.tabButtons == 3, "game.dat keeps the buttons at cr3_field_data +0x34");
    }
    std::printf("tab buttons: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
