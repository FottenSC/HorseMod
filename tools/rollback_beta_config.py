#!/usr/bin/env python3
"""Generate and validate persistent HorseMod rollback beta profiles."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import tempfile


LEGACY_CONFIG_VERSION = 1
CONFIG_VERSION = 2
DEFAULT_PORT = 47170
MAX_PROFILE_BYTES = 16 * 1024
VALID_KEYS = {
    "config_version",
    "enabled",
    "transport",
    "role",
    "bind_address",
    "bind_port",
    "peer_address",
    "peer_port",
    "secret",
    "rollback_window",
    "input_delay",
    "trace",
}
COMMON_REQUIRED_KEYS = {"config_version", "enabled"}
DIRECT_REQUIRED_KEYS = {"role", "peer_address", "peer_port", "secret"}
PEER_RE = re.compile(r"^[A-Za-z0-9.-]{1,253}$")
SECRET_RE = re.compile(r"^[0-9a-fA-F]{64}$")


def valid_secret(value: str) -> bool:
    if not SECRET_RE.fullmatch(value):
        return False
    return len(set(bytes.fromhex(value))) >= 16


def parse_bool(value: str) -> bool:
    lowered = value.strip().lower()
    if lowered in {"1", "true", "yes", "on"}:
        return True
    if lowered in {"0", "false", "no", "off"}:
        return False
    raise ValueError("boolean value is invalid")


def parse_profile_text(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"line {number} is missing '='")
        key, value = (part.strip() for part in line.split("=", 1))
        key = key.lower()
        if key not in VALID_KEYS:
            raise ValueError(f"unknown key on line {number}: {key}")
        if key in values:
            raise ValueError(f"duplicate key on line {number}: {key}")
        values[key] = value
    missing = sorted(COMMON_REQUIRED_KEYS - values.keys())
    if missing:
        raise ValueError("missing required keys: " + ",".join(missing))
    version = int(values["config_version"], 10)
    if version not in {LEGACY_CONFIG_VERSION, CONFIG_VERSION}:
        raise ValueError("unsupported config_version")
    if not parse_bool(values["enabled"]):
        raise ValueError("profile is disabled")
    transport = values.get("transport")
    if version == CONFIG_VERSION:
        if transport not in {"steam-p2p", "direct-udp"}:
            raise ValueError(
                "transport must be steam-p2p or direct-udp")
    else:
        transport = "direct-udp"
    direct = transport == "direct-udp"
    if direct:
        missing = sorted(DIRECT_REQUIRED_KEYS - values.keys())
        if missing:
            raise ValueError("missing required keys: " + ",".join(missing))
    if direct and values["role"].lower() not in {"host", "guest"}:
        raise ValueError("role must be host or guest")
    if direct and not PEER_RE.fullmatch(values["peer_address"]):
        raise ValueError("peer_address must be an IPv4 address or DNS name")
    for key in ("bind_port", "peer_port", "rollback_window", "input_delay"):
        if key not in values:
            continue
        parsed = int(values[key], 10)
        if parsed <= 0 or parsed > 65535:
            raise ValueError(f"{key} is outside 1..65535")
    window = int(values.get("rollback_window", "12"), 10)
    delay = int(values.get("input_delay", "1"), 10)
    if window > 60:
        raise ValueError("rollback_window is outside 1..60")
    if delay > window:
        raise ValueError("input_delay exceeds rollback_window")
    secret_value = values.get("secret", "")
    if direct and not valid_secret(secret_value):
        raise ValueError(
            "secret must be 64 hexadecimal characters with strong entropy")
    if "trace" in values:
        parse_bool(values["trace"])
    return values


def render_profile(
    *,
    role: str,
    peer_address: str,
    bind_port: int,
    peer_port: int,
    secret_value: str,
    rollback_window: int,
    input_delay: int,
    trace: bool,
) -> str:
    return (
        "# HorseMod rollback beta profile. Keep secret private.\n"
        f"config_version={CONFIG_VERSION}\n"
        "enabled=true\n"
        "transport=direct-udp\n"
        f"role={role}\n"
        "bind_address=0.0.0.0\n"
        f"bind_port={bind_port}\n"
        f"peer_address={peer_address}\n"
        f"peer_port={peer_port}\n"
        f"secret={secret_value}\n"
        f"rollback_window={rollback_window}\n"
        f"input_delay={input_delay}\n"
        f"trace={'true' if trace else 'false'}\n"
    )


def render_steam_profile(
    *,
    rollback_window: int,
    input_delay: int,
    trace: bool,
) -> str:
    return (
        "# HorseMod rollback beta profile. Steam connects the lobby peers;\n"
        "# no IP address, port forwarding, or shared secret is required.\n"
        f"config_version={CONFIG_VERSION}\n"
        "enabled=true\n"
        "transport=steam-p2p\n"
        f"rollback_window={rollback_window}\n"
        f"input_delay={input_delay}\n"
        f"trace={'true' if trace else 'false'}\n"
    )


def _lock_down_private_file(path: Path) -> None:
    if os.name != "nt":
        os.chmod(path, 0o600)
        if path.stat().st_mode & 0o077:
            raise OSError(f"private permissions were not applied: {path}")
        return

    identity = subprocess.run(
        ["whoami", "/user", "/fo", "csv", "/nh"],
        check=True,
        capture_output=True,
        text=True,
    )
    rows = list(csv.reader(identity.stdout.splitlines()))
    if len(rows) != 1 or len(rows[0]) < 2 \
            or re.fullmatch(r"S-\d(?:-\d+)+", rows[0][1]) is None:
        raise OSError("cannot determine the current Windows user SID")
    sid = rows[0][1]
    subprocess.run(
        [
            "icacls",
            str(path),
            "/inheritance:r",
            "/grant:r",
            f"*{sid}:(F)",
        ],
        check=True,
        capture_output=True,
        text=True,
    )


def write_private(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    try:
        temporary.write_text(text, encoding="utf-8")
        _lock_down_private_file(temporary)
        temporary.replace(path)
        # Apply and verify by exit status again after replacement so an
        # existing destination ACL cannot survive the atomic update.
        _lock_down_private_file(path)
    finally:
        if temporary.exists():
            temporary.unlink()


def read_profile(path: Path) -> str:
    raw = path.read_bytes()
    if not raw or len(raw) > MAX_PROFILE_BYTES:
        raise ValueError(
            f"profile size must be within 1..{MAX_PROFILE_BYTES} bytes")
    return raw.decode("utf-8")


def validate_generation_args(args: argparse.Namespace) -> None:
    for label, value in (
        ("bind port", args.bind_port),
        ("peer port", args.peer_port),
    ):
        if value <= 0 or value > 65535:
            raise ValueError(f"{label} is outside 1..65535")
    if args.rollback_window <= 0 or args.rollback_window > 60:
        raise ValueError("rollback window is outside 1..60")
    if args.input_delay <= 0 or args.input_delay > args.rollback_window:
        raise ValueError("input delay is outside the rollback window")
    if not PEER_RE.fullmatch(args.peer_address):
        raise ValueError("peer address must be an IPv4 address or DNS name")
    if not valid_secret(args.secret):
        raise ValueError(
            "secret must be 64 hexadecimal characters with strong entropy")


def single(args: argparse.Namespace) -> int:
    if args.secret is None:
        args.secret = secrets.token_hex(32)
    validate_generation_args(args)
    text = render_profile(
        role=args.role,
        peer_address=args.peer_address,
        bind_port=args.bind_port,
        peer_port=args.peer_port,
        secret_value=args.secret,
        rollback_window=args.rollback_window,
        input_delay=args.input_delay,
        trace=args.trace,
    )
    parse_profile_text(text)
    write_private(args.output, text)
    print(f"wrote {args.role} beta profile: {args.output.resolve()}")
    print("Share the same secret out-of-band; do not post either profile.")
    return 0


def steam(args: argparse.Namespace) -> int:
    if args.rollback_window <= 0 or args.rollback_window > 60:
        raise ValueError("rollback window is outside 1..60")
    if args.input_delay <= 0 or args.input_delay > args.rollback_window:
        raise ValueError("input delay is outside the rollback window")
    text = render_steam_profile(
        rollback_window=args.rollback_window,
        input_delay=args.input_delay,
        trace=args.trace,
    )
    parse_profile_text(text)
    write_private(args.output, text)
    print(f"wrote Steam P2P beta profile: {args.output.resolve()}")
    print("Use the same profile on both clients; Steam supplies peer routing.")
    return 0


def pair(args: argparse.Namespace) -> int:
    secret_value = args.secret or secrets.token_hex(32)
    common = {
        "rollback_window": args.rollback_window,
        "input_delay": args.input_delay,
        "trace": args.trace,
    }
    host_text = render_profile(
        role="host",
        peer_address=args.guest_public_address,
        bind_port=args.host_port,
        peer_port=args.guest_port,
        secret_value=secret_value,
        **common,
    )
    guest_text = render_profile(
        role="guest",
        peer_address=args.host_public_address,
        bind_port=args.guest_port,
        peer_port=args.host_port,
        secret_value=secret_value,
        **common,
    )
    parse_profile_text(host_text)
    parse_profile_text(guest_text)
    host_path = args.output_dir / "host" / "rollback_beta.ini"
    guest_path = args.output_dir / "guest" / "rollback_beta.ini"
    write_private(host_path, host_text)
    write_private(guest_path, guest_text)
    manifest = {
        "schema_version": 1,
        "classification": "rollback-beta-profile-pair",
        "transport": "direct-udp",
        "host": {
            "profile": str(host_path.resolve()),
            "public_endpoint":
                f"{args.host_public_address}:{args.host_port}",
            "sha256": hashlib.sha256(
                host_text.encode("utf-8")).hexdigest(),
        },
        "guest": {
            "profile": str(guest_path.resolve()),
            "public_endpoint":
                f"{args.guest_public_address}:{args.guest_port}",
            "sha256": hashlib.sha256(
                guest_text.encode("utf-8")).hexdigest(),
        },
        "shared_secret_sha256": hashlib.sha256(
            secret_value.encode("utf-8")).hexdigest(),
    }
    manifest_path = args.output_dir / "rollback_beta_pair.json"
    write_private(
        manifest_path,
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
    )
    print(f"wrote host profile: {host_path.resolve()}")
    print(f"wrote guest profile: {guest_path.resolve()}")
    print(f"wrote non-secret manifest: {manifest_path.resolve()}")
    print("Distribute each profile privately to its named player.")
    return 0


def validate(args: argparse.Namespace) -> int:
    values = parse_profile_text(read_profile(args.profile))
    version = int(values["config_version"])
    transport = values.get(
        "transport",
        "direct-udp" if version == LEGACY_CONFIG_VERSION else "")
    summary = {
        "ok": True,
        "config_version": version,
        "transport": transport,
    }
    if transport == "direct-udp":
        summary.update({
            "role": values["role"].lower(),
            "bind_endpoint":
                f"{values.get('bind_address', '0.0.0.0')}:"
                f"{values.get('bind_port', DEFAULT_PORT)}",
            "peer_endpoint":
                f"{values['peer_address']}:{values['peer_port']}",
            "secret_sha256": hashlib.sha256(
                values["secret"].encode("utf-8")).hexdigest(),
        })
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def selftest() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        args = argparse.Namespace(
            host_public_address="198.51.100.10",
            guest_public_address="peer.example.net",
            host_port=47170,
            guest_port=47171,
            secret="".join(f"{value:02x}" for value in range(32)),
            rollback_window=12,
            input_delay=1,
            trace=False,
            output_dir=root,
        )
        steam_text = render_steam_profile(
            rollback_window=12, input_delay=1, trace=False)
        steam_values = parse_profile_text(steam_text)
        pair(args)
        host = parse_profile_text(
            (root / "host" / "rollback_beta.ini").read_text(
                encoding="utf-8"))
        guest = parse_profile_text(
            (root / "guest" / "rollback_beta.ini").read_text(
                encoding="utf-8"))
        manifest = json.loads(
            (root / "rollback_beta_pair.json").read_text(
                encoding="utf-8"))
        ok = (
            steam_values["transport"] == "steam-p2p"
            and "peer_address" not in steam_values
            and "secret" not in steam_values
            and host["role"] == "host"
            and host["peer_address"] == "peer.example.net"
            and guest["role"] == "guest"
            and guest["peer_address"] == "198.51.100.10"
            and host["secret"] == guest["secret"]
            and manifest["transport"] == "direct-udp"
            and "secret" not in manifest
        )
        rejected = False
        try:
            parse_profile_text(
                "config_version=2\nenabled=true\ntransport=direct-udp\n"
                "role=host\n"
                "peer_address=bad/address\npeer_port=47170\n"
                "secret=0123456789abcdef0123456789abcdef\n")
        except ValueError:
            rejected = True
        weak_secret_rejected = False
        try:
            parse_profile_text(
                "config_version=2\nenabled=true\ntransport=direct-udp\n"
                "role=host\n"
                "peer_address=peer.example.net\npeer_port=47170\n"
                f"secret={'0' * 64}\n")
        except ValueError:
            weak_secret_rejected = True
        oversized_path = root / "oversized.ini"
        oversized_path.write_bytes(b"x" * (MAX_PROFILE_BYTES + 1))
        oversized_rejected = False
        try:
            read_profile(oversized_path)
        except ValueError:
            oversized_rejected = True
        ok = ok and rejected and weak_secret_rejected \
            and oversized_rejected
    print(f"rollback beta config tool self-test "
          f"{'passed' if ok else 'failed'}")
    return 0 if ok else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    subparsers = parser.add_subparsers(dest="command")

    steam_parser = subparsers.add_parser(
        "steam", help="generate the zero-port-forwarding Steam P2P profile")
    steam_parser.add_argument("--rollback-window", type=int, default=12)
    steam_parser.add_argument("--input-delay", type=int, default=1)
    steam_parser.add_argument("--trace", action="store_true")
    steam_parser.add_argument("--output", type=Path, required=True)
    steam_parser.set_defaults(func=steam)

    single_parser = subparsers.add_parser(
        "single", help="generate one legacy direct-UDP profile")
    single_parser.add_argument("--role", choices=("host", "guest"),
                               required=True)
    single_parser.add_argument("--peer-address", required=True)
    single_parser.add_argument("--bind-port", type=int, default=DEFAULT_PORT)
    single_parser.add_argument("--peer-port", type=int, default=DEFAULT_PORT)
    single_parser.add_argument("--secret")
    single_parser.add_argument("--rollback-window", type=int, default=12)
    single_parser.add_argument("--input-delay", type=int, default=1)
    single_parser.add_argument("--trace", action="store_true")
    single_parser.add_argument("--output", type=Path, required=True)
    single_parser.set_defaults(func=single)

    pair_parser = subparsers.add_parser(
        "pair", help="generate matched direct-UDP profiles")
    pair_parser.add_argument("--host-public-address", required=True)
    pair_parser.add_argument("--guest-public-address", required=True)
    pair_parser.add_argument("--host-port", type=int, default=DEFAULT_PORT)
    pair_parser.add_argument("--guest-port", type=int, default=DEFAULT_PORT)
    pair_parser.add_argument("--secret")
    pair_parser.add_argument("--rollback-window", type=int, default=12)
    pair_parser.add_argument("--input-delay", type=int, default=1)
    pair_parser.add_argument("--trace", action="store_true")
    pair_parser.add_argument("--output-dir", type=Path, required=True)
    pair_parser.set_defaults(func=pair)

    validate_parser = subparsers.add_parser(
        "validate", help="validate a profile without printing its secret")
    validate_parser.add_argument("profile", type=Path)
    validate_parser.set_defaults(func=validate)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not hasattr(args, "func"):
        parser.error("choose steam, single, pair, or validate")
    try:
        return args.func(args)
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"rollback beta config failed: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
