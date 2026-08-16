#!/usr/bin/env python3
"""Validate the complete public Neo 2 front-light calibration contract."""

import math
import pathlib
import re
import sys


EXPECTED_KEYS = {
    "schema",
    "calibration_id",
    "max_power_multiple",
    "warm_percentage",
    "cold_primary_path",
    "cold_secondary_path",
    "warm_primary_path",
    "warm_secondary_path",
    "brightness_codes",
    "cold_power",
    "warm_power",
    "drive_codes",
    "drive_power",
}
ENDPOINT = re.compile(r"/sys/[A-Za-z0-9._:/-]+\Z")
IDENTITY = re.compile(r"[A-Za-z0-9._:+-]{1,160}\Z")


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def load(path: pathlib.Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            fail("malformed calibration line")
        key, value = line.split("=", 1)
        if key not in EXPECTED_KEYS:
            fail(f"unknown calibration key: {key}")
        if key in values:
            fail(f"duplicate calibration key: {key}")
        values[key] = value
    missing = EXPECTED_KEYS - values.keys()
    if missing:
        fail("missing calibration keys: " + ",".join(sorted(missing)))
    return values


def integer_array(values: dict[str, str], key: str, size: int) -> None:
    try:
        parsed = [int(value, 10) for value in values[key].split(",")]
    except ValueError as error:
        fail(f"invalid integer array: {key}: {error}")
    if len(parsed) != size or any(value < 0 or value > 65535 for value in parsed):
        fail(f"invalid integer array length or range: {key}")
    if any(right <= left for left, right in zip(parsed, parsed[1:])):
        fail(f"integer array is not strictly increasing: {key}")


def power_array(values: dict[str, str], key: str, size: int) -> None:
    try:
        parsed = [float(value) for value in values[key].split(",")]
    except ValueError as error:
        fail(f"invalid power array: {key}: {error}")
    if len(parsed) != size or any(not math.isfinite(value) or value < 0.0 for value in parsed):
        fail(f"invalid power array length or range: {key}")


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: validate-neo2-frontlight-calibration.py FILE")
    path = pathlib.Path(sys.argv[1])
    if not path.is_file() or path.is_symlink():
        fail("calibration must be a regular file")
    values = load(path)
    if values["schema"] != "neo2-frontlight-v1":
        fail("wrong calibration schema")
    if not IDENTITY.fullmatch(values["calibration_id"]):
        fail("invalid calibration identity")
    try:
        maximum = float(values["max_power_multiple"])
        warm_percentage = int(values["warm_percentage"], 10)
    except ValueError as error:
        fail(f"invalid calibration policy: {error}")
    if not math.isfinite(maximum) or maximum <= 0.0 or maximum > 2.0:
        fail("max_power_multiple must be greater than 0 and at most 2")
    if warm_percentage < 1 or warm_percentage > 200:
        fail("warm_percentage must be between 1 and 200")
    for key in ("cold_primary_path", "warm_primary_path"):
        endpoint = values[key]
        if not ENDPOINT.fullmatch(endpoint) or ".." in endpoint:
            fail(f"invalid primary endpoint: {key}")
    for key in ("cold_secondary_path", "warm_secondary_path"):
        endpoint = values[key]
        if endpoint and (not ENDPOINT.fullmatch(endpoint) or ".." in endpoint):
            fail(f"invalid secondary endpoint: {key}")
    integer_array(values, "brightness_codes", 31)
    integer_array(values, "drive_codes", 252)
    power_array(values, "cold_power", 31)
    power_array(values, "warm_power", 31)
    power_array(values, "drive_power", 252)


if __name__ == "__main__":
    main()
