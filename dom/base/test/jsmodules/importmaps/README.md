The eleven resolution JSON fixtures are copied unchanged from Firefox's WPT
import-maps/data-driven/resources directory, revision
309a5a01511293cd91f86d82c9c2f7130dd38ebc (see the workspace reference checkout).
The upstream WPT data files are distributed under the WPT BSD 3-Clause license.

test_importMaps_resolution.html adapts only the harness to SimpleTest. Each
case calls the browser's native import.meta.resolve from an inline module with
the fixture's base URL. It preserves upstream expected URLs/failures, including
scopes, null entries, opaque URL prefixes and backtracking.

The integration cases in test_importMaps{,_security,_lifecycle}.html adapt the
upstream multiple-import-maps, external-import-map-errors, type-whitespace and
integrity cases. Additional cases cover this loader's source-only speculative
preloads and its document.open module-map reset. CSP uses a separate HTTP
document, following the upstream security tests, rather than modifying the
inherited principal of a srcdoc document. LICENSE.md retains the WPT license.
