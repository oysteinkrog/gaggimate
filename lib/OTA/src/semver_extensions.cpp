
#include <Arduino.h>

#include <sstream>
#include <vector>

#include "semver.h"

using namespace std;

vector<string> split(const string &s, char delim) {
    vector<string> result;
    stringstream ss(s);
    string item;

    while (getline(ss, item, delim)) {
        result.push_back(item);
    }

    return result;
}

semver_t from_string(const string &version) {
    if (version.empty()) {
        return {0, 0, 0, nullptr, nullptr};
    }
    // Split off the prerelease at the first '-' BEFORE splitting on '.' —
    // splitting the whole string on '.' first truncated dotted prerelease
    // identifiers ("1.9.0-sleep.2" parsed as prerelease "sleep", dropping
    // the ".2"), which made successive prerelease tags compare equal and
    // blocked OTA updates between them.
    auto dash = version.find('-');
    auto core = dash != string::npos ? version.substr(0, dash) : version;
    auto numbers = split(core, '.');
    auto major = numbers.size() > 0 ? atoi(numbers.at(0).c_str()) : 0;
    auto minor = numbers.size() > 1 ? atoi(numbers.at(1).c_str()) : 0;
    auto patch = numbers.size() > 2 ? atoi(numbers.at(2).c_str()) : 0;
    char *prerelease_ptr = nullptr;

    if (dash != string::npos && dash + 1 < version.length()) {
        auto prerelease = version.substr(dash + 1);
        prerelease_ptr = (char *)malloc(prerelease.length() + 1);
        if (prerelease_ptr != nullptr) {
            prerelease.copy(prerelease_ptr, prerelease.length());
            prerelease_ptr[prerelease.length()] = '\0';
        }
    }

    semver_t _ver = {major, minor, patch, nullptr, prerelease_ptr};

    return _ver;
}

String render_to_string(const semver_t &version) {
    String rendered = String(version.major) + "." + String(version.minor) + "." + String(version.patch);
    if (version.prerelease != nullptr) {
        rendered += "-" + String(version.prerelease);
    }
    return rendered;
}

bool operator>(const semver_t &x, const semver_t &y) { return semver_compare(x, y) > 0; }
