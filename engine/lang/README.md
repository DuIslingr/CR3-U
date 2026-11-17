# The port's own text in other languages

The game's text (its menus, messages, the story) comes from your copy of Chaos Rings III, in the
languages that release has: Japanese and English on the iPhone and Android, Japanese on the Vita (with
the fan translations' English or Spanish over it). These catalogs translate only what CR3-U adds:
- its Options rows, values and help lines: the **Cheats** and **Graphics** tabs, the **Other** tab's
  rows of its own (Menu Language, Vita Saves), the Reset button and its questions;
- the first start's setup dialogs and progress windows, and the "Could not find your copy" box;
- the Vita save import;
- the New Game Plus question on a cleared save.

**Options > Other > Menu Language** picks the language. **Auto** follows the game text's language
(Japanese, English, or Spanish with Joseas's fan translation). Before a language is picked (a first
start), the system's language is used. `CR_UI_LANG=<code>` forces one for a run (tests, captures).

## The files

- `template.pot` lists every string. Each `<code>.po` holds one language: `ja`, `fr`, `de`, `es`, `zh-Hans`,
  `zh-Hant`, `ko`, `ru`.
- They are gettext catalogs: edit them with Poedit, Weblate or a text editor (UTF-8).
- A translation keeps the English's `{0}`, `{1}` ... in any order, and its `\n` line breaks.
- An empty `msgstr` (or one marked fuzzy) shows the English.
- Option labels and values should stay about as short as the English. A help line should fit two lines
  (about 150 Latin or 70 CJK characters).
- A string the game's font (with its fallbacks: the system's CJK font) can't draw is shown in English.

## Trying a translation without building

Put a `<code>.po` into a `lang/` folder:
- in the game folder (beside `cr3view`, or the profile folder's);
- or in the profile folder itself.

It is read in place of the built-in one, and a new code appears in the Menu Language list. Its name comes
from the header's `X-Language-Name:` line.

## Keeping them up to date

`tools/ui_strings.py` collects the strings from the code (`tr("...")`, `trf("...", {...})`, `N_("...")`
under `src/`) into `template.pot`, and brings every `.po` up to date: translations kept, new strings added
untranslated, removed ones dropped.
- `tools/ui_strings.py --new <code> "<name>"` starts a language.
- The `ui-strings` test fails while the catalogs are behind the code.
- `platform/ui_text.{hpp,cpp}` and `tools/ui_strings.py` are shared with CRO-U (and CR1-U, CR2-U):
  a fix there belongs in all of them.

## Who translated what

- **Russian:** the Options' wording CR3-U shares with CRO-U is metastigma's (CRO-U-metastigma PR #51).
- **Everything else:** machine translations, not yet reviewed by native speakers. The first start's
  setup strings are CRO-U's where the English is the same.
- They use Chaos Rings III's own terms where its text has them - in Japanese ジーン, ブレイクゲージ,
  クライマックスアーツ, 連携, 応援, 先制攻撃, バトルモード, 画面方向の固定. The game has no French,
  German, Spanish, Chinese, Korean or Russian text of its own; there the terms follow CRO-U's official
  ones (gène / Gen / gen / 基因 / 진, the Break gauge as that release names it).
- Corrections are welcome.
