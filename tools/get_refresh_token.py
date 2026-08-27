#!/usr/bin/env python3
"""Obtain a Spotify refresh token on the host, for the CrowPanel rotary port.

Why this exists
---------------
Upstream ThingPulse has the ESP32 itself run the OAuth flow: it starts an mDNS
responder and a web server, and registers `http://tp-spotify.local/callback/`
as the redirect URI. Spotify no longer accepts that. Their current rule is:

    "Use HTTPS for your redirect URI, unless you are using a loopback address,
     when HTTP is permitted... use the explicit IPv4 or IPv6, like
     http://127.0.0.1:PORT ... localhost is not allowed as redirect URI."

A `.local` mDNS name over plain HTTP is neither HTTPS nor a loopback literal,
so the authorize endpoint rejects it with `INVALID_CLIENT: Insecure redirect
URI`. An ESP32 on your LAN cannot satisfy the rule at all: it is not loopback,
and it cannot terminate TLS for a name it does not own a certificate for.

So the flow moves to the host, which *can* use a loopback address. This script
does the authorize + token exchange here, and writes the resulting refresh
token into the filesystem image. The board then finds `/refresh-token.txt` at
boot and skips the browser flow entirely.

The redirect URI matters only during the code exchange. Refreshing an access
token later does not involve it, so the board never needs it again.

Usage
-----
    1. In the Spotify dashboard, set the app's Redirect URI to exactly:
           http://127.0.0.1:8888/callback
    2. python3 tools/get_refresh_token.py
    3. pio run -e crowpanel-21-rotary -t uploadfs

Credentials are read from data/user.ini, which is gitignored. Nothing secret
is written to a tracked file, and the refresh token itself lands in data/ which
is also ignored.
"""

from __future__ import annotations

import base64
import configparser
import http.server
import json
import pathlib
import sys
import threading
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

REDIRECT_URI = "http://127.0.0.1:8888/callback"
PORT = 8888

# Same scopes the firmware asks for. user-modify-playback-state covers the
# knob's volume writes as well as skip and play/pause.
SCOPES = "user-read-playback-state user-modify-playback-state"

REPO = pathlib.Path(__file__).resolve().parent.parent
USER_INI = REPO / "data" / "user.ini"
TOKEN_FILE = REPO / "data" / "refresh-token.txt"

_auth_code: str | None = None
_auth_error: str | None = None
_done = threading.Event()


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    """Catches the one redirect Spotify sends back, then stops."""

    def do_GET(self) -> None:  # noqa: N802 - stdlib naming
        global _auth_code, _auth_error

        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != "/callback":
            self.send_response(404)
            self.end_headers()
            return

        params = urllib.parse.parse_qs(parsed.query)
        _auth_code = params.get("code", [None])[0]
        _auth_error = params.get("error", [None])[0]

        body = (
            b"<h2>Authorized. You can close this tab.</h2>"
            if _auth_code
            else b"<h2>Authorization failed. Check the terminal.</h2>"
        )
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

        _done.set()

    def log_message(self, *args) -> None:
        """Silence the default per-request logging."""


def read_credentials() -> tuple[str, str]:
    if not USER_INI.exists():
        sys.exit(f"missing {USER_INI} — create it with a [spotify] section first")

    parser = configparser.ConfigParser()
    parser.read(USER_INI)

    try:
        client_id = parser["spotify"]["client_id"].strip()
        client_secret = parser["spotify"]["client_secret"].strip()
    except KeyError as exc:
        sys.exit(f"{USER_INI} has no [spotify] {exc} entry")

    if not client_id or not client_secret:
        sys.exit(f"{USER_INI}: client_id and client_secret must both be set")

    return client_id, client_secret


def exchange_code(client_id: str, client_secret: str, code: str) -> str:
    """Trades the one-time auth code for a long-lived refresh token."""
    payload = urllib.parse.urlencode(
        {
            "grant_type": "authorization_code",
            "code": code,
            # Must match the authorize request byte for byte, or Spotify
            # rejects the exchange.
            "redirect_uri": REDIRECT_URI,
        }
    ).encode()

    basic = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    request = urllib.request.Request(
        "https://accounts.spotify.com/api/token",
        data=payload,
        headers={
            "Authorization": f"Basic {basic}",
            "Content-Type": "application/x-www-form-urlencoded",
        },
    )

    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            body = json.loads(response.read())
    except urllib.error.HTTPError as exc:
        sys.exit(f"token exchange failed ({exc.code}): {exc.read().decode()}")

    token = body.get("refresh_token")
    if not token:
        sys.exit(f"no refresh_token in response: {body}")

    return token


def main() -> None:
    client_id, client_secret = read_credentials()

    authorize_url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode(
        {
            "client_id": client_id,
            "response_type": "code",
            "redirect_uri": REDIRECT_URI,
            "scope": SCOPES,
        }
    )

    server = http.server.HTTPServer(("127.0.0.1", PORT), CallbackHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    print(f"listening on {REDIRECT_URI}")
    print("\nIf a browser does not open, paste this:\n")
    print(authorize_url)
    print()
    webbrowser.open(authorize_url)

    if not _done.wait(timeout=300):
        server.shutdown()
        sys.exit("timed out after 5 minutes waiting for the redirect")

    server.shutdown()

    if _auth_error or not _auth_code:
        sys.exit(f"authorization failed: {_auth_error or 'no code returned'}")

    token = exchange_code(client_id, client_secret, _auth_code)

    # Raw string, no trailing newline: the firmware reads this file with
    # SCFileIO::readFsString() and uses the bytes verbatim as the token.
    TOKEN_FILE.write_text(token)

    print(f"refresh token written to {TOKEN_FILE}")
    print("\nnow run:  pio run -e crowpanel-21-rotary -t uploadfs")


if __name__ == "__main__":
    main()
