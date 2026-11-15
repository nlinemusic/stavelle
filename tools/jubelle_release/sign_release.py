#!/usr/bin/env python3
# Jubelle R3: writes SHA256SUMS for the given files and signs it (Ed25519).
# The private key comes from the environment variable JUBELLE_RELEASE_SIGNING_KEY
# (64 hex digits), never from a file in the repository. The signature is
# checked against JUBELLE_RELEASE_PUBLIC_KEY before anything is written, so a
# release can't be signed with a key the app doesn't know.
#   sign_release.py <out dir> <file>...
import hashlib
import os
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

out, files = sys.argv[1], sys.argv[2:]
seed = os.environ.get("JUBELLE_RELEASE_SIGNING_KEY", "").strip()
public = os.environ.get("JUBELLE_RELEASE_PUBLIC_KEY", "").strip()
if len(seed) != 64 or len(public) != 64:
    sys.exit("JUBELLE_RELEASE_SIGNING_KEY (secret) and JUBELLE_RELEASE_PUBLIC_KEY (variable) must both be set")

lines = []
for path in files:
    with open(path, "rb") as f:
        lines.append(f"{hashlib.sha256(f.read()).hexdigest()}  {os.path.basename(path)}\n")
sums = "".join(lines).encode()

key = Ed25519PrivateKey.from_private_bytes(bytes.fromhex(seed))
signature = key.sign(sums)
Ed25519PublicKey.from_public_bytes(bytes.fromhex(public)).verify(signature, sums)   # raises if the keys don't match

with open(os.path.join(out, "SHA256SUMS"), "wb") as f:
    f.write(sums)
with open(os.path.join(out, "SHA256SUMS.sig"), "w") as f:
    f.write(signature.hex() + "\n")
print(sums.decode(), end="")
print("signed and checked against the public key")
