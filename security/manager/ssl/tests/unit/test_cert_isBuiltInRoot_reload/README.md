This fixture checks certificate identity, not chain validity. Its SQL database
contains ISRG Root X1 (also in the built-in module) and Let's Encrypt Authority
X1 (not built in). DigiCert Global Root G2 is deliberately absent from the DB,
so the reload test must find it from the module. The expired Let's Encrypt
intermediate remains a valid negative input for `isBuiltInRoot`.

ISRG and DigiCert PEMs are extracted from this tree's NSS `certdata.txt`.
`identities.json` records SHA-256 fingerprints and DB keys computed using the
documented `nsNSSCertificate::GetDbKey` format. Both tests use the same metadata.
No test grants certificate trust or changes certificate verification.

To regenerate, from the workspace root, choose an **empty** directory under obj:

```
obj/test/_virtualenv/Scripts/python.exe basilisk/platform/security/manager/ssl/tests/unit/test_cert_isBuiltInRoot_reload/generate.py --certutil C:/ddev/microgecko/obj/test/dist/bin/certutil.exe --work-dir C:/ddev/microgecko/obj/qualification/tls-source-refresh/builtin-regeneration --write
```

Use the built NSS utility, not Windows `certutil.exe`. The tool creates a new
passwordless SQL DB in that working directory, imports the two fixture certs
without trust overrides, verifies their exported DER, then copies PEMs,
metadata, `cert9.db`, and `key4.db` into source. It never modifies staged tests
or a browser profile. NSS DB bytes contain random salts, so the meaningful
regeneration check is certificate contents/identity, not binary equality.

The former cert8/key3 fixtures and GeoTrust/VeriSign assumptions were obsolete.
Firefox 78 likewise uses only cert9/key4 in its reload test; this fork's current
root store no longer contains those old roots. Rebuild/restage after source
updates when no tests are active, then run both identity tests.
