Chaos Rings III (CR3-U): your own fonts, if you want them - .ttf, .otf or .ttc files put here.

Leave this folder empty and the game draws its text in your copy's own font (TB Gothic - the
Spanish fan translation's own while it's the language in use), with a system font (Noto Sans CJK,
Malgun Gothic, Microsoft YaHei, PingFang) for the characters it lacks.

With fonts here:
- the first by file name draws all the text, in every language (a translation's own font no
  longer replaces it);
- the others stand in for the characters the first lacks (Japanese, Korean, Chinese ...), in
  file-name order, ahead of the system's.
A file that isn't a font is left out. A font far wider than the original can run long lines off the
message windows. --font <file> and the CR3_FONT environment variable still come first.
The log (cr3view.log in the profile folder) names the font/ files it used.
