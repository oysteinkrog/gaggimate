#ifndef GM_SAFE_REPLACE_H
#define GM_SAFE_REPLACE_H

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
// directory and settles whatever an interrupted commit left:
//   .bak and target       -> the commit landed; drop the .bak
//   .bak, no target, .tmp -> the .tmp is complete (it was before the old file
//                            was renamed); promote it, drop the .bak
//   .bak, no target       -> roll the .bak back to target
//   .tmp and target       -> the write was interrupted before the commit; the
//                            .tmp may be short; drop it
//   .tmp alone            -> a first save that never committed; keep it only
//                            if it parses as JSON (all users store JSON)
namespace saferep {

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
