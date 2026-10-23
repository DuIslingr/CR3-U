#pragma once
// The game's text (`message\*.dat`: EvtTextData, EvtWindowMessage::ReadPage; docs/FORMATS.md,
// tools/msg.py): numbered messages (from 1), each in the release's languages - CR3's two (0
// Japanese, 1 English; CR2's had 8: the record's size tells), UTF-8 with backslash commands for the window - pages, lines, centring,
// colours, the speaker's tag, waits. A message's speaker tag (`\d001`) names one of the 144
// (CR3; CR2 had 21) whose names are `message\name_tag.dat`'s (EvtWindowMessage::GetNameTagNumber:
// the tag's place in the table, + 1).

#include <cstdint>
#include <string>
#include <vector>

namespace cr3 {

class TextData {
public:
    bool parse(const std::vector<uint8_t>& data);
    int count() const { return count_; }
    int languages() const { return languages_; }
    // message n (from 1) in a language, raw (its commands in it); "" when there is none
    std::string message(int n, int language = 1) const;
    // its voice (the trailer's ids; -1 none)
    int voice(int n) const;

private:
    std::vector<uint8_t> data_;
    uint32_t records_ = 0, strings_ = 0;
    int count_ = 0, languages_ = 0;
    std::vector<int> voices_;
};

struct TextLine {
    std::string text; // UTF-8, the commands taken out
    bool centred = false;
    int colour = 0;   // \#<digit>: 1 red, 2 (0, .8, 1), 3 green, 4 (1, .8, 0), 5 magenta, 6 white,
                      // 7 black; 0 (and else) white
};
// a pause in a page's typing (EvtWindowPage::NextBlock): \@ waits for a tap (EvtWindow::UpdatePage,
// 0.1 s on), \w<n> n / 30 s (CR3's UpdatePage: its count against the time x 30) and goes on by
// itself. (A page's end waits for a tap anyway - ReadPage adds a \@ to each but a \* one -, so its
// own \@ there is left out.)
struct TextStop {
    int at = 0;   // the page's code points typed before it
    int wait = 0; // \w's n; 0 \@ (a tap)
};
struct TextPage {
    std::vector<TextLine> lines;
    std::vector<TextStop> stops;
};
struct ParsedMessage {
    std::vector<TextPage> pages;
    int speaker = -1; // the tag's index (0..143) in the game's table; -1 none
    bool ends = false;   // \*: its last page ends it once typed, without a tap (and no cursor)
    bool noSkip = false; // \m: no fast-forward (EvtWindow::ShowMessage: SetSkipButtonEnable, SetPageSkipEnable false)
};

// a message's pages (\f ends one, \n a line, \* the message), their stops, its speaker
ParsedMessage parseMessage(const std::string& text);

// a colour number's RGB
void textColour(int colour, float rgb[3]);

} // namespace cr3
