#!/usr/bin/env python3
"""Fetch the externally supplied validation materials and verify their hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import urllib.request


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    manifest = Path(__file__).resolve().parents[1] / 'validation/inputs.json'
    for item in json.loads(manifest.read_text()):
        destination = args.directory / item['filename']
        if destination.exists():
            if digest(destination) != item['sha256']:
                raise SystemExit(f'Existing file has unexpected hash: {destination}')
            print(f'Verified {destination}')
            continue
        with tempfile.NamedTemporaryFile(dir=args.directory, delete=False) as temp:
            temporary = Path(temp.name)
        try:
            urllib.request.urlretrieve(item['url'], temporary)
            if digest(temporary) != item['sha256']:
                raise SystemExit(f'Download has unexpected hash: {item["filename"]}')
            temporary.replace(destination)
            print(f'Downloaded and verified {destination}')
        finally:
            temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
