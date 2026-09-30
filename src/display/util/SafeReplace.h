#ifndef GM_SAFE_REPLACE_H
#define GM_SAFE_REPLACE_H

#include <ArduinoJson.h>
#include <FS.h>
#include <WString.h>

// Replacing a file without losing both versions (gm-bzu.7).
//
// A save writes the whole document to `target.tmp` first. Committing it used to
// remove `target` and then rename the temporary file over it, because FAT (the
// SD card) refuses to rename onto an existing name. If that rename failed, the
// error branch removed the temporary file too, so neither version survived; a
// power cut between the remove and the rename left only the `.tmp`, which
// nothing ever picked up again.
//
// commitReplace keeps the old file as `target.bak` until the new one is in
// place, rolls the old one back if the rename fails, and leaves a complete
// `.tmp` behind rather than deleting it. recoverReplace runs at startup over a
// directory and settles whatever an interrupted commit left. Every file it
// keeps or promotes must parse as JSON (all users store JSON):
//   .bak and target       -> the commit landed if the target parses; drop the
//                            .bak (and any .tmp). If the target does not parse
//                            and the .bak does, roll the .bak back
//   .bak, no target, .tmp -> promote the .tmp if it parses, drop the .bak; a
//                            .tmp that does not parse is dropped and the .bak
//                            rolled back; a .tmp that parses but will not
//                            rename is rolled back past, and kept if the
//                            rollback fails too
//   .bak, no target       -> roll the .bak back to target
//   .tmp and target       -> the write was interrupted before the commit; the
//                            .tmp may be short; drop it
//   .tmp alone            -> a first save that never committed. Promote it if
//                            it parses; drop it if it does not; if it parses
//                            and only the rename failed, keep it (it is the
//                            only copy) and retry at the next startup
//
// A full filesystem does not show up where a writer looks for it. print()
// reports every byte as written because the 4 KB stdio buffer takes them,
// the ENOSPC comes out of the flush inside fclose, and File::close() drops
// fclose's result. writeJson writes, flushes, syncs and closes with every
// result checked, then reads the file back, so a caller commits only a file
// that is really on disk.
namespace saferep {

// Serialises doc to path, overwriting it, and returns true only when the
// whole document is on disk: every write, the flush, the sync and the close
// succeeded, and the file read back has the expected size and parses. On
// false it has logged (with `tag`) and removed path.
bool writeJson(fs::FS &fs, const String &path, const JsonDocument &doc, const char *tag);

// Moves tmpPath over target, keeping the previous target as target.bak until
// the move lands. Returns false and logs (with `tag`) when the move failed;
// then target holds the previous version when there was one, and tmpPath is
// left in place for the next startup's recovery.
bool commitReplace(fs::FS &fs, const String &tmpPath, const String &target, const char *tag);

// Settles leftover `<name><suffix>.bak` and `<name><suffix>.tmp` files under
// dir per the table above. `suffix` is the target extension, e.g. ".json".
void recoverReplace(fs::FS &fs, const String &dir, const char *suffix, const char *tag);

} // namespace saferep

#endif // GM_SAFE_REPLACE_H
