#pragma once
// CR3-U's add-ons: outfits and weapons of your own - new ones, beside the game's (HD Models only
// replaces those). Each is a manifest, models/addons/<anything>.ini, and the glTF model it names
// (docs/FORMATS.md "Add-ons"):
//
//   id = 1                 1 .. 999, its own for good: the saves keep what's held by it
//   kind = outfit          outfit (an armour that dresses the body) or weapon
//   character = 1          who wears it (char_para: 1 the hero, ...)
//   name = Night Coat      its name in the lists (name_ja = for Japanese; else this)
//   model = coat.glb       beside the manifest - made over the like item's figure (its export)
//   like = 200083          the item it is made from: its numbers, icon, body or weapon figure
//                          (default: the character's own kit's)
//   price = 30             in the shop (`shop`, default com_1 - the premium shop)
//   currency = coin        coin, GP or gold
//   hd_models = no         yes: worn, too, by a character HD Models replaces; or the replaced
//                          figures it is made to fit (j001b01 j001h01)
//
// It becomes the game's own: a row of item_para's Armor / Weapon (a copy of the like item's - id
// 9000 + id, the full id 209000 / 109000 + id, only the character may wear it, its model a new
// one), item_name's, for a weapon model.mbe's AttachmentModel, and goods in the shop's list, all
// served by the release (Database::setEdit) to whatever reads them. Its figure - an outfit
// <model>b<100 + id> (j001b101), a weapon wa<id> - is the like item's (Database::addAlias: its
// skeleton, animations - a body's the b01 body's motions -, materials) with the add-on's meshes
// (applyFigureReplacement) in place of its own.

#include <string>
#include <vector>

#include "assets/mvgl.hpp"

namespace cr3 {

class Cr3OfflineDb;

struct Cr3Addon {
    std::string manifest;     // (its path)
    int id = 0;               // 1 .. 999
    bool weapon = false;      // else an outfit
    int character = 0;        // char_para's
    std::string name, nameJa;
    std::string model;        // the glTF file's path
    int like = 0;             // the item it is made from (its full id)
    int price = 0;
    std::string currency = "coin", shop = "com_1";
    bool hdAny = false;               // (hd_models = yes)
    std::vector<std::string> hdFits;  // (hd_models = the figures it fits)
    // set by Cr3Addons::load
    int item = 0;             // its full id (209000 + id, 109000 + id)
    int icon = 0;             // (the like item's: item_para +0xc)
    std::string wearer;       // the character's model (j001)
    std::string figure;       // what's drawn (j001b101, wa001)
    std::string base;         // the figure it is made over (j001b02, w001_01)
    std::string variant;      // (a weapon's texture variation: the like weapon's)
};

class Cr3Addons {
public:
    // reads `folder`'s manifests (*.ini) and sets `release` up with them - after its overlay is
    // added, before anything reads its tables; those that can't be (no such item, character or
    // figure; an id twice) are left out, each with a line in problems()
    void load(Database& release, const std::string& folder);
    const std::vector<Cr3Addon>& list() const { return list_; }
    const std::vector<std::string>& problems() const { return problems_; }
    const Cr3Addon* byItem(int fullId) const;
    const Cr3Addon* byFigure(const std::string& figure) const; // (its name, no ".geom")
    // the goods on their shops' lists (after Cr3OfflineDb::load)
    void addGoods(Cr3OfflineDb& db) const;
    // whether it may be used while HD Models replaces `replaced` - the wearer's figures with a
    // replacement (none: always)
    static bool usable(const Cr3Addon& a, const std::vector<std::string>& replaced);

private:
    std::vector<Cr3Addon> list_;
    std::vector<std::string> problems_;
};

} // namespace cr3
