#include "assets/addons.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

#include "assets/mbe.hpp"
#include "battle/cr3battle.hpp"
#include "db/cr3db.hpp"

namespace cr3 {

namespace {

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool number(const std::string& s, int& out) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end) return false;
    out = int(v);
    return true;
}

// a manifest's "key = value" lines ('#' starts a comment line); false: one isn't
bool readManifest(const std::string& path, std::map<std::string, std::string>& out, std::string& why) {
    std::ifstream in(path);
    if (!in) return why = "can't be read", false;
    std::string line;
    for (int n = 1; std::getline(in, line); ++n) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) return why = "line " + std::to_string(n) + ": not key = value", false;
        std::string value = trim(line.substr(eq + 1));
        if (const size_t hash = value.find(" #"); hash != std::string::npos) value = trim(value.substr(0, hash));
        out[lower(trim(line.substr(0, eq)))] = value;
    }
    return true;
}

// the rows the add-ons add to one of the release's tables
struct NewRow {
    std::string table;
    int like = 0, id = 0;
    const char* layout = "";
    std::vector<std::pair<size_t, int32_t>> ints;
    std::vector<std::pair<size_t, std::string>> strings;
};

} // namespace

void Cr3Addons::load(Database& release, const std::string& folder) {
    list_.clear(), problems_.clear();
    std::error_code ec;
    if (folder.empty() || !std::filesystem::is_directory(folder, ec)) return;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec))
        if (e.is_regular_file(ec) && lower(e.path().extension().string()) == ".ini") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) return;
    Cr3BattleTables t;
    std::string err;
    if (!t.load(release, &err)) {
        problems_.push_back("the release's tables: " + err);
        return;
    }
    std::map<std::string, std::vector<NewRow>> rows; // (by file: data\item_para.mbe ...)
    for (const auto& path : files) {
        const std::string file = path.filename().string();
        const auto bad = [&](const std::string& why) { problems_.push_back(file + ": " + why); };
        std::map<std::string, std::string> m;
        std::string why;
        if (!readManifest(path.string(), m, why)) {
            bad(why);
            continue;
        }
        Cr3Addon a;
        a.manifest = path.string();
        const std::string kind = lower(m["kind"]);
        if (!number(m["id"], a.id) || a.id < 1 || a.id > 999) {
            bad("id: 1 .. 999");
            continue;
        }
        if (kind != "outfit" && kind != "weapon") {
            bad("kind: outfit or weapon");
            continue;
        }
        a.weapon = kind == "weapon";
        if (std::any_of(list_.begin(), list_.end(), [&](const Cr3Addon& o) { return o.id == a.id && o.weapon == a.weapon; })) {
            bad("id " + std::to_string(a.id) + ": another " + kind + "'s");
            continue;
        }
        MbeFile::Row c, md;
        if (!number(m["character"], a.character) || a.character <= 0 || a.character > 30 || !t.character.find("Char", a.character, "ii", c) ||
            !t.model.find("Model", c.i(1), "iippffiffii", md) || md.s(2).empty()) {
            bad("character: no such character");
            continue;
        }
        a.wearer = md.s(2);
        if (a.weapon == false && !(md.i(10) & 1)) {
            bad("character: " + a.wearer + " has no body to dress (not a parts model)");
            continue;
        }
        a.name = m["name"], a.nameJa = m.count("name_ja") ? m["name_ja"] : a.name;
        if (a.name.empty()) {
            bad("name: none");
            continue;
        }
        a.model = (path.parent_path() / m["model"]).string();
        if (m["model"].empty() || !std::filesystem::is_regular_file(a.model, ec)) {
            bad("model: " + (m["model"].empty() ? std::string("none") : a.model + " isn't there"));
            continue;
        }
        const int group = a.weapon ? 1 : 2;
        const char* table = a.weapon ? "Weapon" : "Armor";
        if (m.count("like") && !m["like"].empty()) {
            if (!number(m["like"], a.like)) {
                bad("like: an item's id");
                continue;
            }
            if (a.like < 100000) a.like += group * 100000;
        } else {
            a.like = group * 100000 + (a.weapon ? t.weaponOf(a.character, nullptr) : t.defaultArmour(a.character));
        }
        MbeFile::Row like;
        if (a.like / 100000 != group || !t.item.find(table, a.like % 100000, "iiiiiiiiiiiii", like)) {
            bad("like: " + std::to_string(a.like) + " isn't " + (a.weapon ? "a weapon" : "an armour"));
            continue;
        }
        a.icon = like.i(3);
        a.price = 50;
        if (m.count("price") && (!number(m["price"], a.price) || a.price < 0)) {
            bad("price: a number");
            continue;
        }
        if (m.count("currency")) {
            const std::string cur = lower(m["currency"]);
            a.currency = cur == "gp" ? "GP" : cur;
            if (a.currency != "coin" && a.currency != "GP" && a.currency != "gold") {
                bad("currency: coin, GP or gold");
                continue;
            }
        }
        if (m.count("shop")) a.shop = m["shop"];
        {
            const std::string hd = lower(m["hd_models"]);
            a.hdAny = hd == "yes" || hd == "true" || hd == "1" || hd == "any";
            if (!a.hdAny && !hd.empty() && hd != "no" && hd != "false" && hd != "0") {
                std::istringstream words(hd);
                for (std::string w; words >> w;) {
                    if (const size_t dot = w.rfind('.'); dot != std::string::npos) w.resize(dot); // (j001b01.glb: j001b01)
                    if (!w.empty() && w.back() == ',') w.pop_back();
                    if (!w.empty()) a.hdFits.push_back(w);
                }
            }
        }
        const int id = 9000 + a.id;
        a.item = group * 100000 + id;
        char name[32];
        if (a.weapon) {
            MbeFile::Row am;
            if (!t.model.find("AttachmentModel", like.i(12), "ipp", am) || am.s(1).empty()) {
                bad("like: " + std::to_string(a.like) + " has no model to make it over");
                continue;
            }
            a.base = am.s(1), a.variant = am.s(2);
            std::snprintf(name, sizeof name, "wa%03d", a.id);
            a.figure = name;
            rows["data\\item_para.mbe"].push_back({"Weapon", a.like % 100000, id, "iiiiiiiiiiiii", {{1, a.item}, {11, 1 << a.character}, {12, id}}, {}});
            rows["data\\model.mbe"].push_back({"AttachmentModel", like.i(12), id, "ipp", {}, {{1, a.figure}}});
        } else {
            a.base = t.body(a.wearer, a.like % 100000);
            a.variant = "a";
            std::snprintf(name, sizeof name, "%sb%d", a.wearer.c_str(), 100 + a.id);
            a.figure = name;
            rows["data\\item_para.mbe"].push_back({"Armor", a.like % 100000, id, "iiiiiiiiiiiii", {{1, a.item}, {11, 1 << a.character}, {12, 100 + a.id}}, {}});
        }
        if (!release.contains(a.base + ".geom")) {
            bad(a.base + ".geom isn't in the release");
            continue;
        }
        rows["data\\item_name.mbe"].push_back({a.weapon ? "WeaponName" : "ArmorName", a.like % 100000, id, "ipp", {}, {{1, a.nameJa}, {2, a.name}}});
        // its figure: the base's files under its name (a body's motions come by motionBase; a
        // weapon's own animation is <base>.anim)
        bool aliased = true;
        for (const char* ext : {".geom", ".skel", ".name"})
            if (release.contains(a.base + ext)) aliased &= release.addAlias(a.figure + ext, a.base + ext);
        if (a.weapon)
            for (const auto& e : std::vector<Database::Entry>(release.entries()))
                if (e.name.size() > a.base.size() && e.name.compare(0, a.base.size(), a.base) == 0 && (e.name[a.base.size()] == '.' || e.name[a.base.size()] == '_') &&
                    e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, ".anim") == 0)
                    release.addAlias(a.figure + e.name.substr(a.base.size()), e.name);
        if (!aliased) {
            bad(a.figure + ": already in the release");
            continue;
        }
        list_.push_back(a);
    }
    for (auto& [file, add] : rows) {
        auto shared = std::make_shared<std::vector<NewRow>>(std::move(add));
        release.setEdit(file, [shared, file = file](std::vector<uint8_t>& bytes, std::string* error) {
            MbeFile f;
            if (!f.parse(bytes, error)) return false;
            for (const NewRow& r : *shared) {
                int like = r.like;
                MbeFile::Row probe;
                const MbeFile::Table* tb = f.table(r.table);
                if (tb && !tb->rows.empty() && !f.find(r.table, like, r.layout, probe) && f.row(*tb, 0, r.layout, probe)) like = probe.i(0); // (a name the like item lacks: any row's)
                std::string why;
                if (!f.appendRow(r.table, like, r.id, r.layout, r.ints, r.strings, &why)) {
                    std::fprintf(stderr, "add-ons: %s: %s\n", file.c_str(), why.c_str());
                    return true; // (the file as it was)
                }
            }
            bytes = f.bytes();
            return true;
        });
    }
}

const Cr3Addon* Cr3Addons::byItem(int fullId) const {
    for (const Cr3Addon& a : list_)
        if (a.item == fullId) return &a;
    return nullptr;
}

const Cr3Addon* Cr3Addons::byFigure(const std::string& figure) const {
    const std::string f = lower(figure);
    for (const Cr3Addon& a : list_)
        if (a.figure == f) return &a;
    return nullptr;
}

void Cr3Addons::addGoods(Cr3OfflineDb& db) const {
    for (const Cr3Addon& a : list_)
        if (!a.shop.empty() && lower(a.shop) != "none")
            db.addGoods(a.shop, (a.weapon ? 10000 : 11000) + a.id, a.item, a.price, a.currency, a.icon);
}

bool Cr3Addons::ownModel(const Cr3Addon& a, const std::vector<std::string>& replaced) {
    if (a.weapon || replaced.empty() || a.hdAny) return true; // (a weapon's model is its own whatever wears it)
    for (const std::string& r : replaced)
        if (std::find(a.hdFits.begin(), a.hdFits.end(), lower(r)) == a.hdFits.end()) return false;
    return true;
}

} // namespace cr3
