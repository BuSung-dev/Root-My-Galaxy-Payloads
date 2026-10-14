#!/usr/bin/env python3
"""Check the Fold6 shell bundle without executing a device payload."""

from pathlib import Path
import hashlib
import json
import struct
from urllib.parse import urlsplit


REPOSITORY = Path(__file__).resolve().parent.parent
SHELL_PROFILES = {
    "q6q-F956BXXS4DZH5",
    "q6q-F956BXXS4DZI1",
    "q6q-F956BXXU4EZIH",
}
FEED_PREFIX = "/BuSung-dev/Root-My-Galaxy-Payloads/main/"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify_executable(path):
    data = path.read_bytes()
    require(data[:6] == b"\x7fELF\x02\x01", f"Not ELF64 little-endian: {path}")
    header = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
    require(header[0] == 3 and header[1] == 183, f"Not AArch64 ET_DYN: {path}")
    require(header[3] != 0, f"Executable entry point is absent: {path}")
    interpreter = None
    pie_flag = False
    for index in range(header[9]):
        ph = struct.unpack_from("<IIQQQQQQ", data, header[4] + index * header[8])
        if ph[0] == 3:
            interpreter = data[ph[2]:ph[2] + ph[5]].rstrip(b"\0")
        elif ph[0] == 2:
            for offset in range(ph[2], ph[2] + ph[5], 16):
                tag, value = struct.unpack_from("<QQ", data, offset)
                if tag == 0:
                    break
                if tag == 0x6FFFFFFB and value & 0x08000000:
                    pie_flag = True
    require(interpreter == b"/system/bin/linker64", f"Android interpreter absent: {path}")
    require(pie_flag, f"DF_1_PIE flag absent: {path}")


def main():
    feed = json.loads((REPOSITORY / "support/targets-v3.json").read_text())
    require(feed["schemaVersion"] == 3, "Unexpected app feed schema")
    ids = [entry["payloadId"] for entry in feed["payloads"]]
    require(len(ids) == len(set(ids)), "Duplicate app payload IDs")
    require(SHELL_PROFILES.isdisjoint(ids), "A shell executable is in the app feed")
    for profile in feed["payloads"]:
        for kind in ("exploit", "kernelsu"):
            artifact = profile[kind]
            parsed = urlsplit(artifact["url"])
            require(parsed.scheme == "https" and parsed.netloc == "raw.githubusercontent.com",
                    f"Unexpected artifact host: {profile['payloadId']}")
            require(parsed.path.startswith(FEED_PREFIX) and not parsed.query and not parsed.fragment,
                    f"URL cannot be pinned by the app: {artifact['url']}")
            relative = parsed.path.removeprefix(FEED_PREFIX)
            size = artifact["size"]
            require(type(size) is int and size > 0, f"Invalid artifact size: {relative}")
            require((REPOSITORY / relative).stat().st_size == size,
                    f"Artifact size differs from the app feed: {relative}")

    catalog = json.loads((REPOSITORY / "support/experimental.json").read_text())
    require({entry["profileId"] for entry in catalog["profiles"]} == SHELL_PROFILES,
            "Unexpected Fold6 experimental catalog")
    for profile in catalog["profiles"]:
        require(profile["rootMyGalaxyAppCompatible"] is False,
                f"Shell profile claims app compatibility: {profile['profileId']}")
        require(profile["includedInSupportFeed"] is False,
                f"Shell profile claims app feed inclusion: {profile['profileId']}")
        documentation = REPOSITORY / profile["documentation"]
        require(documentation.is_file(), f"Missing firmware record: {documentation}")
        record = documentation.read_text()
        require(profile["kernelRelease"] in record, f"Kernel identity absent: {documentation}")
        for artifact in profile["artifacts"]:
            path = REPOSITORY / artifact["path"]
            data = path.read_bytes()
            require(len(data) == artifact["size"], f"Catalog size mismatch: {path}")
            require(hashlib.sha256(data).hexdigest() == artifact["sha256"],
                    f"Catalog hash mismatch: {path}")
            require(artifact["sha256"] in record, f"Hash absent from firmware record: {path}")
        verify_executable(REPOSITORY / profile["artifacts"][0]["path"])

    metadata = json.loads((REPOSITORY / "experimental/q6q-F956BXXU4EZIH/profile.json").read_text())
    ezih = next(entry for entry in catalog["profiles"] if entry["profileId"] == metadata["profileId"])
    require(metadata["artifacts"] == ezih["artifacts"], "EZIH source metadata differs from catalog")
    print(f"PASS: {len(ids)} app entries and {len(SHELL_PROFILES)} Fold6 shell contributions")


if __name__ == "__main__":
    main()
