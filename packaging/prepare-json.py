#!/usr/bin/env python3
"""Unpack the pinned tinox-central JSON dependency without build-time network access."""
import base64
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile

response = json.loads(Path(sys.argv[1]).read_text())
payload = base64.b64decode(response["contentBase64"], validate=True)
expected = "5a6566ad4d187b079ee6112fbcf4d3d873ba77fe1ac8a342bfdf5aedc85bafcc"
if hashlib.sha256(payload).hexdigest() != expected:
    sys.exit("tinox.core:json:1.1.10 checksum mismatch")
with tarfile.open(fileobj=io.BytesIO(payload)) as archive:
    archive.extractall(sys.argv[2], filter="data")
