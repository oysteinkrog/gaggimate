#include "SafeReplace.h"

#include <ArduinoJson.h>
#include <esp_log.h>

#include <vector>

namespace saferep {

namespace {

// A .tmp with no other version to fall back on is kept only if it holds a
// whole JSON document: a power cut mid-write leaves a prefix, which does not
// parse. Deserialised straight from the stream, so the check needs no copy
// of the file.
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

void recoverOne(fs::FS &fs, const String &target, const char *tag) {
    const String bak = target + ".bak";
    const String tmp = target + ".tmp";
    const bool hasTarget = fs.exists(target);
    const bool hasBak = fs.exists(bak);
    const bool hasTmp = fs.exists(tmp);
    if (hasBak) {
        if (hasTarget) {
            fs.remove(bak);
            if (hasTmp) {
                fs.remove(tmp);
            }
            return;
        }
        if (hasTmp) {
            if (fs.rename(tmp, target)) {
                ESP_LOGW(tag, "Recovered %s from its completed .tmp", target.c_str());
                fs.remove(bak);
            } else {
                ESP_LOGE(tag, "Could not promote %s; rolling back the .bak", tmp.c_str());
                if (fs.rename(bak, target)) {
                    fs.remove(tmp);
                }
            }
            return;
        }
        if (fs.rename(bak, target)) {
            ESP_LOGW(tag, "Rolled %s back from its .bak", target.c_str());
        } else {
            ESP_LOGE(tag, "Could not roll %s back from %s", target.c_str(), bak.c_str());
        }
        return;
    }
    if (hasTmp) {
        if (hasTarget) {
            fs.remove(tmp);
            return;
        }
        if (parsesAsJson(fs, tmp) && fs.rename(tmp, target)) {
            ESP_LOGW(tag, "Recovered %s from an uncommitted .tmp", target.c_str());
        } else {
            ESP_LOGW(tag, "Dropping incomplete %s", tmp.c_str());
            fs.remove(tmp);
        }
    }
}

} // namespace

bool commitReplace(fs::FS &fs, const String &tmpPath, const String &target, const char *tag) {
    const String bak = target + ".bak";
    if (fs.exists(bak)) {
        // Startup recovery settles these; one still here means a commit was
        // interrupted since boot and the current target is the version to
        // keep, so the stale copy goes.
        fs.remove(bak);
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

void recoverReplace(fs::FS &fs, const String &dir, const char *suffix, const char *tag) {
    File root = fs.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
        }
        return;
    }
    // Collect first, act after the directory handle is closed: renaming and
    // removing entries under an open directory iterator is not something
    // every filesystem here handles.
    std::vector<String> targets;
    const String sfxBak = String(suffix) + ".bak";
    const String sfxTmp = String(suffix) + ".tmp";
    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        file.close();
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) {
            name = name.substring(slash + 1);
        }
        String base;
        if (name.endsWith(sfxBak) || name.endsWith(sfxTmp)) {
            base = name.substring(0, name.length() - 4);
        }
        if (base.length() > 0) {
            bool seen = false;
            for (const String &t : targets) {
                seen = seen || t == base;
            }
            if (!seen) {
                targets.push_back(base);
            }
        }
        file = root.openNextFile();
    }
    root.close();
    for (const String &base : targets) {
        recoverOne(fs, dir + "/" + base, tag);
    }
}

} // namespace saferep
