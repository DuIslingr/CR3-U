#include "script/gamestate.hpp"
#include <iostream>
#include <stdexcept>

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        cr3::GameState source, target;
        std::string error;
        target.cr3Money = 17;
        check(!source.makeNewGamePlus(target, &error) && target.cr3Money == 17 && !error.empty(),
              "Uncleared save accepted or destination changed");
        source.setFlag(733, true); // entering the epilogue alone is not the final quest's clear
        check(!source.canNewGamePlus(), "Chapter flag unlocked New Game Plus");
        source.setFlag(6016, true);
        source.setFlag(2001, true);
        source.setFlag(1021, true);
        source.setFlag(1023, true);
        source.setFlag(1025, true);
        source.setFlag(4101, true);
        source.setFlag(8999, true);
        source.cr3TeamLevel = 139;
        source.cr3ProfileValue = 123;
        source.cr3Items = {{7, 5}, {100001, 2}, {600001, 1}};
        source.cr3Weapon[1] = 1;
        source.cr3Armour[1] = 48;
        source.cr3Armour[4] = 144; // imported equipment can lack its kit flag
        source.cr3Accessory[1] = 7;
        source.cr3Attachments[100001] = {17, 9};
        cr3::Cr3Gene gene(483, 99999);
        gene.uid = 12;
        gene.locked = true;
        gene.slots[0][0] = 100;
        gene.adds[0] = 50;
        source.cr3Genes.push_back(gene);
        gene.uid = 18;
        gene.stocked = true;
        source.cr3Genes.push_back(gene);
        source.cr3GeneUid = 18;
        source.cr3GeneOf[1] = 12;
        source.cr3GeneBook = {483, 484};
        source.cr3GeneSeen = {483};
        source.cr3GeneStock = 250;
        source.cr3Sheets["d01"].opened = {1, 2, 3};
        source.cr3Sheets["d01"].completed = 2;
        cr3::Cr3Present present;
        present.gene = true;
        present.geneData = gene;
        source.cr3Presents.push_back(present);
        source.cr3Money = 123456;
        source.cr3Coins = 91;
        source.cr3CoinsSpent = 19;
        source.cr3GP = 80;
        source.cr3GPSpent = 20;
        source.cr3Daily = 31;
        source.cr3VipDays = 9;
        source.cr3VipTrial = true;
        source.cr3LastLogin = 1700000000;
        source.cr3Cheer = 345;
        source.cr3Names["name"] = "Name with spaces";
        source.cr3Names["team"] = "Old Team";
        source.cr3Records.playSeconds = 36000;
        source.cr3Records.kills = 456;
        source.cr3Records.maxDamage = 999999;
        source.cr3Records.vsMatches = 44;
        source.cr3Records.merges = 12;
        source.cr3Vs.boosts[5] = 7;
        source.cr3Vs.playing = true;
        source.cr3Vs.score = 999;
        source.cr3Vs.cleared.insert(88);
        source.cr3Vs.missionCleared = true;
        source.cr3Quests.begun.insert(16);
        source.cr3Quests.ended.insert(16);
        source.cr3Quests.offered.push_back(16);
        source.cr3Quests.endedCount = 10;
        source.cr3Quests.state = 3;
        source.cr3Party[0] = 1;
        source.cr3Party[1] = 2;
        source.cr3Mp[1] = 1;
        source.globalWork[24] = 16;
        source.globalWork[25] = 99;
        source.cr3Boxes.push_back({1, "tbox_00", 1, 1, 0, 5, true});
        source.fieldBoxes["tbox_00"] = 2;
        source.switchesOn.insert("switch");
        source.goodsButtons.insert(0);
        source.gimmickShown["person"] = false;
        source.brokenRocks.insert("rock");
        source.popPoint2 = "d0502_p1";
        source.next.title = true;
        source.next.battle = 10;
        source.encountEnable = false;
        const std::string before = source.save();
        check(source.makeNewGamePlus(target, &error), "Cleared save rejected");
        check(source.save() == before, "Source changed");
        check(target.cr3NewGamePlus == 1 && !target.canNewGamePlus(), "Story clear or run count wrong");
        check(target.flag(1021) && target.flag(1023) && target.flag(1025) && !target.flag(1022) &&
                  target.flag(1024) && !target.flag(2001) && !target.flag(733) && !target.flag(8999) &&
                  target.globalWork[24] == 0 && target.globalWork[25] == 0,
              "Story work carried over");
        check(target.cr3Party[0] == 6 && target.cr3Party[1] == 0 && target.cr3Mp.empty(),
              "Prologue party or recovery wrong");
        check(target.cr3TeamLevel == 139 && target.cr3ProfileValue == 123, "Level/profile lost");
        check(target.cr3Items == source.cr3Items && target.cr3Weapon == source.cr3Weapon &&
                  target.cr3Armour == source.cr3Armour && target.cr3Accessory == source.cr3Accessory &&
                  target.cr3Attachments == source.cr3Attachments,
              "Inventory/equipment lost");
        check(target.cr3Genes.size() == 2 && target.cr3Genes[0].uid == 12 && target.cr3Genes[1].uid == 18 &&
                  target.cr3Genes[1].stocked && target.cr3Genes[0].exp == 99999 && target.cr3Genes[0].adds[0] == 50 &&
                  target.cr3Genes[0].slots[0][0] == 100 && target.cr3Genes[0].locked && target.wornGene(1) == 0,
              "Gene progression/equipment lost");
        check(target.cr3GeneUid == 18 && target.cr3GeneStock == 250 && target.cr3GeneBook == source.cr3GeneBook &&
                  target.cr3GeneSeen == source.cr3GeneSeen,
              "Gene collection lost");
        check(target.cr3Sheets.at("d01").completed == 2 &&
                  target.cr3Sheets.at("d01").opened == source.cr3Sheets.at("d01").opened &&
                  target.cr3Presents.size() == 1 && target.cr3Presents[0].geneData.uid == 18,
              "Sheets/presents lost");
        check(target.cr3Money == 123456 && target.cr3Coins == 91 && target.cr3CoinsSpent == 19 && target.cr3GP == 80 &&
                  target.cr3GPSpent == 20,
              "Currency lost");
        check(target.cr3Daily == 31 && target.cr3LastLogin == 1700000000 && target.cr3VipDays == 9 &&
                  target.cr3VipTrial && target.cr3Cheer == 345,
              "Daily claims reset");
        check(target.cr3Names == source.cr3Names && target.cr3Records.playSeconds == 36000 &&
                  target.cr3Records.kills == 456 && target.cr3Records.maxDamage == 999999 &&
                  target.cr3Records.vsMatches == 44,
              "Names/records lost");
        check(target.cr3Quests.begun.empty() && target.cr3Quests.ended.empty() && target.cr3Quests.offered.empty() &&
                  target.cr3Quests.state == 0 && target.cr3Quests.selected == -100 && target.cr3Quests.endedCount == 0,
              "Quest progression retained");
        check(target.cr3Vs.boosts == source.cr3Vs.boosts && !target.cr3Vs.playing && target.cr3Vs.cleared.empty() &&
                  target.cr3Vs.score == 0 && !target.cr3Vs.missionCleared,
              "VS progression or items wrong");
        check(target.cr3Boxes.empty() && target.fieldBoxes.empty() && target.switchesOn.empty() &&
                  target.goodsButtons.empty() && target.gimmickShown.empty() && target.brokenRocks.empty() &&
                  target.popPoint2.empty() && target.encountEnable && !target.next.title && target.next.battle == -1,
              "Field/script state leaked");
        cr3::GameState loaded;
        check(loaded.load(target.save({{"area", "t0108"}})) && loaded.save() == target.save(), "NG+ save round trip failed");
        loaded.setFlag(6016, true);
        check(loaded.makeNewGamePlus(loaded) && loaded.cr3NewGamePlus == 2 && loaded.cr3Genes[0].uid == 12,
              "Repeated/aliased NG+ failed");
        check(loaded.load("CR3-U save 1\narea t0108\n") && loaded.cr3NewGamePlus == 0, "Older save inherited NG+ marker");
        std::cout << "New Game Plus: eligibility, carryover, story reset, source preservation, round trip and repeat "
                     "passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
