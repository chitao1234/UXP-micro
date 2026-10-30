The certificate fixtures are source files, generated explicitly rather than at
test startup. Use the build's existing Python virtualenv (it supplies `pyasn1`,
`pyasn1_modules`, `rsa`, and `ecc`; `unittest.mock` is part of Python 3):

```
obj/test/_virtualenv/Scripts/python.exe basilisk/platform/security/manager/ssl/tests/unit/generate_test_certs.py --year 2026
obj/test/_virtualenv/Scripts/python.exe basilisk/platform/security/manager/ssl/tests/unit/generate_test_certs.py --year 2026 --write
obj/test/_virtualenv/Scripts/python.exe basilisk/platform/security/manager/ssl/tests/unit/test_pycert.py
```

Run from the workspace root. The first command reports stale PEMs and returns 1
if regeneration is needed; `--write` changes source PEMs. `--list` shows each
fixture's policy without generating. No command stages tests, opens an NSS DB,
changes verifier policy, or changes the clock. Keys stay unchanged: pycert uses
the same fixed test keys in pykey. Non-time negative fixtures retain their
specified signature algorithms, key sizes, names and extensions.

The command also refreshes the duplicate `dom/security/test/contentverifier/goodChain.pem`
and its root fingerprint in `head.js` from the three PSM content-signing
certificates. Keep the browser's payload signatures unchanged: its leaf uses the
same fixed key. These browser tests cover the signed-content channel, CSP, SRI,
reloads and rejection of invalid signatures in addition to the PSM unit checks.

The inventory includes `../mochitest/browser/*.pem.certspec`, whose ordinary
viewer/UI certificates use the same rolling validity. The browser's
`expired-ca.pem` stays fixed at 2010–2011, while `ee-from-expired-ca.pem` rolls
so its error still identifies the expired issuer. Subject/key blocklist hashes,
the empty-subject serial, MD5-signature negative case, usages and trust are
preserved and checked by the maintenance tests.

Policies are explicit in `generate_test_certs.py`:

* Ordinary default-validity fixtures retain upstream's 800-day interval centered
  on January 1 of `--year`. Refresh annually; rolling 2026 files expire in early
  February 2027. The 823/854/1825-day EV-duration fixtures retain those exact
  lengths, so the 27-month/28-month distinction remains covered.
* Historical SHA-1, name-matching and StartCom/WoSign chains retain their bytes
  and are verified by tests at explicit 2016 times. The EKU test uses both 2016
  and current-time verification: its shared root and three historical leaves
  explicitly span 2015–2035; its three nsSGC intermediates retain their cutoff
  dates. Current-time EKU leaves use rolling validity.
* Known-expired, future-dated, before-epoch, malformed, and pinned debug EV-root
  fixtures are preserved. Future tests need review before 2031/2033, before-epoch
  fixtures before 2031, and debug EV roots before 2035. EV-root DER hashes are
  checked against their preserved values; root replacement requires updating the
  debug-only pins in `ExtendedValidation.cpp` separately.
  `test_x509.pem` is also a fixed parser-only input, retaining independent exact
  serial/date assertions without coupling X509.jsm tests to live TLS renewal.
* Four ordinary CA/intermediate specs previously expired in 2025 and now span
  2015–2035. The blocklist test derives their serials from the current fixtures.
  Unknown explicit validity and PEMs without a known spec/exception stop the
  maintenance command for review.
* Real certificates for built-in-root identity tests have separately checked
  DER hashes and use `test_cert_isBuiltInRoot_reload/generate.py`; their source
  SQL DB is a deliberate reload input, not a generated test-run cache.

After updating source, rebuild/restage the test fixtures **with no suite active**.
Delete/recreate only the generated NSS databases in the staged fixture
directories (`cert9.db`, `key4.db`, `pkcs11.txt`, and legacy equivalents if used).
The TLS helper opens an existing DB without rereading PEMs, so changing PEMs alone
can leave it serving stale certificates. The checked-in `test_cert_isBuiltInRoot_reload`
databases are separate deliberate fixtures; do not delete those as caches.

Validate successful TLS/OCSP handshakes along with expiry, future validity,
revocation, bad-signature, key-size, hostname, EV-duration, and historical-policy
negative cases. Never fix fixture ageing by suppressing certificate errors or
changing the validation clock in live-connection tests.
