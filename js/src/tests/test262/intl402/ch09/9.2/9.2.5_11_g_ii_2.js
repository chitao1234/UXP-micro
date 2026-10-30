// Copyright 2011-2012 Norbert Lindenberg. All rights reserved.
// Copyright 2012 Mozilla Corporation. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/**
 * @description Tests that missing Unicode extension values default to true for
 *     boolean keys.
 * @author Norbert Lindenberg
 */

var extensions = ["-u-co-phonebk-kn", "-u-kn-co-phonebk"];
extensions.forEach(function (extension) {
    var defaultLocale = new Intl.Collator().resolvedOptions().locale;
    var collator = new Intl.Collator([defaultLocale + extension], {usage: "sort"});
    var locale = collator.resolvedOptions().locale;
    var numeric = collator.resolvedOptions().numeric;
    if (numeric !== undefined) {
        if (numeric !== true) {
            $ERROR("Default value for \"kn\" should be true, but is " + numeric + ".");
        }
        if (locale.indexOf("-kn") === -1 || locale.indexOf("-kn-true") !== -1) {
            $ERROR("The true boolean key must be retained in canonical bare -kn form.");
        }
    }
});

