#!/usr/bin/env python3
"""Call the EKA2L1 loopback control API, retaining response evidence."""
import argparse
import json
from pathlib import Path
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('method', choices=['GET', 'POST', 'PUT', 'PATCH', 'DELETE'])
    parser.add_argument('endpoint', help='API path beginning with /v1/')
    parser.add_argument('body', nargs='?', help='JSON request body')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--output', type=Path, help='Save response bytes')
    args = parser.parse_args()
    if not args.endpoint.startswith('/v1/') or not 1 <= args.port <= 65535:
        parser.error('Expected /v1/ endpoint and a valid loopback port')
    body = json.dumps(json.loads(args.body)).encode() if args.body else None
    request = urllib.request.Request(
        f'http://127.0.0.1:{args.port}{args.endpoint}', data=body,
        method=args.method, headers={'Content-Type': 'application/json'})
    # The local emulator must not be routed through the session network proxy.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        response = opener.open(request, timeout=300)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        data = response.read()
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(data)
        if 'application/json' in response.headers.get('Content-Type', ''):
            print(json.dumps(json.loads(data), indent=2))
        elif not args.output:
            parser.error('Binary response requires --output')
        if response.status >= 400:
            raise SystemExit(1)


if __name__ == '__main__':
    main()
