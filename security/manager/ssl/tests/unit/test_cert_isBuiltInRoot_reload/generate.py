#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Refresh built-in-root identity fixtures using the tree's NSS certutil.

All NSS writes happen in an explicitly supplied empty directory under obj/.
With --write, copy only the resulting source fixtures; never touch staged tests.
"""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

from pyasn1.codec.der import decoder, encoder
from pyasn1_modules import rfc2459

HERE = Path(__file__).resolve().parent
PLATFORM = HERE.parents[5]
OBJ = PLATFORM.parents[1] / "obj"
sys.path.insert(0, str(HERE.parent))
from generate_test_certs import IDENTITY_PEMS

ROOTS = {
    "imported": ("ISRG Root X1", "builtin-imported.pem"),
    "moduleOnly": ("DigiCert Global Root G2", "builtin-module.pem"),
}


def root_der(certdata, name):
    section = certdata.split('# Certificate "{}"'.format(name), 1)[1].split("# Trust for ", 1)[0]
    value = section.split("CKA_VALUE MULTILINE_OCTAL\n", 1)[1].split("\nEND", 1)[0]
    return bytes(int(octal, 8) for octal in re.findall(r"\\([0-7]{3})", value))


def pem_bytes(der):
    data = base64.b64encode(der)
    return (b"-----BEGIN CERTIFICATE-----\n" +
            b"\n".join(data[i:i + 64] for i in range(0, len(data), 64)) +
            b"\n-----END CERTIFICATE-----\n")


def identity(der, name, filename):
    cert, remaining = decoder.decode(der, asn1Spec=rfc2459.Certificate())
    if remaining:
        raise ValueError("trailing certificate data")
    tbs = cert["tbsCertificate"]
    encoded_serial = encoder.encode(tbs["serialNumber"])
    if encoded_serial[0] != 2 or encoded_serial[1] >= 128:
        raise ValueError("expected a short-form DER certificate serial")
    serial = encoded_serial[2:]
    issuer = encoder.encode(tbs["issuer"])
    # nsNSSCertificate::GetDbKey: unused module/slot IDs, serial length,
    # issuer length (big endian), serial value, and DER issuer name.
    key = struct.pack(">IIII", 0, 0, len(serial), len(issuer)) + serial + issuer
    digest = hashlib.sha256(der).digest()
    if digest.hex() != IDENTITY_PEMS[HERE.name + "/" + filename]:
        raise ValueError("unexpected certificate identity: {}".format(name))
    return {
        "name": name,
        "file": filename,
        "dbKey": base64.b64encode(key).decode("ascii"),
        "sha256Fingerprint": ":".join("{:02X}".format(byte) for byte in digest),
    }


def generate(certutil, workdir, write=False):
    workdir = Path(workdir).resolve()
    workdir.relative_to(OBJ.resolve())
    if not Path(certutil).is_absolute():
        raise ValueError("pass the absolute path to NSS certutil, not Windows certutil")
    workdir.mkdir(parents=True, exist_ok=True)
    if any(workdir.iterdir()):
        raise ValueError("work directory must be empty: {}".format(workdir))
    certdata = (PLATFORM / "security/nss/lib/ckfw/builtins/certdata.txt").read_text()
    data = {}
    metadata = {}
    for role, (name, filename) in ROOTS.items():
        der = root_der(certdata, name)
        data[filename] = pem_bytes(der)
        metadata[role] = identity(der, name, filename)
    data["nonbuiltin.pem"] = (HERE / "nonbuiltin.pem").read_bytes()
    nonbuiltin_der = base64.b64decode(b"".join(data["nonbuiltin.pem"].splitlines()[1:-1]))
    metadata["nonbuiltin"] = identity(nonbuiltin_der, "Let's Encrypt Authority X1", "nonbuiltin.pem")
    for filename, contents in data.items():
        (workdir / filename).write_bytes(contents)

    db = "sql:" + str(workdir)

    def certutil_run(*args):
        return subprocess.check_output([str(certutil), "-d", db] + list(args),
                                       stderr=subprocess.STDOUT)

    certutil_run("-N", "--empty-password")
    for role in ("imported", "nonbuiltin"):
        item = metadata[role]
        certutil_run("-A", "-n", item["name"], "-t", ",,", "-a", "-i", str(workdir / item["file"]))
        exported = certutil_run("-L", "-n", item["name"], "-r")
        actual = hashlib.sha256(exported).digest()
        expected = bytes.fromhex(item["sha256Fingerprint"].replace(":", ""))
        if actual != expected:
            raise ValueError("NSS exported unexpected DER for {}".format(item["name"]))
    listed = certutil_run("-L").decode("utf-8", errors="replace")
    if metadata["moduleOnly"]["name"] in listed:
        raise ValueError("module-only certificate unexpectedly imported")
    print(listed)
    data["identities.json"] = (json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode("utf-8")
    for filename in ("cert9.db", "key4.db"):
        data[filename] = (workdir / filename).read_bytes()
    if write:
        for filename, contents in data.items():
            (HERE / filename).write_bytes(contents)
            print("WRITE {}".format(filename))
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--certutil", required=True, help="absolute path to built NSS certutil.exe")
    parser.add_argument("--work-dir", required=True, help="empty output directory under workspace obj/")
    parser.add_argument("--write", action="store_true", help="copy generated files into source")
    args = parser.parse_args()
    try:
        generate(args.certutil, args.work_dir, args.write)
    except (ValueError, OSError, IndexError, subprocess.CalledProcessError) as error:
        parser.exit(1, "{}\n".format(error))


if __name__ == "__main__":
    main()
