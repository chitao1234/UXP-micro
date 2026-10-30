/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"

#include "mozilla/ScopeExit.h"
#include "js/Date.h"

BEGIN_TEST(testDateToLocaleString)
{
    // This test should only attempt to run if we have Intl support: necessary
    // to properly assume that changes to the default locale will predictably
    // affect the behavior of the locale-sensitive Date methods tested here.
    JS::Rooted<JS::Value> haveIntl(cx);
    EVAL("typeof Intl !== 'undefined'", &haveIntl);
    if (!haveIntl.toBoolean())
        return true;

    // Pervasive assumption: our Intl support includes "de" (German) and
    // "en" (English) and treats them differently for purposes of
    // Date.prototype.toLocale{,Date,Time}String behavior.

    // Start with German.
    CHECK(JS_SetDefaultLocale(cx, "de"));

    // The (constrained) Date object we'll use to test behavior.
    EXEC("var d = new Date(Date.UTC(2015, 9 - 1, 17));");

    // Test that toLocaleString behavior changes with default locale changes.
    EXEC("var deAll = d.toLocaleString();");

    CHECK(JS_SetDefaultLocale(cx, "en"));
    EXEC("if (d.toLocaleString() === deAll) \n"
         "  throw 'toLocaleString results should have changed with system locale change';");

    // Test that toLocaleDateString behavior changes with default locale changes.
    EXEC("var enDate = d.toLocaleDateString();");

    CHECK(JS_SetDefaultLocale(cx, "de"));
    EXEC("if (d.toLocaleDateString() === enDate) \n"
         "  throw 'toLocaleDateString results should have changed with system locale change';");

    // Test that toLocaleTimeString behavior changes with default locale changes.
    EXEC("var deTime = d.toLocaleTimeString();");

    CHECK(JS_SetDefaultLocale(cx, "en"));
    EXEC("if (d.toLocaleTimeString() === deTime) \n"
         "  throw 'toLocaleTimeString results should have changed with system locale change';");

    JS_ResetDefaultLocale(cx);
    return true;
}
END_TEST(testDateToLocaleString)

#if defined(XP_WIN)
BEGIN_TEST(testDateToLocaleString_windowsTimeZone)
{
    JS::RootedValue haveIntl(cx);
    EVAL("typeof Intl !== 'undefined'", &haveIntl);
    if (!haveIntl.toBoolean())
        return true;

    // _putenv copies its argument on Windows. Restore the original environment
    // and both timezone caches even if an assertion returns early.
    const char* original = getenv("TZ");
    size_t length = original ? strlen(original) : 0;
    js::UniqueChars restoreTZ(js_pod_malloc<char>(length + 4));
    CHECK(restoreTZ);
    strcpy(restoreTZ.get(), "TZ=");
    if (original)
        strcat(restoreTZ.get(), original);
    auto restore = mozilla::MakeScopeExit([&] {
        _putenv(restoreTZ.get());
        JS::ResetTimeZone();
    });

    CHECK(_putenv("TZ=") == 0);
    JS::ResetTimeZone();
    EXEC("var hostZone = new Intl.DateTimeFormat().resolvedOptions().timeZone;"
         "var date = new Date(Date.UTC(2020, 0, 15, 12));"
         "var oldFormatter = new Intl.DateTimeFormat('en-US');"
         "var oldOutput = oldFormatter.format(date);");

    struct TestZone {
        const char* setting;
        const char* name;
        int hour;
    };
    const TestZone zones[] = {
        {"TZ=PST8PDT", "PST8PDT", 4},
        {"TZ=UTC", "UTC", 12},
        {"TZ=EST5EDT", "EST5EDT", 7},
        {"TZ=GMT0", "GMT0", 12},
        {"TZ=PST8PDT", "PST8PDT", 4}
    };
    for (const auto& zone : zones) {
        CHECK(_putenv(zone.setting) == 0);
        JS::ResetTimeZone();
        JS::RootedString name(cx, JS_NewStringCopyZ(cx, zone.name));
        CHECK(name);
        JS::RootedValue nameValue(cx, JS::StringValue(name));
        CHECK(JS_SetProperty(cx, global, "testZone", nameValue));
        JS::RootedValue hourValue(cx, JS::Int32Value(zone.hour));
        CHECK(JS_SetProperty(cx, global, "testHour", hourValue));
        EXEC("if (date.getHours() !== testHour) throw new Error('CRT zone');"
             "for (var month of [0, 6]) {"
             "  var d = new Date(Date.UTC(2020, month, 15, 12));"
             "  var options = {hour: 'numeric', minute: 'numeric', second: 'numeric'};"
             "  var actual = new Intl.DateTimeFormat('en-US', options).format(d);"
             "  options.timeZone = testZone;"
             "  var expected = new Intl.DateTimeFormat('en-US', options).format(d);"
             "  if (actual !== expected) throw new Error('ICU zone: ' + actual + ' != ' + expected);"
             "}"
             "if (oldFormatter.format(date) !== oldOutput) throw new Error('existing formatter changed');");
    }

    // Unsupported TZ values must not be interpreted as arbitrary IANA IDs.
    // They keep ICU's host-detection fallback, just as when TZ is absent.
    const char* fallbackSettings[] = {"TZ=Europe/Paris", "TZ=invalid-zone", "TZ="};
    for (const char* setting : fallbackSettings) {
        CHECK(_putenv(setting) == 0);
        JS::ResetTimeZone();
        EXEC("if (new Intl.DateTimeFormat().resolvedOptions().timeZone !== hostZone)"
             "  throw new Error('host-zone fallback');");
    }
    return true;
}
END_TEST(testDateToLocaleString_windowsTimeZone)
#endif
