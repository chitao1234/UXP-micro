Borrowed from Firefox 309a5a01511293cd91f86d82c9c2f7130dd38ebc (2026-10-27).

The css-pseudo/highlight-cascade/cascade-highlight-001.html test, its reference,
and css-pseudo/support/highlights.css are copied from
`testing/web-platform/tests/css/css-pseudo/`. The test's load handler calls
`window.focus()` before selecting its text, because this native harness focuses
the chrome window while inactive content selections use platform colors.
The reference and support stylesheet are unchanged; all test declarations and
expected pixels are preserved.

cascade-highlight-selection-alias.html has the same focus adaptation, with only
`div > span::selection` changed to `div > span::-moz-selection`. It keeps the
upstream expected result and specificity scenario, and checks that the legacy
alias participates in that cascade. See LICENSE.md for the upstream BSD license.

The supplemental mochitest checks this fork's legacy Inspector APIs, alias
queries, CSSOM mutation, source order and !important; the upstream test does not
cover mixed standard/legacy spelling or those interfaces.
