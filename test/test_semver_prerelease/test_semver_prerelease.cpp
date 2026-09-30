// Unit tests: prerelease ordering in the vendored semver comparison
// (lib/OTA/src/semver.c), which the OTA update channel uses to decide
// whether a build on GitHub is newer than the one installed.
//
// gm-bzu.57: the prerelease branch of compare_prerelease() had its two
// NULL-vs-non-NULL cases swapped, so a release such as 1.2.0 compared as
// LOWER than its own release candidate 1.2.0-rc1. That let the update
// channel offer 1.2.0-rc1 as an "update" over an installed 1.2.0, which is
// a downgrade. Host-side, no Arduino or ESP-IDF: pio test -e
// native_otaresult -f test_semver_prerelease.

#include <unity.h>

#include <cstring>
#include <string>

// semver.c is a plain C translation unit (symlinked into this directory, see
// semver.c alongside this file) so PlatformIO's native test build compiles
// it with the C compiler and links it in. This env has lib_ldf_mode off and
// no framework, so nothing else pulls semver.c into the test binary; #include
// "semver.c" into this .cpp, done for a same-language sibling like
// test/test_autotune_simc/test_autotune_simc.cpp, does not work here because
// C++ compiling semver.c's plain C strchr() usage is stricter than C about
// the const-ness of the returned pointer.
#include "semver.h"

namespace {

// Parses `s` into a semver_t. Aborts the test on a parse failure so a typo
// in a test's own version string fails loudly instead of silently comparing
// zeros.
semver_t parse(const std::string &s) {
    semver_t v;
    std::memset(&v, 0, sizeof(v));
    TEST_ASSERT_EQUAL_MESSAGE(0, semver_parse(s.c_str(), &v), s.c_str());
    return v;
}

// Returns semver_compare(a, b) for the two version strings, freeing the
// parsed structs afterward so repeated calls don't leak the prerelease and
// metadata buffers semver_parse allocates.
int compare(const std::string &a, const std::string &b) {
    semver_t va = parse(a);
    semver_t vb = parse(b);
    int result = semver_compare(va, vb);
    semver_free(&va);
    semver_free(&vb);
    return result;
}

} // namespace

void setUp() {}
void tearDown() {}

// The bug's own example: a release candidate must rank below its release.
void test_release_candidate_ranks_below_release() {
    TEST_ASSERT_EQUAL(-1, compare("1.2.0-rc1", "1.2.0"));
    TEST_ASSERT_EQUAL(1, compare("1.2.0", "1.2.0-rc1"));
    TEST_ASSERT_FALSE(semver_gt(parse("1.2.0-rc1"), parse("1.2.0")));
}

// A version with no prerelease at all must still equal itself and rank
// above any prerelease of the same major.minor.patch, not just rc-tagged
// ones.
void test_plain_release_outranks_any_prerelease_tag() {
    TEST_ASSERT_EQUAL(1, compare("2.0.0", "2.0.0-alpha"));
    TEST_ASSERT_EQUAL(1, compare("2.0.0", "2.0.0-beta.11"));
    TEST_ASSERT_EQUAL(0, compare("2.0.0", "2.0.0"));
}

// Two prereleases of the same release, numeric identifiers: rc2 outranks
// rc1, and the comparison is numeric, not lexical (rc9 < rc10).
void test_prerelease_vs_prerelease_numeric() {
    TEST_ASSERT_EQUAL(-1, compare("1.2.0-rc1", "1.2.0-rc2"));
    TEST_ASSERT_EQUAL(1, compare("1.2.0-rc2", "1.2.0-rc1"));
    TEST_ASSERT_EQUAL(-1, compare("1.2.0-rc9", "1.2.0-rc10"));
}

// Dotted identifier lists: numeric identifiers compare numerically,
// alphanumeric ones lexically, and a numeric identifier always ranks below
// an alphanumeric one at the same position (semver 2.0 spec item 11).
void test_prerelease_numeric_vs_alphanumeric_identifier() {
    TEST_ASSERT_EQUAL(-1, compare("1.0.0-alpha", "1.0.0-alpha.1"));
    TEST_ASSERT_EQUAL(1, compare("1.0.0-alpha.1", "1.0.0-alpha"));
    TEST_ASSERT_EQUAL(-1, compare("1.0.0-alpha.1", "1.0.0-alpha.beta"));
    TEST_ASSERT_EQUAL(1, compare("1.0.0-alpha.beta", "1.0.0-alpha.1"));
    TEST_ASSERT_EQUAL(-1, compare("1.0.0-alpha.beta", "1.0.0-beta"));
}

// A longer list of dotted identifiers ranks higher than a shorter one that
// matches it as a prefix (spec item 11, rule 4), spot-checked along the
// canonical ascending chain from the semver 2.0 spec (section 11).
void test_prerelease_identifier_count_and_full_chain() {
    TEST_ASSERT_EQUAL(-1, compare("1.0.0-beta", "1.0.0-beta.2"));
    TEST_ASSERT_EQUAL(1, compare("1.0.0-beta.2", "1.0.0-beta"));

    const char *ascending[] = {
        "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0",
    };
    const size_t n = sizeof(ascending) / sizeof(ascending[0]);
    for (size_t i = 0; i + 1 < n; i++) {
        TEST_ASSERT_EQUAL_MESSAGE(-1, compare(ascending[i], ascending[i + 1]), ascending[i]);
        TEST_ASSERT_EQUAL_MESSAGE(1, compare(ascending[i + 1], ascending[i]), ascending[i + 1]);
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_release_candidate_ranks_below_release);
    RUN_TEST(test_plain_release_outranks_any_prerelease_tag);
    RUN_TEST(test_prerelease_vs_prerelease_numeric);
    RUN_TEST(test_prerelease_numeric_vs_alphanumeric_identifier);
    RUN_TEST(test_prerelease_identifier_count_and_full_chain);
    return UNITY_END();
}
