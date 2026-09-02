#ifndef GITHUBOTA_COMMON_H
#define GITHUBOTA_COMMON_H

#include "semver.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <WiFiClientSecure.h>

using Updater = HTTPUpdate;

void attach_ca_bundle(WiFiClientSecure &client);

String get_updated_base_url_via_redirect(WiFiClientSecure &wifi_client, String &release_url);
String get_redirect_location(WiFiClientSecure &wifi_client, String &initial_url);
String get_updated_version_via_txt_file(WiFiClientSecure &wifi_client, String &_release_url);

// Follow up to maxHops redirects by hand, re-attaching the CA bundle before
// every leg, and return the first non-redirecting (2xx) URL. HTTPClient and
// HTTPUpdate silently drop setCACertBundle() when they follow a redirect to a
// different host -- every GitHub release asset 302s from github.com to
// release-assets.githubusercontent.com -- so the second TLS handshake fails
// with -30336 "No CA Chain is set". Resolving the chain here and handing the
// caller the terminal single-host URL keeps every leg trusted. Returns "" on
// failure (unreachable host, empty Location, or too many hops).
String resolve_redirect_chain(WiFiClientSecure &wifi_client, String url, int maxHops = 5);

void print_update_result(Updater updater, HTTPUpdateResult result, const char *TAG);

bool update_required(semver_t _new_version, semver_t _current_version);

void update_started();
void update_finished();
void update_error(int err);

#endif
