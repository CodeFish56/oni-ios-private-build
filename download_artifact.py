"""Download and verify one GitHub Actions artifact with local Git credentials.

Usage: python download_artifact.py ARTIFACT_ID OUTPUT_ZIP
The credential is read from Git Credential Manager and never printed or saved.
"""

import hashlib
import json
import os
import pathlib
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request


REPO = "CodeFish56/oni-ios-private-build"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        return None


def github_get(url, token):
    request = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "oni-ios-private-build-artifact-downloader",
        },
    )
    return urllib.request.build_opener(NoRedirect).open(request, timeout=120)


def main():
    artifact_id = int(sys.argv[1])
    output = pathlib.Path(sys.argv[2]).resolve()
    result = subprocess.run(
        ["git", "-c", "credential.interactive=false", "credential", "fill"],
        input="protocol=https\nhost=github.com\n\n",
        text=True,
        capture_output=True,
        check=True,
        timeout=20,
    )
    credentials = dict(
        line.split("=", 1) for line in result.stdout.splitlines() if "=" in line
    )
    token = credentials["password"]
    api = f"https://api.github.com/repos/{REPO}/actions/artifacts/{artifact_id}"
    with github_get(api, token) as response:
        metadata = json.load(response)
    if metadata["expired"] or metadata["id"] != artifact_id:
        raise ValueError("Artifact expired or ID mismatch")
    expected = metadata["digest"].removeprefix("sha256:")
    expected_size = metadata["size_in_bytes"]
    print(f"Artifact: {metadata['name']}, {expected_size:,} bytes", flush=True)

    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_suffix(output.suffix + ".part")
    digest = hashlib.sha256()
    received = partial.stat().st_size if partial.exists() else 0
    if received > expected_size:
        raise ValueError("Partial file exceeds the expected artifact size")
    if received:
        print(f"Resuming from {received / (1024 ** 3):.2f} GiB", flush=True)
        with partial.open("rb") as src:
            while block := src.read(4 * 1024 * 1024):
                digest.update(block)

    attempts = 0
    while received < expected_size:
        attempts += 1
        if attempts > 12:
            raise ConnectionError("Too many interrupted artifact downloads")
        try:
            github_get(api + "/zip", token)
            raise ValueError("GitHub did not redirect to the artifact CDN")
        except urllib.error.HTTPError as error:
            if error.code not in (301, 302, 303, 307, 308):
                raise
            location = error.headers["Location"]
        if urllib.parse.urlparse(location).scheme != "https":
            raise ValueError("Artifact download did not redirect to HTTPS")
        # The signed CDN URL needs no GitHub credential.
        headers = {"Range": f"bytes={received}-"} if received else {}
        request = urllib.request.Request(location, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                if received and response.status != 206:
                    raise ValueError("CDN did not honor the byte-range request")
                if received and not response.headers.get("Content-Range", "").startswith(f"bytes {received}-"):
                    raise ValueError("CDN returned an unexpected byte range")
                with partial.open("ab") as dest:
                    while block := response.read(4 * 1024 * 1024):
                        dest.write(block)
                        digest.update(block)
                        received += len(block)
                        if received // (128 * 1024 * 1024) != (received - len(block)) // (128 * 1024 * 1024):
                            print(f"Downloaded {received / (1024 ** 3):.2f} GiB", flush=True)
        except (ConnectionError, TimeoutError, OSError) as error:
            print(f"Download interrupted at {received:,} bytes; retrying ({type(error).__name__})", flush=True)
    if received != expected_size or digest.hexdigest() != expected:
        raise ValueError(
            f"Artifact size or SHA-256 mismatch: {received:,} bytes, {digest.hexdigest()}"
        )
    os.replace(partial, output)
    print(f"Verified: {output}\nSHA-256: {expected}", flush=True)


if __name__ == "__main__":
    main()
