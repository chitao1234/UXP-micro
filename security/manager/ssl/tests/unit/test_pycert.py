# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import base64
import hashlib
import importlib.util
import io
import json
import sqlite3
import unittest

from ecc import encoding
from pyasn1.codec.der import decoder, encoder
from pyasn1_modules import rfc2459

import generate_test_certs as refresh
import pycert
import pykey


class CertificateGeneration(unittest.TestCase):
    def test_deterministic_ecdsa_certificate_and_signature(self):
        # Exercise the actual pycert -> pykey -> PyECC path that broke in the
        # Python 3 port.  Equal bytes alone could still represent a bad signature,
        # so verify the DER's signature and reject tampered data as well.
        spec = ("issuer:EC issuer\nsubject:EC leaf\nissuerKey:secp384r1\n"
                "subjectKey:secp256r1\nsignature:ecdsaWithSHA256\n"
                "extension:subjectAlternativeName:example.com\n")
        now = datetime.datetime(2026, 6, 12)
        cert = pycert.Certificate(io.StringIO(spec), now=now)
        der = cert.toDER()
        self.assertEqual(der, cert.toDER())
        self.assertEqual(der, pycert.Certificate(io.StringIO(spec), now=now).toDER())
        decoded, rest = decoder.decode(der, asn1Spec=rfc2459.Certificate())
        self.assertEqual(rest, b"")
        tbs = encoder.encode(decoded["tbsCertificate"])
        bit_string = list(decoded["signatureValue"])
        signature_der = bytes(sum(bit_string[i + j] << (7 - j) for j in range(8))
                              for i in range(0, len(bit_string), 8))
        point, rest = decoder.decode(signature_der,
                                     asn1Spec=pykey.ECPoint())
        self.assertEqual(rest, b"")
        signature = encoding.enc_point((int(point["x"]), int(point["y"])))
        self.assertTrue(cert.issuerKey.key.verify(tbs, signature, "sha256"))
        self.assertFalse(cert.issuerKey.key.verify(tbs + b"tampered", signature, "sha256"))
        wrong_key = pykey.keyFromSpecification("secp256r1")
        self.assertFalse(wrong_key.key.verify(tbs, signature, "sha256"))

    def test_year_changes_only_rolling_date_window(self):
        spec = "issuer:issuer\nsubject:subject\n"
        a = pycert.Certificate(io.StringIO(spec), now=datetime.datetime(2026, 10, 31))
        b = pycert.Certificate(io.StringIO(spec), now=datetime.datetime(2027, 1, 1))
        self.assertEqual(a.notAfter - a.notBefore, datetime.timedelta(days=800))
        self.assertEqual(b.notAfter - b.notBefore, datetime.timedelta(days=800))
        self.assertEqual(b.now, datetime.datetime(2027, 1, 1))
        self.assertEqual(a.issuer, b.issuer)
        self.assertEqual(a.subject, b.subject)
        self.assertNotEqual(a.toDER(), b.toDER())

    def test_explicit_historical_dates_ignore_generation_year(self):
        spec = "issuer:issuer\nsubject:subject\nvalidity:20130101-20140101\n"
        a = pycert.Certificate(io.StringIO(spec), now=datetime.datetime(2026, 1, 1))
        b = pycert.Certificate(io.StringIO(spec), now=datetime.datetime(2030, 1, 1))
        self.assertEqual(a.toDER(), b.toDER())
        self.assertEqual(a.notAfter, datetime.datetime(2014, 1, 1))

    def test_fixed_x509_parser_fixture_is_reproducible(self):
        spec = (refresh.HERE / "test_x509.pem.certspec").read_text()
        generated = pycert.Certificate(io.StringIO(spec), now=datetime.datetime(2026, 1, 1)).toPEM()
        self.assertEqual(generated.strip(), (refresh.HERE / "test_x509.pem").read_text().strip())

    def test_ev_duration_lengths_retained(self):
        for days in (823, 854, 1825):
            spec = "issuer:issuer\nsubject:subject\nvalidity:{}\n".format(days)
            cert = pycert.Certificate(io.StringIO(spec), now=datetime.datetime(2026, 1, 1))
            self.assertEqual(cert.notAfter - cert.notBefore, datetime.timedelta(days=days))

    def test_inventory_and_protected_roots(self):
        rows = refresh.inventory()
        self.assertGreater(len(rows), 300)
        policies = {relative: policy for relative, _, policy in rows}
        self.assertEqual(policies["bad_certs/expired-ee.pem"], "fixed negative/edge case")
        self.assertEqual(policies["bad_certs/notYetValid.pem"], "fixed negative/edge case")
        self.assertEqual(policies["test_cert_sha1/ee-post_int-post.pem"], "historical")
        self.assertEqual(policies["bad_certs/evroot.pem"], "pinned EV root")

    def test_unknown_explicit_validity_requires_classification(self):
        with self.assertRaises(ValueError):
            refresh.policy_for("new-fixture.pem", "validity:20310101-20320101\n")
        with self.assertRaises(ValueError):
            refresh.policy_for("new-fixture.pem", "validity:100\n")

    def test_mixed_historical_current_eku_chain(self):
        rows = {relative: (text, policy) for relative, text, policy in refresh.inventory()}
        for relative in refresh.FIXED_LIVE_VALIDITY:
            text, policy = rows[relative]
            cert = pycert.Certificate(io.StringIO(text), now=datetime.datetime(2026, 1, 1))
            self.assertLess(cert.notBefore, datetime.datetime(2016, 8, 25))
            self.assertGreater(cert.notAfter, datetime.datetime(2026, 12, 31))
            self.assertEqual(policy, "fixed live/historical chain")

    def test_builtin_root_metadata_matches_certdata_and_der(self):
        fixture = refresh.HERE / "test_cert_isBuiltInRoot_reload"
        module_spec = importlib.util.spec_from_file_location("generate_root_identity", str(fixture / "generate.py"))
        generator = importlib.util.module_from_spec(module_spec)
        module_spec.loader.exec_module(generator)
        certdata = (generator.PLATFORM / "security/nss/lib/ckfw/builtins/certdata.txt").read_text()
        metadata = json.loads((fixture / "identities.json").read_text())
        for role in ("imported", "moduleOnly", "nonbuiltin"):
            info = metadata[role]
            der = base64.b64decode(b"".join((fixture / info["file"]).read_bytes().splitlines()[1:-1]))
            self.assertEqual(info, generator.identity(der, info["name"], info["file"]))
            if role != "nonbuiltin":
                self.assertEqual(der, generator.root_der(certdata, info["name"]))
            else:
                self.assertNotIn('# Certificate "{}"'.format(info["name"]), certdata)

    def test_reload_sql_database_contains_only_imported_fixtures(self):
        fixture = refresh.HERE / "test_cert_isBuiltInRoot_reload"
        metadata = json.loads((fixture / "identities.json").read_text())
        # Inspect this deliberate source DB read-only, without initializing NSS
        # or accidentally creating another empty fixture database.
        with sqlite3.connect((fixture / "cert9.db").as_uri() + "?mode=ro", uri=True) as database:
            der_certs = [row[0] for row in database.execute("SELECT a11 FROM nssPublic WHERE a11 IS NOT NULL")]
        fingerprints = {hashlib.sha256(der).hexdigest() for der in der_certs}
        expected = {metadata[role]["sha256Fingerprint"].replace(":", "").lower()
                    for role in ("imported", "nonbuiltin")}
        self.assertEqual(fingerprints, expected)
        self.assertNotIn(metadata["moduleOnly"]["sha256Fingerprint"].replace(":", "").lower(), fingerprints)


if __name__ == "__main__":
    unittest.main()
