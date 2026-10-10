"""Download a private Actions artifact in verified byte ranges.

Usage: python download_artifact_parallel.py ARTIFACT_ID OUTPUT_ZIP
An existing OUTPUT_ZIP.part from download_artifact.py is reused as a prefix.
Credentials stay in memory. The final GitHub SHA-256 digest is mandatory.
"""

import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from download_artifact import REPO, github_get


def credential() -> str:
    result = subprocess.run(
        ["git", "-c", "credential.interactive=false", "credential", "fill"],
        input="protocol=https\nhost=github.com\n\n",
        text=True,
        capture_output=True,
        check=True,
        timeout=20,
    )
    fields = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    return fields["password"]


def signed_url(api: str, token: str) -> str:
    try:
        github_get(api + "/zip", token)
    except urllib.error.HTTPError as error:
        if error.code not in (301, 302, 303, 307, 308):
            raise
        url = error.headers["Location"]
        if urllib.parse.urlparse(url).scheme != "https":
            raise ValueError("Artifact CDN URL is not HTTPS")
        return url
    raise ValueError("GitHub did not redirect to the artifact CDN")


def download_range(url: str, start: int, end: int, path: Path) -> None:
    expected = end - start + 1
    for attempt in range(12):
        received = path.stat().st_size if path.exists() else 0
        if received == expected:
            return
        if received > expected:
            raise ValueError(f"Segment too large: {path}")
        request = urllib.request.Request(
            url, headers={"Range": f"bytes={start + received}-{end}"}
        )
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                if response.status != 206:
                    raise ValueError("CDN did not honor the byte range")
                wanted = f"bytes {start + received}-{end}/"
                if not response.headers.get("Content-Range", "").startswith(wanted):
                    raise ValueError("Unexpected CDN Content-Range")
                with path.open("ab") as dest:
                    while block := response.read(4 * 1024 * 1024):
                        dest.write(block)
        except (ConnectionError, TimeoutError, urllib.error.URLError, OSError):
            if attempt == 11:
                raise
            time.sleep(min(2**attempt, 30))
    if path.stat().st_size != expected:
        raise ValueError(f"Incomplete segment: {path}")


def main() -> None:
    artifact_id = int(sys.argv[1])
    output = Path(sys.argv[2]).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    token = credential()
    api = f"https://api.github.com/repos/{REPO}/actions/artifacts/{artifact_id}"
    with github_get(api, token) as response:
        metadata = json.load(response)
    if metadata["expired"] or metadata["id"] != artifact_id:
        raise ValueError("Artifact expired or ID mismatch")
    expected_size = metadata["size_in_bytes"]
    expected_digest = metadata["digest"].removeprefix("sha256:")
    prefix = output.with_suffix(output.suffix + ".part")
    prefix_size = prefix.stat().st_size if prefix.exists() else 0
    if prefix_size > expected_size:
        raise ValueError("Existing prefix exceeds artifact size")
    print(f"Artifact {artifact_id}: {expected_size:,} bytes; reusing {prefix_size:,} bytes", flush=True)

    workers = 8
    remaining = expected_size - prefix_size
    width = (remaining + workers - 1) // workers
    segments = []
    for index in range(workers):
        start = prefix_size + index * width
        if start >= expected_size:
            break
        end = min(expected_size - 1, start + width - 1)
        segments.append((start, end, output.with_name(output.name + f".range-{index:02d}")))

    url = signed_url(api, token)
    if segments:
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(segments)) as pool:
            futures = [pool.submit(download_range, url, start, end, path)
                       for start, end, path in segments]
            for future in concurrent.futures.as_completed(futures):
                future.result()
                print(f"Completed {sum(f.done() and not f.exception() for f in futures)}/{len(futures)} ranges", flush=True)

    merged = output.with_suffix(output.suffix + ".merge")
    digest = hashlib.sha256()
    count = 0
    with merged.open("wb") as dest:
        for path in ([prefix] if prefix_size else []) + [item[2] for item in segments]:
            with path.open("rb") as src:
                while block := src.read(4 * 1024 * 1024):
                    dest.write(block)
                    digest.update(block)
                    count += len(block)
    if count != expected_size or digest.hexdigest() != expected_digest:
        raise ValueError("Artifact size or SHA-256 mismatch")
    os.replace(merged, output)
    print(f"Verified: {output}; SHA-256: {expected_digest}", flush=True)


if __name__ == "__main__":
    main()
