# CR3 unreadable-save handling

The native title treated a failed existing load as an empty slot. A synthetic
save naming an unavailable area reproduced the complete failure: loading failed,
the prologue started, and its pending autosave replaced the original bytes.
This was reproduced only in an isolated temporary profile.

The title now distinguishes occupied paths from empty slots and returns to the
file menu when an existing load fails, with autosaving disabled and a notice
that the original file was kept. Unreadable slots display "Unreadable save"
using the native occupied panel. They cannot be used as Copy File or New Game
Plus destinations. They remain available for deliberate deletion through both
existing confirmations. Copy requires a readable source and genuinely empty
space.

`viewer-save-slots` tests selection and both delete confirmations without doing
filesystem writes. `CR_SELFTEST_BAD_SAVE=1` exercises the real native title with
a synthetic unavailable-area save, attempts any erroneously scheduled autosave,
and verifies the original bytes and return to the menu. Native and GLES
ASan/UBSan runs pass with Android, iOS and Japanese Vita data (six runs). All 18
CR3 CTests pass in both builds. The Android 960x640 file-menu capture was checked
visually: the unreadable label fits and the native empty/new-game panels remain
unchanged. No player saves were touched.
