/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests X509.jsm against a fixed parser input. Its serial and validity are
// deliberately stable; live TLS tests use the separately refreshed default-ee.

var { X509 } = Cu.import("resource://gre/modules/psm/X509.jsm", {});

function stringToBytes(s) {
  let b = [];
  for (let i = 0; i < s.length; i++) {
    b.push(s.charCodeAt(i));
  }
  return b;
}

function readPEMToBytes(filename) {
  return stringToBytes(atob(pemToBase64(readFile(do_get_file(filename)))));
}

function run_test() {
  let certificate = new X509.Certificate();
  certificate.parse(readPEMToBytes("test_x509.pem"));

  equal(certificate.tbsCertificate.version, 3,
        "test_x509.pem should be x509v3");

  // serialNumber
  deepEqual(certificate.tbsCertificate.serialNumber,
            [ 0x35, 0x1b, 0xe9, 0x3a, 0x1b, 0x03, 0x1c, 0x46, 0x1b, 0x45,
              0xfe, 0x9b, 0xb2, 0x20, 0x0f, 0x6e, 0xf2, 0x9e, 0xd9, 0x50 ],
            "test_x509.pem should have expected serialNumber");

  deepEqual(certificate.tbsCertificate.signature.algorithm._values,
            [ 1, 2, 840, 113549, 1, 1, 11 ], // sha256WithRSAEncryption
            "test_x509.pem should have sha256WithRSAEncryption signature");
  // TODO: there should actually be an explicit encoded NULL here, but it looks
  // like pycert doesn't include it.
  deepEqual(certificate.tbsCertificate.signature.parameters, null,
            "test_x509.pem should have NULL parameters for signature");

  equal(certificate.tbsCertificate.issuer.rdns.length, 1,
        "test_x509.pem should have one RDN in issuer");
  equal(certificate.tbsCertificate.issuer.rdns[0].avas.length, 1,
        "test_x509.pem should have one AVA in RDN in issuer");
  deepEqual(certificate.tbsCertificate.issuer.rdns[0].avas[0].value.value,
            stringToBytes("Test CA"),
            "test_x509.pem should have issuer 'Test CA'");

  equal(certificate.tbsCertificate.validity.notBefore.time.getTime(),
        Date.parse("2015-11-28T00:00:00.000Z"),
        "test_x509.pem should have the correct value for notBefore");
  equal(certificate.tbsCertificate.validity.notAfter.time.getTime(),
        Date.parse("2018-02-05T00:00:00.000Z"),
        "test_x509.pem should have the correct value for notAfter");

  equal(certificate.tbsCertificate.subject.rdns.length, 1,
        "test_x509.pem should have one RDN in subject");
  equal(certificate.tbsCertificate.subject.rdns[0].avas.length, 1,
        "test_x509.pem should have one AVA in RDN in subject");
  deepEqual(certificate.tbsCertificate.subject.rdns[0].avas[0].value.value,
            stringToBytes("Test End-entity"),
            "test_x509.pem should have subject 'Test End-entity'");

  deepEqual(certificate.tbsCertificate.subjectPublicKeyInfo.algorithm.algorithm._values,
            [ 1, 2, 840, 113549, 1, 1, 1 ], // rsaEncryption
            "test_x509.pem should have a spki algorithm of rsaEncryption");

  equal(certificate.tbsCertificate.extensions.length, 2,
        "test_x509.pem should have two extensions");

  deepEqual(certificate.signatureAlgorithm.algorithm._values,
            [ 1, 2, 840, 113549, 1, 1, 11 ], // sha256WithRSAEncryption
            "test_x509.pem should have sha256WithRSAEncryption signatureAlgorithm");
  // TODO: there should actually be an explicit encoded NULL here, but it looks
  // like pycert doesn't include it.
  deepEqual(certificate.signatureAlgorithm.parameters, null,
            "test_x509.pem should have NULL parameters for signatureAlgorithm");

  equal(certificate.signatureValue.length, 2048 / 8,
        "length of signature on test_x509.pem should be 2048 bits");
}
