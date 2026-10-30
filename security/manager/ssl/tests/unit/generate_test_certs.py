#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Explicit maintenance of PSM source certificate fixtures; see README.certs.md.

Like current Firefox's generate-test-certs command, this uses pycert specs to
produce adjacent PEMs.  Historical and pinned files are classified here rather
than silently moving their validity dates.  This never opens an NSS database,
changes verification preferences, stages tests, or changes the system clock.
"""

import argparse
import base64
import collections
import datetime
import hashlib
import io
from pathlib import Path
import sys

import pycert

HERE = Path(__file__).resolve().parent

# These tests pass explicit 2016 validation times, not the current time.  Their
# entire chains (including default-validity CAs) must stay in that time window.
HISTORICAL_GROUPS = {
    "test_baseline_requirements": "August 2016 name-matching policy cutoffs",
    "test_cert_sha1": "March 2016 SHA-1 policy cutoffs",
    "test_startcom_wosign": "November 2016 CA distrust policy cutoffs",
}

# Fingerprints are of the certificates' DER bytes.  The corresponding roots
# are pinned in debug-only ExtendedValidation.cpp; do not reissue them here.
EV_ROOT_SHA256 = "e4fb0416103267086c842e91f3ef0e4599bca85473f5032c7bdc09707649bfaa"
PINNED_ROOTS = {
    "bad_certs/evroot.pem": EV_ROOT_SHA256,
    "test_ev_certs/evroot.pem": EV_ROOT_SHA256,
    "test_keysize_ev/evroot.pem": EV_ROOT_SHA256,
    "test_validity/evroot.pem": EV_ROOT_SHA256,
    "test_keysize_ev/ev_root_rsa_2040.pem":
        "494610f4f5b196e7fbfa4da63403d09922d477203f84e0df1cadb4c276bb6324",
}

# Preserve the precise time-negative and ASN.1 edge-case inputs.  In particular,
# the intentionally future certificates still test the 2031/2033 boundaries;
# their classification will need review before those dates, not a clock bypass.
FIXED_CASES = {
    "test_x509.pem": "exact-byte X509.jsm parser input, never used for TLS",
    "bad_certs/beforeEpoch.pem": "validity before the Unix epoch",
    "bad_certs/beforeEpochINT.pem": "issuer validity before the Unix epoch",
    "bad_certs/expired-ee.pem": "intentionally expired leaf (also exact-date API test)",
    "bad_certs/expiredINT.pem": "intentionally expired issuer",
    "bad_certs/md5signature-expired.pem": "MD5 signature and expired leaf",
    "bad_certs/mismatch-expired.pem": "hostname mismatch and expired leaf",
    "bad_certs/mismatch-untrusted-expired.pem": "untrusted issuer, mismatch and expiry",
    "bad_certs/untrusted-expired.pem": "untrusted issuer and expired leaf",
    "bad_certs/mismatch-notYetValid.pem": "hostname mismatch and future leaf",
    "bad_certs/notYetValid.pem": "future leaf",
    "bad_certs/notYetValidINT.pem": "future issuer",
    "test_cert_eku/int-nsSGC-old.pem": "July 2016 Netscape step-up cutoff",
    "test_cert_eku/int-nsSGC-older.pem": "July 2015 Netscape step-up cutoff",
    "test_cert_eku/int-nsSGC-recent.pem": "August 2016 Netscape step-up cutoff",
}

# Hand-built malformed ASN.1 is not replaceable by a valid pycert certificate.
OPAQUE_PEMS = {
    "test_certviewer_invalid_oids/bug483440-attack2b.pem",
    "test_certviewer_invalid_oids/bug483440-attack7.pem",
    "test_certviewer_invalid_oids/bug483440-pk10oflo.pem",
}

# Real-world identity checks use certdata.txt, never reissued pycert roots.
# Expiry is irrelevant here because these tests do not verify a chain.
IDENTITY_PEMS = {
    "test_cert_isBuiltInRoot_reload/builtin-imported.pem":
        "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6",
    "test_cert_isBuiltInRoot_reload/builtin-module.pem":
        "cb3ccbb76031e5e0138f8dd39a23f9de47ffc35e43c1144cea27d46a5ab1cb5f",
    "test_cert_isBuiltInRoot_reload/nonbuiltin.pem":
        "7fdce3bf4103c2684b3adbb5792884bd45c75094c217788863950346f79c90a3",
}

# The first four replaced stale 2025 expiry.  The EKU root is used both today
# and at an explicit 2016 validation time; its three named leaves are used by
# that historical portion of the same test.  Keep the policy dates on the
# intervening nsSGC intermediates unchanged.
FIXED_LIVE_VALIDITY = {
    "bad_certs/other-test-ca.pem": "20150101-20350101",
    "bad_certs/test-int.pem": "20150101-20350101",
    "ocsp_certs/other-test-ca.pem": "20150101-20350101",
    "ocsp_certs/test-int.pem": "20150101-20350101",
    "test_cert_eku/ca.pem": "20150101-20350101",
    "test_cert_eku/ee-int-nsSGC-old.pem": "20150101-20350101",
    "test_cert_eku/ee-int-nsSGC-older.pem": "20150101-20350101",
    "test_cert_eku/ee-int-nsSGC-recent.pem": "20150101-20350101",
}

EV_DURATIONS = {
    "test_validity/ev_ee_27_months-ev_int_60_months-evroot.pem": "823",
    "test_validity/ev_ee_28_months-ev_int_60_months-evroot.pem": "854",
    "test_validity/ev_int_60_months-evroot.pem": "1825",
}


def validity_from_spec(text):
    values = [line.split(":", 1)[1] for line in text.splitlines()
              if line.startswith("validity:")]
    if len(values) > 1:
        raise ValueError("multiple validity fields in certificate specification")
    return values[0] if values else None


def policy_for(relative_pem, text):
    """Fail closed on unclassified explicit validity; default specs roll yearly."""
    if relative_pem in PINNED_ROOTS:
        return "pinned EV root"
    group = relative_pem.split("/", 1)[0]
    if group in HISTORICAL_GROUPS:
        return "historical"
    if relative_pem in FIXED_CASES:
        return "fixed negative/edge case"
    validity = validity_from_spec(text)
    if relative_pem in FIXED_LIVE_VALIDITY:
        if validity != FIXED_LIVE_VALIDITY[relative_pem]:
            raise ValueError("{} must specify validity:{}".format(
                relative_pem, FIXED_LIVE_VALIDITY[relative_pem]))
        return "fixed live/historical chain"
    if relative_pem in EV_DURATIONS:
        if validity != EV_DURATIONS[relative_pem]:
            raise ValueError("{} must retain its {}-day EV interval".format(
                relative_pem, EV_DURATIONS[relative_pem]))
        return "rolling EV duration"
    if validity is not None:
        raise ValueError("unclassified explicit validity: {} ({})".format(
            relative_pem, validity))
    return "rolling"


def inventory(root=HERE):
    """Validate every PEM/spec is covered before changing any fixture."""
    root = Path(root)
    rows = []
    for spec in sorted(root.rglob("*.pem.certspec")):
        pem = spec.with_suffix("")
        relative = pem.relative_to(root).as_posix()
        if not pem.is_file():
            raise ValueError("missing output fixture: {}".format(relative))
        text = spec.read_text(encoding="utf-8")
        policy = policy_for(relative, text)
        if relative in PINNED_ROOTS:
            der = base64.b64decode(b"".join(pem.read_bytes().splitlines()[1:-1]))
            actual = hashlib.sha256(der).hexdigest()
            if actual != PINNED_ROOTS[relative]:
                raise ValueError("pinned EV root changed: {}".format(relative))
        rows.append((relative, text, policy))
    known = {relative for relative, _, _ in rows}
    for pem in root.rglob("*.pem"):
        relative = pem.relative_to(root).as_posix()
        if relative in IDENTITY_PEMS:
            der = base64.b64decode(b"".join(pem.read_bytes().splitlines()[1:-1]))
            if hashlib.sha256(der).hexdigest() != IDENTITY_PEMS[relative]:
                raise ValueError("identity fixture changed: {}".format(relative))
        elif relative not in known and relative not in OPAQUE_PEMS:
            raise ValueError("unclassified PEM without a spec: {}".format(relative))
    for relative in IDENTITY_PEMS:
        if not (root / relative).is_file():
            raise ValueError("missing identity fixture: {}".format(relative))
    for relative in set(PINNED_ROOTS) | set(FIXED_CASES) | set(FIXED_LIVE_VALIDITY) | set(EV_DURATIONS):
        if relative not in known:
            raise ValueError("fixture policy has no corresponding spec: {}".format(relative))
    return rows


def generate_updates(rows, year, root=HERE):
    """Generate everything first, so a generation error cannot leave half a set."""
    now = datetime.datetime(year, 1, 1)
    updates = []
    for relative, text, policy in rows:
        if policy in ("pinned EV root", "historical", "fixed negative/edge case"):
            continue
        pem = Path(root) / relative
        contents = (pycert.Certificate(io.StringIO(text), now=now).toPEM() + "\n").encode("ascii")
        # Git normalizes PEM newlines.  Do not rewrite all files merely because
        # this checkout uses CRLF, but emit newly changed fixtures with LF.
        if pem.read_bytes().replace(b"\r\n", b"\n") != contents:
            updates.append((pem, contents))
    return updates


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--year", type=int, default=datetime.datetime.utcnow().year,
                        help="year used for rolling validity (default: current UTC year)")
    parser.add_argument("--write", action="store_true",
                        help="write changed source PEMs; default checks without writing")
    parser.add_argument("--list", action="store_true",
                        help="list fixture policies without generating certificates")
    args = parser.parse_args(argv)
    try:
        rows = inventory()
        counts = collections.Counter(policy for _, _, policy in rows)
        for policy, count in sorted(counts.items()):
            print("{}: {}".format(policy, count))
        print("opaque malformed PEMs: {}".format(len(OPAQUE_PEMS)))
        print("manual identity PEMs: {}".format(len(IDENTITY_PEMS)))
        if args.list:
            for relative, _, policy in rows:
                print("{}: {}".format(policy, relative))
            return 0
        updates = generate_updates(rows, args.year)
        for pem, contents in updates:
            print("{} {}".format("WRITE" if args.write else "STALE", pem.relative_to(HERE)))
        if args.write:
            for pem, contents in updates:
                pem.write_bytes(contents)
        print("{} fixture(s) {}".format(len(updates), "written" if args.write else "differ"))
        return 0 if args.write or not updates else 1
    except (ValueError, OSError) as error:
        parser.exit(2, "{}\n".format(error))


if __name__ == "__main__":
    sys.exit(main())
