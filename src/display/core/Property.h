#pragma once
#ifndef PROPERTY_H
#define PROPERTY_H

#include <Arduino.h>
#include <Preferences.h>
#include <display/core/utils.h>
#include <vector>

// Preferences::putString returns strlen(value), so a successful write of an
// empty string is indistinguishable from a failure by the return value alone.
// Empty is a legitimate value here -- a cleared list serializes to "" -- so
// treat 0 as success when there was nothing to write, rather than retrying and
// logging forever on a save that actually worked. The cost is that a genuinely
// failed empty write still passes unnoticed, which is what every write did
// before; the API gives us no way to tell those two apart.
inline bool nvsPutString(Preferences &prefs, const char *key, const String &value) {
    return prefs.putString(key, value) != 0 || value.isEmpty();
}

// Type-specific NVS access used by Property<T>; add a specialization to support a new type.
// write() returns false when the value did not reach NVS, so the property stays
// dirty and the next flush retries it.
template <typename T> struct PreferencesCodec;

template <> struct PreferencesCodec<int> {
    static int read(Preferences &prefs, const char *key, const int &def) { return prefs.getInt(key, def); }
    static bool write(Preferences &prefs, const char *key, const int &value) { return prefs.putInt(key, value) != 0; }
};

template <> struct PreferencesCodec<bool> {
    static bool read(Preferences &prefs, const char *key, const bool &def) { return prefs.getBool(key, def); }
    static bool write(Preferences &prefs, const char *key, const bool &value) { return prefs.putBool(key, value) != 0; }
};

template <> struct PreferencesCodec<float> {
    static float read(Preferences &prefs, const char *key, const float &def) { return prefs.getFloat(key, def); }
    static bool write(Preferences &prefs, const char *key, const float &value) { return prefs.putFloat(key, value) != 0; }
};

template <> struct PreferencesCodec<double> {
    static double read(Preferences &prefs, const char *key, const double &def) { return prefs.getDouble(key, def); }
    static bool write(Preferences &prefs, const char *key, const double &value) { return prefs.putDouble(key, value) != 0; }
};

template <> struct PreferencesCodec<String> {
    static String read(Preferences &prefs, const char *key, const String &def) { return prefs.getString(key, def); }
    static bool write(Preferences &prefs, const char *key, const String &value) { return nvsPutString(prefs, key, value); }
};

template <> struct PreferencesCodec<std::vector<String>> {
    static std::vector<String> read(Preferences &prefs, const char *key, const std::vector<String> &def) {
        if (!prefs.isKey(key))
            return def;
        return explode(prefs.getString(key, ""), ',');
    }
    static bool write(Preferences &prefs, const char *key, const std::vector<String> &value) {
        return nvsPutString(prefs, key, implode(value, ","));
    }
};

class PropertyBase {
  public:
    virtual ~PropertyBase() = default;
    virtual void load(Preferences &prefs) = 0;
    // False when this property had a pending value that did not make it into NVS.
    virtual bool store(Preferences &prefs) = 0;
    // The NVS key, so a failed save can name what was lost.
    [[nodiscard]] virtual const char *name() const = 0;
    virtual bool isDirty() const = 0;
};

using PropertyRegistry = std::vector<PropertyBase *>;

// A persisted setting: tracks its own dirty state so only changed values hit NVS.
template <typename T> class Property : public PropertyBase {
  public:
    Property(PropertyRegistry &registry, const char *key, T defaultValue) : key(key), value(std::move(defaultValue)) {
        registry.push_back(this);
    }

    const T &get() const { return value; }

    // Returns true if the value changed and was marked for persisting.
    bool set(const T &newValue) {
        if (value == newValue)
            return false;
        value = newValue;
        dirty = true;
        return true;
    }

    // Overrides the value without marking it dirty (migration defaults for absent keys).
    void initDefault(const T &newValue) { value = newValue; }

    bool isDirty() const override { return dirty; }

    void load(Preferences &prefs) override { value = PreferencesCodec<T>::read(prefs, key, value); }

    bool store(Preferences &prefs) override {
        if (!dirty)
            return true;
        // Clear before writing so a concurrent set() is not lost, only deferred to the next flush.
        dirty = false;
        if (PreferencesCodec<T>::write(prefs, key, value)) {
            return true;
        }
        // The namespace opened, so doSave() did not bail, but this individual key
        // still failed -- a full NVS partition rejects new keys one at a time.
        // Without restoring the flag the value is gone for good: nothing marks it
        // dirty again, so it silently reverts on the next boot and the setting
        // "does not stick" with no trace in the log. Re-arming means the next
        // flush tries again, and doSave() reports that it had to.
        dirty = true;
        return false;
    }

    [[nodiscard]] const char *name() const override { return key; }

  private:
    const char *key;
    T value;
    bool dirty = false;
};

#endif // PROPERTY_H
