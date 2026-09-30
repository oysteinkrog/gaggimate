#include "SafeReplace.h"

#include <ArduinoJson.h>
#include <esp_log.h>

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <unistd.h>
#include <vector>

namespace saferep {

namespace {

// A file is kept or promoted only if it holds a whole JSON document: a power
// cut or a full filesystem mid-write leaves a prefix, which does not parse.
// Deserialised straight from the stream, so the check needs no copy of the
// file.
bool parsesAsJson(fs::FS &fs, const String &path) {
    File f = fs.open(path, "r");
    if (!f) {
        return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    return err == DeserializationError::Ok;
}

// Rolls bak back onto target, removing a target that is in the way (FAT
// refuses to rename onto an existing name). Only called with a bak that
// parses.
bool rollBack(fs::FS &fs, const String &bak, const String &target, const char *tag) {
    if (fs.exists(target) && !fs.remove(target)) {
        ESP_LOGE(tag, "Could not remove %s to roll back its .bak", target.c_str());
        return false;
    }
    if (!fs.rename(bak, target)) {
        ESP_LOGE(tag, "Could not roll %s back from %s", target.c_str(), bak.c_str());
        return false;
    }
    ESP_LOGW(tag, "Rolled %s back from its .bak", target.c_str());
    return true;
}

void recoverOne(fs::FS &fs, const String &target, const char *tag) {
    const String bak = target + ".bak";
    const String tmp = target + ".tmp";
    const bool hasTarget = fs.exists(target);
    const bool hasBak = fs.exists(bak);
    const bool hasTmp = fs.exists(tmp);
    if (hasBak) {
        if (hasTarget) {
            // The .bak goes only once the target is known good: a commit
            // interrupted mid-rename on some filesystems can leave a target
            // entry whose content is not the whole file.
            if (parsesAsJson(fs, target)) {
                fs.remove(bak);
                if (hasTmp) {
                    fs.remove(tmp);
                }
                return;
            }
            ESP_LOGE(tag, "%s does not parse", target.c_str());
            if (parsesAsJson(fs, bak) && rollBack(fs, bak, target, tag)) {
                if (hasTmp) {
                    fs.remove(tmp);
                }
            } else {
                ESP_LOGE(tag, "No valid version of %s to roll back to; leaving the files as they are", target.c_str());
            }
            return;
        }
        if (hasTmp) {
            if (!parsesAsJson(fs, tmp)) {
                ESP_LOGW(tag, "Dropping incomplete %s", tmp.c_str());
                fs.remove(tmp);
                rollBack(fs, bak, target, tag);
                return;
            }
            if (fs.rename(tmp, target)) {
                ESP_LOGW(tag, "Recovered %s from its completed .tmp", target.c_str());
                fs.remove(bak);
                return;
            }
            // The .tmp is whole but will not move. The .bak is the version
            // the interrupted save was replacing, so it goes back; the .tmp
            // is dropped only once that rollback has put a file in place.
            ESP_LOGE(tag, "Could not promote %s (it parses; the rename failed); rolling back the .bak", tmp.c_str());
            if (rollBack(fs, bak, target, tag)) {
                fs.remove(tmp);
            } else {
                ESP_LOGE(tag, "Keeping %s and its .bak for the next startup", tmp.c_str());
            }
            return;
        }
        rollBack(fs, bak, target, tag);
        return;
    }
    if (hasTmp) {
        if (hasTarget) {
            fs.remove(tmp);
            return;
        }
        // No other version exists, so the content check and the rename are
        // separate outcomes: an incomplete .tmp goes, but a whole one that
        // only failed to rename is the sole copy and stays for a retry.
        if (!parsesAsJson(fs, tmp)) {
            ESP_LOGW(tag, "Dropping incomplete %s", tmp.c_str());
            fs.remove(tmp);
        } else if (fs.rename(tmp, target)) {
            ESP_LOGW(tag, "Recovered %s from an uncommitted .tmp", target.c_str());
        } else {
            ESP_LOGE(tag, "Could not rename %s (it parses); keeping it to retry at the next startup", tmp.c_str());
        }
    }
}

#ifndef GAGGIMATE_SIM
// Print over a stdio stream that remembers whether any write came up short,
// so a failure in the middle of the document is not lost behind a later
// write that succeeds.
class StdioPrint : public Print {
  public:
    explicit StdioPrint(FILE *f) : _f(f) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *buf, size_t size) override {
        const size_t n = fwrite(buf, 1, size, _f);
        if (n != size) {
            _failed = true;
        }
        return n;
    }
    bool failed() const { return _failed; }

  private:
    FILE *_f;
    bool _failed = false;
};
#endif

// Writes doc to path with every step checked. Returns false with errno-level
// detail logged; the caller removes the file.
bool writeChecked(fs::FS &fs, const String &path, const JsonDocument &doc, size_t expected, const char *tag) {
#ifndef GAGGIMATE_SIM
    const char *mount = fs.mountpoint();
    if (mount != nullptr) {
        const String full = String(mount) + path;
        FILE *f = fopen(full.c_str(), "w");
        if (f == nullptr) {
            ESP_LOGE(tag, "Could not open %s: %d", path.c_str(), errno);
            return false;
        }
        StdioPrint out(f);
        const size_t written = serializeJson(doc, out);
        bool ok = true;
        if (out.failed() || written != expected) {
            ESP_LOGE(tag, "Wrote %u of %u bytes to %s: %d", (unsigned)written, (unsigned)expected, path.c_str(), errno);
            ok = false;
        }
        // The buffered tail is where a full filesystem reports: flush and
        // sync while the result can still be read, then check the close too.
        if (ok && fflush(f) != 0) {
            ESP_LOGE(tag, "Flushing %s failed: %d", path.c_str(), errno);
            ok = false;
        }
        if (ok && fsync(fileno(f)) != 0) {
            ESP_LOGE(tag, "Syncing %s failed: %d", path.c_str(), errno);
            ok = false;
        }
        if (fclose(f) != 0 && ok) {
            ESP_LOGE(tag, "Closing %s failed: %d", path.c_str(), errno);
            ok = false;
        }
        return ok;
    }
#endif
    // No mount point (the simulator's FS shim): File hides the flush and close
    // results, so this path relies on the read-back check alone.
    File file = fs.open(path, "w");
    if (!file) {
        ESP_LOGE(tag, "Could not open %s", path.c_str());
        return false;
    }
    const size_t written = serializeJson(doc, file);
    file.flush();
    file.close();
    if (written != expected) {
        ESP_LOGE(tag, "Wrote %u of %u bytes to %s", (unsigned)written, (unsigned)expected, path.c_str());
        return false;
    }
    return true;
}

} // namespace

bool writeJson(fs::FS &fs, const String &path, const JsonDocument &doc, const char *tag) {
    const size_t expected = measureJson(doc);
    bool ok = writeChecked(fs, path, doc, expected, tag);
    if (ok) {
        // Read back through the filesystem: this is the check that holds
        // whatever the layer below reported.
        File f = fs.open(path, "r");
        const size_t onDisk = f ? f.size() : 0;
        if (f) {
            f.close();
        }
        if (onDisk != expected) {
            ESP_LOGE(tag, "%s holds %u of %u bytes after the write", path.c_str(), (unsigned)onDisk, (unsigned)expected);
            ok = false;
        } else if (!parsesAsJson(fs, path)) {
            ESP_LOGE(tag, "%s does not parse after the write", path.c_str());
            ok = false;
        }
    }
    if (!ok) {
        fs.remove(path);
    }
    return ok;
}

bool commitReplace(fs::FS &fs, const String &tmpPath, const String &target, const char *tag) {
    const String bak = target + ".bak";
    if (fs.exists(bak)) {
        // Startup recovery settles these; one still here means a commit was
        // interrupted since boot. The stale copy goes when the current target
        // parses, or when it does not parse itself; otherwise it is the last
        // good version and goes back in place first, so the commit below sets
        // it aside as usual.
        if (fs.exists(target) && parsesAsJson(fs, target)) {
            fs.remove(bak);
        } else if (!parsesAsJson(fs, bak)) {
            ESP_LOGW(tag, "Dropping unreadable %s", bak.c_str());
            fs.remove(bak);
        } else if (!rollBack(fs, bak, target, tag)) {
            ESP_LOGE(tag, "Could not restore %s from its .bak; refusing the commit", target.c_str());
            return false;
        }
    }
    const bool hadOld = fs.exists(target);
    if (hadOld && !fs.rename(target, bak)) {
        // The old file is untouched. Nothing to recover, so the temporary
        // file goes rather than waiting for a startup that has nothing to do.
        ESP_LOGE(tag, "Could not set %s aside; keeping it", target.c_str());
        fs.remove(tmpPath);
        return false;
    }
    if (!fs.rename(tmpPath, target)) {
        ESP_LOGE(tag, "Could not move %s into place", tmpPath.c_str());
        if (hadOld && !fs.rename(bak, target)) {
            // Both the rollback and the commit failed. The .bak and the
            // complete .tmp are both still on disk; recoverReplace promotes
            // the .tmp at the next startup.
            ESP_LOGE(tag, "Could not restore %s from %s; the .tmp holds the new version", target.c_str(), bak.c_str());
        }
        return false;
    }
    if (hadOld) {
        fs.remove(bak);
    }
    return true;
}

namespace {

// Adds `name` to targets when it is a leftover .bak or .tmp for `suffix`.
void noteLeftover(String name, const String &sfxBak, const String &sfxTmp, std::vector<String> &targets) {
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) {
        name = name.substring(slash + 1);
    }
    if (!name.endsWith(sfxBak) && !name.endsWith(sfxTmp)) {
        return;
    }
    const String base = name.substring(0, name.length() - 4);
    if (base.length() == 0) {
        return;
    }
    for (const String &t : targets) {
        if (t == base) {
            return;
        }
    }
    targets.push_back(base);
}

// Lists dir without opening its entries. File::openNextFile() opens every
// entry it returns, and on FAT each open is a linear scan of the directory,
// so walking /h on the SD card that way is quadratic in the shot count: with
// the bench card's history the boot sat in this walk for over eight minutes
// with WiFi and BLE never started (2026-09-08). readdir() reads the names
// straight out of the directory clusters. Returns false when the filesystem
// has no VFS mount point to read through.
bool listDirNames(fs::FS &fs, const String &dir, const String &sfxBak, const String &sfxTmp, std::vector<String> &targets) {
#ifdef GAGGIMATE_SIM
    (void)fs;
    (void)dir;
    (void)sfxBak;
    (void)sfxTmp;
    (void)targets;
    return false;
#else
    const char *mount = fs.mountpoint();
    if (mount == nullptr) {
        return false;
    }
    const String path = String(mount) + dir;
    DIR *d = opendir(path.c_str());
    if (d == nullptr) {
        // ENOENT is the normal first boot; report anything else.
        if (errno != ENOENT) {
            ESP_LOGW("SafeReplace", "opendir %s: %d", path.c_str(), errno);
        }
        return true;
    }
    while (const dirent *e = readdir(d)) {
        noteLeftover(String(e->d_name), sfxBak, sfxTmp, targets);
    }
    closedir(d);
    return true;
#endif
}

} // namespace

void recoverReplace(fs::FS &fs, const String &dir, const char *suffix, const char *tag) {
    const String sfxBak = String(suffix) + ".bak";
    const String sfxTmp = String(suffix) + ".tmp";
    // Collect first, act after the directory handle is closed: renaming and
    // removing entries under an open directory iterator is not something
    // every filesystem here handles.
    std::vector<String> targets;
    if (!listDirNames(fs, dir, sfxBak, sfxTmp, targets)) {
        File root = fs.open(dir);
        if (!root || !root.isDirectory()) {
            if (root) {
                root.close();
            }
            return;
        }
        File file = root.openNextFile();
        while (file) {
            noteLeftover(String(file.name()), sfxBak, sfxTmp, targets);
            file.close();
            file = root.openNextFile();
        }
        root.close();
    }
    for (const String &base : targets) {
        recoverOne(fs, dir + "/" + base, tag);
    }
}

} // namespace saferep
