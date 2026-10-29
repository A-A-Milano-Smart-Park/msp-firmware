#!/usr/bin/env python3
"""Generate trust_anchor.h (BearSSL br_x509_trust_anchor array) from the PEM roots
listed in anchors.txt. Output layout matches the pycert_bearssl tool.

Usage:
    python3 tools/trust-anchors/generate.py            # rewrite trust_anchor.h
    python3 tools/trust-anchors/generate.py --check    # fail if trust_anchor.h is out of date

Requires the `cryptography` package.
"""
import sys
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa

HERE = Path(__file__).resolve().parent
HEADER = HERE.parent.parent / "trust_anchor.h"
CURVES = {"secp256r1": "BR_EC_secp256r1", "secp384r1": "BR_EC_secp384r1", "secp521r1": "BR_EC_secp521r1"}


def read_manifest():
    entries = []
    for line in (HERE / "anchors.txt").read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, note = (part.strip() for part in line.split("|", 1))
        entries.append((name, note))
    return entries


def byte_array(name, data):
    lines = [f"static const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 12):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 12]) + ",")
    return "\n".join(lines + ["};"]) + "\n"


def render(entries):
    certs = [(x509.load_pem_x509_certificate((HERE / "roots" / name).read_bytes()), note) for name, note in entries]
    out = ["""#ifndef _TRUST_ANCHOR_H_
#define _TRUST_ANCHOR_H_

#ifdef __cplusplus
extern "C"
{
#endif

/* Root CA trust anchors in BearSSL br_x509_trust_anchor format (same layout as
 * the pycert_bearssl tool output).  Do not change the byte arrays manually.
 * These are root CAs, not server certificates: the file does not need to be
 * updated when the server certificate is renewed, only if the server moves to
 * a CA that is not listed here.  Included certs:
 *"""]
    for i, (cert, note) in enumerate(certs):
        cn = cert.subject.get_attributes_for_oid(x509.NameOID.COMMON_NAME)[0].value
        out += [f" * Index:    {i}", f" * Label:    {cn}", f" * Subject:  {cert.subject.rfc4514_string()}",
                f" * Expires:  {cert.not_valid_after_utc:%Y-%m-%d}", f" * Used for: {note}"]
        if i != len(certs) - 1:
            out.append(" *")
    out += [" */\n", f"#define TAs_NUM {len(certs)}\n"]

    entries_c = []
    for i, (cert, _) in enumerate(certs):
        out.append(byte_array(f"TA_DN{i}", cert.subject.public_bytes()))
        key = cert.public_key()
        if isinstance(key, rsa.RSAPublicKey):
            n, e = key.public_numbers().n, key.public_numbers().e
            out.append(byte_array(f"TA_RSA_N{i}", n.to_bytes((n.bit_length() + 7) // 8, "big")))
            out.append(byte_array(f"TA_RSA_E{i}", e.to_bytes((e.bit_length() + 7) // 8, "big")))
            key_c = (f"            BR_KEYTYPE_RSA,\n            {{ .rsa = {{\n"
                     f"                (unsigned char *)TA_RSA_N{i}, sizeof TA_RSA_N{i},\n"
                     f"                (unsigned char *)TA_RSA_E{i}, sizeof TA_RSA_E{i},\n            }} }}")
        elif isinstance(key, ec.EllipticCurvePublicKey):
            point = key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
            out.append(byte_array(f"TA_EC_CURVE{i}", point))
            key_c = (f"            BR_KEYTYPE_EC,\n            {{ .ec = {{\n                {CURVES[key.curve.name]},\n"
                     f"                (unsigned char *)TA_EC_CURVE{i}, sizeof TA_EC_CURVE{i},\n            }} }}")
        else:
            raise SystemExit(f"unsupported key type in root {i}")
        entries_c.append(f"    {{\n        {{ (unsigned char *)TA_DN{i}, sizeof TA_DN{i} }},\n"
                         f"        BR_X509_TA_CA,\n        {{\n{key_c}\n        }}\n    }},")

    out += ["static const br_x509_trust_anchor TAs[] = {", "\n".join(entries_c), "};\n",
            '#ifdef __cplusplus\n} /* extern "C" */\n#endif\n\n#endif /* ifndef _TRUST_ANCHOR_H_ */']
    return "\n".join(out) + "\n"


def main():
    content = render(read_manifest())
    if "--check" in sys.argv:
        if HEADER.read_text() != content:
            sys.exit("trust_anchor.h is out of date: run python3 tools/trust-anchors/generate.py")
        print("trust_anchor.h is up to date")
        return
    HEADER.write_text(content)
    print(f"wrote {HEADER}")


if __name__ == "__main__":
    main()
