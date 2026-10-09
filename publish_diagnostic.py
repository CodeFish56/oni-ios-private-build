"""Publish an incomplete diagnostic IPA to the existing private GitHub repo.

The local Git credential is used in memory and is never printed or persisted.
This release is for sideload testing only; it is not a playable game.
"""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import pathlib
import subprocess
import urllib.error
import urllib.parse
import urllib.request


REPO = "CodeFish56/oni-ios-private-build"
TAG = "oni-boot-diagnostic-20261009"
API = f"https://api.github.com/repos/{REPO}"


def token_from_git() -> str:
    result = subprocess.run(
        ["git", "-c", "credential.interactive=false", "credential", "fill"],
        input="protocol=https\nhost=github.com\n\n",
        text=True,
        capture_output=True,
        check=True,
        timeout=20,
    )
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    return values["password"]


def api_json(url: str, token: str, payload: dict | None = None) -> dict:
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "oni-ios-private-diagnostic",
            **({"Content-Type": "application/json"} if data is not None else {}),
        },
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def release(token: str) -> dict:
    try:
        return api_json(f"{API}/releases/tags/{TAG}", token)
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
    return api_json(
        f"{API}/releases",
        token,
        {
            "tag_name": TAG,
            "target_commitish": "main",
            "name": "Boot diagnostic IPA (incomplete, not playable)",
            "body": (
                "Private installation test only. This IPA omits large Unity streaming "
                "resources and FMOD banks. It cannot run Oxygen Not Included gameplay. "
                "The complete unsigned build remains in the GitHub Actions artifact."
            ),
            "draft": False,
            "prerelease": True,
        },
    )


def upload(release_info: dict, ipa: pathlib.Path, token: str) -> dict:
    parsed = urllib.parse.urlparse(release_info["upload_url"].split("{", 1)[0])
    path = f"{parsed.path}?name={urllib.parse.quote(ipa.name)}"
    connection = http.client.HTTPSConnection(parsed.hostname, timeout=120)
    try:
        connection.putrequest("POST", path)
        connection.putheader("Authorization", f"Bearer {token}")
        connection.putheader("Accept", "application/vnd.github+json")
        connection.putheader("X-GitHub-Api-Version", "2022-11-28")
        connection.putheader("User-Agent", "oni-ios-private-diagnostic")
        connection.putheader("Content-Type", "application/octet-stream")
        connection.putheader("Content-Length", str(ipa.stat().st_size))
        connection.endheaders()
        sent = 0
        with ipa.open("rb") as source:
            while block := source.read(4 * 1024 * 1024):
                connection.send(block)
                sent += len(block)
                if sent // (64 * 1024 * 1024) != (sent - len(block)) // (64 * 1024 * 1024):
                    print(f"Uploaded {sent // (1024 * 1024)} MiB", flush=True)
        response = connection.getresponse()
        body = response.read()
        if response.status != 201:
            raise RuntimeError(f"GitHub asset upload failed: HTTP {response.status}: {body[:300]!r}")
        return json.loads(body)
    finally:
        connection.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa", type=pathlib.Path)
    args = parser.parse_args()
    ipa = args.ipa.resolve()
    digest = hashlib.sha256()
    with ipa.open("rb") as source:
        while block := source.read(4 * 1024 * 1024):
            digest.update(block)
    token = token_from_git()
    repo_info = api_json(API, token)
    if repo_info["full_name"] != REPO or not repo_info["private"]:
        raise RuntimeError("Refusing to publish to a non-private or unexpected repository")
    release_info = release(token)
    existing = next(
        (asset for asset in release_info["assets"] if asset["name"] == ipa.name), None
    )
    asset = existing or upload(release_info, ipa, token)
    if asset["size"] != ipa.stat().st_size:
        raise RuntimeError("Uploaded asset size differs from the local IPA")
    remote_digest = asset.get("digest", "")
    if remote_digest and remote_digest != f"sha256:{digest.hexdigest()}":
        raise RuntimeError("Uploaded asset SHA-256 differs from the local IPA")
    print(f"Private release: {release_info['html_url']}")
    print(f"Asset: {asset['name']} ({asset['size']} bytes)")
    print(f"SHA-256: {digest.hexdigest()}")


if __name__ == "__main__":
    main()
