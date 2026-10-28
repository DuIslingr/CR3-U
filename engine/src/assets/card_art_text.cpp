// Original card identities and N/R/SR profiles, verified against unpatched Japanese
// PCSG00457 data. English translations are local to this option: mobile/fan-patch
// labels describe different characters. No other localization is replaced.
#include "assets/card_art.hpp"
namespace cr3 {
namespace {
struct Text {
    int first, last;
    const char* names[2]; // Japanese, English (also used by fan-English releases)
    const char* profiles[2][3];
};
const Text texts[] = {
    {328, 330, {u8"\u30a2\u30ab\u30ba\u30ad\u30f3", u8"Little Red Riding Hood"},
        {
            {u8"\u3053\u3093\u306b\u3061\u306f\u3001\u30aa\u30aa\u30ab\u30df\u3055\u3093\u3002\\n\u79c1\u306f\u5bc4\u308a\u9053\u3092\u3057\u3066\u304b\u3089\u901d\u304d\u307e\u3059\u308f\u3002",
             u8"\u3053\u3093\u3070\u3093\u306f\u3001\u304a\u3070\u3042\u3055\u3093\u3002\\n\u3082\u3046\u3059\u3050\u304a\u8fce\u3048\u304c\u53c2\u308a\u307e\u3059\u308f\u3002",
             u8"\u305f\u3060\u3044\u307e\u3001\u304a\u6bcd\u3055\u3093\u3002\\n\u79c1\u304c\u6b7b\u3093\u3067\u3044\u306a\u304f\u3066\u6b8b\u5ff5\u3067\u3059\u304b\u3002"},
            {u8"Hello, Mr. Wolf.\\nI'll take a detour before I die.",
             u8"Good evening, Grandmother.\\nSomeone will be coming\\nfor you soon.",
             u8"I'm home, Mother.\\nAre you disappointed\\nthat I'm still alive?"},
        }},
    {349, 351, {u8"\u30a2\u30d5\u30ed\u30c7\u30a3\u30fc\u30c6", u8"Aphrodite"},
        {
            {u8"\u7f8e\u3057\u3055\u3092\u7f6a\u3068\u8003\u3048\u308b\u4eba\u304c\u3044\u308b\u3002\\n\u305d\u306e\u7f6a\u306b\u5bfe\u3057\u3066\u7f70\u3092\u4e0e\u3048\u308b\u795e\u3082\u3044\u308b\u3002",
             u8"\u6ce1\u304b\u3089\u751f\u307e\u308c\u305f\u3068\u8a00\u308f\u308c\u308b\u3001\\n\u7f8e\u3068\u8c4a\u7a63\u3092\u53f8\u308b\u5973\u795e\u30a2\u30d5\u30ed\u30c7\u30a3\u30fc\u30c6\u3002\\n\u795e\u3067\u3042\u308a\u306a\u304c\u3089\u3082\u5973\u3068\u3057\u3066\u306e\u30d7\u30e9\u30a4\u30c9\u3092\\n\u5f37\u304f\u6301\u3063\u3066\u304a\u308a\u3001\\n\u5973\u795e\u540c\u58eb\u306e\u8acd\u3044\u304c\u539f\u56e0\u3067\u4eba\u754c\u306e\\n\u6226\u4e89\u3092\u8d77\u3053\u3057\u3066\u3057\u307e\u3046\u7b49\u3001\\n\u554f\u984c\u5150\u3068\u3057\u3066\u306e\u5074\u9762\u304c\u3068\u3066\u3082\u5f37\u3044\u3002",
             u8"\u4e16\u754c\u3067\u5f7c\u5973\u3088\u308a\u7f8e\u3057\u3044\u30e2\u30ce\u306f\u5b58\u5728\u3057\u306a\u3044\u3002\\n\u306a\u305c\u306a\u3089\u5f7c\u5973\u304c\u305d\u308c\u3092\u6d88\u3057\u3066\u3057\u307e\u3046\u304b\u3089\u3060\u3002"},
            {u8"There are those who see beauty\\nas a sin, and gods who punish\\nthat sin.",
             u8"Aphrodite, goddess of beauty\\nand fertility, is said to have\\nbeen born from sea foam.\\nFor all her divinity, she takes\\ngreat pride in her womanhood.\\nHer quarrels with other goddesses\\nhave even caused wars among\\nhumans. She is quite a troublemaker.",
             u8"Nothing in the world is\\nmore beautiful than she is.\\nShe eliminates anything that is."},
        }},
    {448, 450, {u8"\u30a2\u30c0\u30e0", u8"Adam"},
        {
            {u8"\u672a\u5b8c\u6210\u306e\u4e16\u754c\u306b\u5f7c\u306f\u653e\u305f\u308c\u305f\u3002\\n\u672a\u6765\u306f\u307e\u3060\u3001\u7121\u9650\u306e\u53ef\u80fd\u6027\u306b\\n\u6e80\u3061\u3042\u3075\u308c\u3066\u3044\u308b\u3002",
             u8"\u5929\u707d\u3084\u9913\u3048\u306b\u8010\u3048\u306a\u304c\u3089\u3001\\n\u751f\u304d\u308b\u559c\u3073\u3084\u60b2\u3057\u307f\u3092\u77e5\u3063\u3066\u3044\u304f\u3002\\n\u30a2\u30c0\u30e0\u306f\u6607\u308b\u65e5\u3092\u6570\u3048\u308b\u305f\u3073\u306b\\n\u4eba\u9593\u3068\u3057\u3066\u6210\u9577\u3057\u3066\u3044\u3063\u305f\u3002",
             u8"\u3053\u308c\u306f\u59cb\u307e\u308a\u306e\u7d42\u308f\u308a\u3002\\n\u6df7\u6c8c\u306f\u79e9\u5e8f\u3078\u3068\u5909\u308f\u308a\u3086\u304f\u3002"},
            {u8"He was set loose in an\\nunfinished world.\\nThe future still brimmed\\nwith endless possibilities.",
             u8"Enduring disasters and hunger,\\nAdam learned life's joys\\nand sorrows.\\nWith every sunrise he counted,\\nhe grew as a human being.",
             u8"This is the end of the beginning.\\nChaos gives way to order."},
        }},
};
int index(int master) {
    for (int k = 0; k < 3; ++k) if (master >= texts[k].first && master <= texts[k].last) return k;
    return -1;
}
} // namespace
std::string CardArtwork::originalName(int master, bool japanese) const {
    const int k = index(master);
    return enabled_ && k >= 0 && originals_[k].ready() ? texts[k].names[japanese ? 0 : 1] : std::string();
}
std::string CardArtwork::originalDescription(int master, int rarity, bool japanese) const {
    const int k = index(master);
    return enabled_ && k >= 0 && originals_[k].ready() && rarity >= 0 && rarity < 3
        ? texts[k].profiles[japanese ? 0 : 1][rarity] : std::string();
}
} // namespace cr3
