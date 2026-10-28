// Synthetic, generated color swatches only; no game artwork is included.
#include "assets/card_art.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <zlib.h>
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using Files = std::map<std::string, Bytes>;
void check(bool good, const char* what) { if (!good) throw std::runtime_error(what); }
void put(Bytes& b, size_t at, uint32_t value, int n = 4) { for (int k = 0; k < n; ++k) b.at(at + k) = uint8_t(value >> (k * 8)); }
Bytes atlas(int color, bool bottomUp = false) {
    const int data = bottomUp ? 67 : 52;
    Bytes b(data + 8); std::memcpy(b.data(), "PVR\x03", 4); std::memcpy(b.data() + 8, "rgba", 4);
    put(b, 12, 0x08080808); put(b, 24, 2); put(b, 28, 1);
    if (bottomUp) { put(b,48,15);std::memcpy(b.data()+52,"PVR\x03",4);put(b,56,3);put(b,60,3);b[65]=1; }
    for(int row=0;row<2;++row){ b[data+row*4]=uint8_t(color+(bottomUp?1-row:row));b[data+row*4+1]=7;b[data+row*4+2]=11;b[data+row*4+3]=255; }
    return b;
}
Bytes picture(int color) { // synthetic one-pixel PNG, detected by content rather than its filename
    Bytes out{137,80,78,71,13,10,26,10};
    auto be = [](Bytes& b, uint32_t v) { for (int n = 3; n >= 0; --n) b.push_back(uint8_t(v >> (8*n))); };
    auto chunk = [&](const char* type, const Bytes& data) {
        be(out, uint32_t(data.size()));
        const size_t start = out.size();
        out.insert(out.end(), type, type + 4); out.insert(out.end(), data.begin(), data.end());
        be(out, uint32_t(crc32(0, out.data() + start, uInt(data.size() + 4))));
    };
    chunk("IHDR", Bytes{0,0,0,1,0,0,0,1,8,6,0,0,0});
    Bytes scanline{0,uint8_t(color),7,11,255}, compressed(compressBound(5));
    uLongf size = compressed.size(); check(compress(compressed.data(), &size, scanline.data(), 5) == Z_OK, "PNG compression");
    compressed.resize(size); chunk("IDAT", compressed); chunk("IEND", {});
    return out;
}
void addPair(Files& files, int series, int color) {
    char name[40]; std::snprintf(name, sizeof name, "images\\card_%03d.img", series); files[name] = atlas(color);
    std::snprintf(name, sizeof name, "%03d.jpg", series); files[name] = picture(color);
}
void archive(const fs::path& path, const Files& files) {
    const size_t n = files.size(), start = 20 + n * 84;
    Bytes bytes(start); std::memcpy(bytes.data(), "MDB1", 4);
    put(bytes, 4, uint32_t(n), 2); put(bytes, 6, uint32_t(n), 2); put(bytes, 8, uint32_t(n)); put(bytes, 12, uint32_t(start));
    size_t k = 0;
    for (const auto& file : files) {
        put(bytes, 20 + k * 8 + 2, uint32_t(k), 2);
        const size_t dot = file.first.rfind('.'), name = 20 + n * 8 + k * 64, data = 20 + n * 72 + k * 12;
        const auto ext = file.first.substr(dot + 1), base = file.first.substr(0, dot);
        std::memcpy(bytes.data() + name, ext.data(), ext.size()); std::memcpy(bytes.data() + name + 4, base.data(), base.size());
        put(bytes, data, uint32_t(bytes.size() - start)); put(bytes, data + 4, uint32_t(file.second.size())); put(bytes, data + 8, uint32_t(file.second.size()));
        bytes.insert(bytes.end(), file.second.begin(), file.second.end()); ++k;
    }
    fs::create_directories(path.parent_path()); std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
int selected(cr3::CardArtwork& art, cr3::Database& db, int series, bool full) {
    Bytes b; cr3::Image im; char name[40];
    if (full) {
        std::snprintf(name, sizeof name, "%03d.jpg", series);
        check((art.readPicture(series, b) || db.read(name, b)) && cr3::decodePicture(b, im), "full-screen picture unavailable");
    } else if (!art.decodeTexture(series, im)) {
        std::snprintf(name, sizeof name, "images\\card_%03d.img", series);
        check(db.read(name, b) && cr3::decodeImg(b, im), "menu picture unavailable");
    }
    return im.rgba.at(0);
}
void donor(const fs::path& path, bool vita, int color, bool full = true, bool valid = true) {
    const int series = vita ? 119 : 160;
    char tex[40], jpg[20]; std::snprintf(tex, sizeof tex, "images\\card_%03d.img", series); std::snprintf(jpg, sizeof jpg, "%03d.jpg", series);
    const std::string suffix = vita ? ".psp2.mvgl" : ".ios.mvgl";
    archive(path / ("CRDBcard01" + suffix), {{tex, atlas(color, vita)}});
    if (full) archive(path / ("CRDBcardhd" + suffix), {{jpg, valid ? picture(color) : Bytes{1,2,3}}});
}
int main() {
    const fs::path root = fs::temp_directory_path() / ("cr3-art-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Files files; for (int s : {42,49,77,119,124,131}) addPair(files, s, s % 100);
        archive(root / "mobile/main.test.android.mvgl", files);
        cr3::Database db; check(db.open((root / "mobile").string()), "synthetic Android database");
        cr3::CardArtwork art; art.configure(db, {});
        check(!art.enabled() && selected(art,db,131,false)==31, "default must preserve release artwork");
        check(art.setEnabled(true) && art.available()==2, "Android partial availability");
        check(art.originalName(330,false)=="Little Red Riding Hood" && art.originalName(351,false)=="Aphrodite", "names must match restored cards");
        check(art.originalName(450,false).empty() && art.originalDescription(450,0,false).empty(), "missing art keeps release name and profile");
        for (int master : {328,329,330,349,350,351}) for (int rarity=0;rarity<3;++rarity) {
            const auto jp=art.originalDescription(master,rarity,true), en=art.originalDescription(master,rarity,false);
            check(!jp.empty() && !en.empty() && jp!=en, "original N/R/SR profiles need both languages");
        }
        check(art.originalName(77,false).empty() && art.originalDescription(77,0,false).empty() && art.originalDescription(330,3,false).empty(), "unrelated genes and unsupported rarities retain release text");
        for (bool full : {false,true}) {
            check(selected(art,db,131,full)==42 && selected(art,db,124,full)==49, "both remapped cards in both views");
            check(selected(art,db,119,full)==19 && selected(art,db,77,full)==77, "missing and unaffected art must retain release files");
        }
        art.setEnabled(false); check(art.originalName(330,false).empty() && art.originalDescription(330,0,false).empty(), "Off restores release metadata"); check(selected(art,db,131,false)==31 && selected(art,db,131,true)==31, "Off restores both views");
        // New files are discovered on re-enabling; only the two card archives are needed.
        const auto extra = root / "artwork/PCSG00500/CR3/resources";
        art.configure(db,{(root/"artwork").string()}); art.setEnabled(true); check(art.available()==2,"empty artwork folder");
        art.setEnabled(false); donor(extra,true,99); const bool wide = cr3::gWideAnimations;
        art.setEnabled(true); check(art.available()==3 && cr3::gWideAnimations==wide,"Vita donor must not switch Android animation parsing");
        check(art.originalName(450,false)=="Adam" && art.originalDescription(450,1,false).find("sunrise")!=std::string::npos, "restored Adam art must use Adam metadata");
        check(selected(art,db,119,false)==99 && selected(art,db,119,true)==99,"Vita original in both views");
        donor(root/"incomplete",true,88,false); art.configure(db,{(root/"incomplete").string()}); art.setEnabled(true);
        check(art.available()==2 && selected(art,db,119,false)==19,"do not mix menu and full-screen variants");
        donor(root/"corrupt/vita",true,88,true,false); donor(root/"corrupt/ios",false,60);
        art.configure(db,{(root/"corrupt").string()}); art.setEnabled(true);
        check(art.available()==3 && selected(art,db,119,false)==60 && selected(art,db,119,true)==60,"skip corrupt donor and use iOS original 160");
        Files ios=files;addPair(ios,160,61);archive(root/"ios/CRDB.ios.mvgl",ios);
        cr3::Database ipad;check(ipad.open((root/"ios").string()),"synthetic iOS database");
        art.configure(ipad,{(root/"artwork").string()});art.setEnabled(true);
        check(art.available()==3 && selected(art,ipad,119,false)==61 && selected(art,ipad,119,true)==61,"prefer original already retained in iOS");
        Files bad=files;bad["042.jpg"]={1,2,3};archive(root/"bad/main.test.android.mvgl",bad);
        cr3::Database broken;check(broken.open((root/"bad").string()),"synthetic incomplete primary");
        art.configure(broken,{});art.setEnabled(true);
        check(selected(art,broken,131,false)==31 && selected(art,broken,131,true)==31,"corrupt originals must not blank either view");
        check(cr3::CardArtwork::textureSeries("card_131")==131 && cr3::CardArtwork::textureSeries("card_rar_02")==-1 && cr3::CardArtwork::textureSeries("card_1xx")==-1,"only gene-picture textures are intercepted");
        for (const auto& entry : files) { Bytes b;check(db.read(entry.first,b) && b==entry.second,"art option altered primary data"); }
        db.close(); ipad.close(); broken.close(); // Windows cannot remove archives while they are open.
        fs::remove_all(root);std::cout<<"Original artwork: both views, partial availability, donors, malformed assets, toggle refresh and release isolation passed\n";return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';std::error_code ec;fs::remove_all(root,ec);return 1;}
}
