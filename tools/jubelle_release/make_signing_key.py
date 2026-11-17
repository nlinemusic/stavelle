#!/usr/bin/env python3
# Jubelle R3 (D6): makes the release signing key. Run this ONCE, on your own
# computer (pip install cryptography). It writes two files and prints what to
# put where. Nothing is sent anywhere.
#
#   jubelle-release-key.private.txt  KEEP SECRET. Paste its line into the GitHub
#                                    secret JUBELLE_RELEASE_SIGNING_KEY, and keep
#                                    a backup copy somewhere safe (offline).
#   jubelle-release-key.public.txt   Not secret. Paste its line into the GitHub
#                                    repository *variable* JUBELLE_RELEASE_PUBLIC_KEY.
#
# If the private key is lost, builds made with this public key can't verify new
# updates: you'd make a new key and install the next version by hand once.
import os
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

out = sys.argv[1] if len(sys.argv) > 1 else "."
private_path = os.path.join(out, "jubelle-release-key.private.txt")
public_path = os.path.join(out, "jubelle-release-key.public.txt")
if os.path.exists(private_path):
    sys.exit(f"{private_path} already exists; not overwriting a key")

key = Ed25519PrivateKey.generate()
seed = key.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw, serialization.NoEncryption()).hex()
public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()

fd = os.open(private_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(fd, "w") as f:
    f.write(seed + "\n")
with open(public_path, "w") as f:
    f.write(public + "\n")

print("Public key (repository variable JUBELLE_RELEASE_PUBLIC_KEY):")
print(public)
print(f"Private key written to {private_path} (secret JUBELLE_RELEASE_SIGNING_KEY). Don't share it.")
