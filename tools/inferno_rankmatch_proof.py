#!/usr/bin/env python3
"""Authorized SC6 Inferno rankmatch leaderboard proof harness.

This version authenticates only with the Steam account configured in a
SC6ReplayArchive-style .env file. It does not use the Steam desktop client's
currently logged-in account.

Required .env keys:
  STEAM_USERNAME
  STEAM_PASSWORD
  STEAM_SHARED_SECRET

python .\inferno_rankmatch_proof.py --env .\.env write --score 2147483647 --max-score 2147483647 --i-understand-production-write

Steam does not expose a client-side "delete my leaderboard row" API. The
restore action restores the saved score/details. If no backup exists,
--restore-without-backup writes a neutral score/details row instead.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import os
import shutil
import struct
import sys
import textwrap
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    import requests
    from steam.client import SteamClient
    from steam.core.msg import MsgProto
    from steam.enums import EOSType, EResult
    from steam.enums.emsg import EMsg
    from steam.guard import generate_twofactor_code
    from steam.steamid import SteamID
    from steam.utils import ip4_to_int
    from steam.webauth import intBase, pkcs1v15_encrypt, rsa_publickey
except ImportError as exc:  # pragma: no cover - user environment check
    missing = exc.name or "dependency"
    raise SystemExit(
        f"Missing Python package '{missing}'. Install dependencies on the authorized machine:\n"
        "  py -m pip install steam requests\n"
        "or:\n"
        "  python -m pip install steam requests"
    ) from exc


SC6_APP_ID = 544750
DEFAULT_LEADERBOARD = "RankmatchWorld"
DEFAULT_STYLE_ID = 26  # ELuxFightStyle::EFS_INFERNO
DEFAULT_SCORE_GUARD = 1

AUTH_BASE = "https://api.steampowered.com/IAuthenticationService"
DEVICE_NAME = "SC6 Inferno Proof"
STEAM_CLIENT_PLATFORM = 1
PERSISTENT_SESSION = 1
DEVICE_CODE_GUARD = 3
EMAIL_CODE_GUARD = 2
STEAM_CLIENT_WEBSITE_ID = "Unknown"
STEAM_CLIENT_DEVICE_TYPE = 1

UPLOAD_KEEP_BEST = 1
UPLOAD_FORCE_UPDATE = 2

RANKMATCH_LAYOUT_NOTE = (
    "Rankmatch details layout used by SC6ReplayArchive: "
    "[0]=region, [1]=language, [2]=style_id, [3]=rank"
)


@dataclass
class SteamAccessToken:
    access_token: str
    refresh_token: str = ""
    steamid: str = ""
    account_name: str = ""


class ModernAuthError(RuntimeError):
    def __init__(self, result: EResult, message: str) -> None:
        super().__init__(message)
        self.result = result


class ModernSteamAuthenticator:
    """Minimal copy of SC6ReplayArchive's modern Steam auth flow."""

    def __init__(self, username: str, password: str, shared_secret: str, tokens_dir: Path) -> None:
        self._username = username
        self._password = password
        self._shared_secret = shared_secret
        self._tokens_dir = tokens_dir
        self._tokens_dir.mkdir(parents=True, exist_ok=True)
        self._session = requests.Session()

    @property
    def _refresh_path(self) -> Path:
        return self._tokens_dir / f"{self._username}.refresh_token.json"

    @property
    def _guard_path(self) -> Path:
        return self._tokens_dir / f"{self._username}.guard_data"

    def get_client_logon_token(self, *, force_credentials: bool = False) -> SteamAccessToken:
        stored = None if force_credentials else self._load_refresh_token()
        if stored is not None:
            return stored
        token = self._credentials_flow()
        self._save_refresh_token(token)
        return token

    def _load_refresh_token(self) -> SteamAccessToken | None:
        if not self._refresh_path.exists():
            return None
        try:
            data = json.loads(self._refresh_path.read_text(encoding="utf-8"))
            refresh = str(data.get("refresh_token") or "")
            steamid = str(data.get("steamid") or "")
            account_name = str(data.get("account_name") or self._username)
        except (OSError, json.JSONDecodeError):
            self._refresh_path.unlink(missing_ok=True)
            return None
        if not refresh:
            self._refresh_path.unlink(missing_ok=True)
            return None
        return SteamAccessToken(
            access_token=str(data.get("access_token") or ""),
            refresh_token=refresh,
            steamid=steamid,
            account_name=account_name,
        )

    def _save_refresh_token(self, token: SteamAccessToken) -> None:
        if not token.refresh_token:
            return
        data = {
            "refresh_token": token.refresh_token,
            "steamid": token.steamid,
            "account_name": token.account_name or self._username,
        }
        self._refresh_path.write_text(json.dumps(data, sort_keys=True), encoding="utf-8")

    def _credentials_flow(self) -> SteamAccessToken:
        if not self._password:
            raise ModernAuthError(EResult.InvalidPassword, "missing STEAM_PASSWORD")

        rsa = self._call("GetPasswordRSAPublicKey", {"account_name": self._username}, http_method="get")
        encrypted_password = self._encrypt_password(
            str(rsa["publickey_mod"]),
            str(rsa["publickey_exp"]),
            self._password,
        )
        payload: dict[str, Any] = {
            "account_name": self._username,
            "encrypted_password": encrypted_password,
            "encryption_timestamp": int(rsa["timestamp"]),
            "remember_login": True,
            "persistence": PERSISTENT_SESSION,
            "website_id": STEAM_CLIENT_WEBSITE_ID,
            "platform_type": STEAM_CLIENT_PLATFORM,
            "device_friendly_name": DEVICE_NAME,
            "device_details": {
                "device_friendly_name": DEVICE_NAME,
                "platform_type": STEAM_CLIENT_PLATFORM,
                "os_type": int(EOSType.Windows10),
                "gaming_device_type": STEAM_CLIENT_DEVICE_TYPE,
            },
        }
        guard_data = self._load_guard_data()
        if guard_data:
            payload["guard_data"] = guard_data

        begin = self._call("BeginAuthSessionViaCredentials", payload)
        client_id = begin.get("client_id")
        request_id = begin.get("request_id")
        steamid = str(begin.get("steamid") or "")
        if not client_id or not request_id:
            raise ModernAuthError(EResult.Fail, "Steam auth did not return a session")

        guard_type = self._choose_guard_type(begin.get("allowed_confirmations", []))
        if guard_type == DEVICE_CODE_GUARD:
            self._submit_device_code(client_id, steamid)
        elif guard_type == EMAIL_CODE_GUARD:
            raise ModernAuthError(
                EResult.AccountLogonDenied,
                "email Steam Guard code required; use an account with STEAM_SHARED_SECRET",
            )
        elif guard_type is not None:
            raise ModernAuthError(
                EResult.AccountLoginDeniedNeedTwoFactor,
                f"unsupported guard confirmation type {guard_type}",
            )

        token = self._poll_session(
            client_id=client_id,
            request_id=request_id,
            interval=float(begin.get("interval") or 1.0),
        )
        if steamid and not token.steamid:
            token.steamid = steamid
        if not token.account_name:
            token.account_name = self._username
        return token

    def _submit_device_code(self, client_id: Any, steamid: str) -> None:
        if not self._shared_secret:
            raise ModernAuthError(
                EResult.AccountLoginDeniedNeedTwoFactor,
                "Steam Guard device code required but STEAM_SHARED_SECRET is empty",
            )
        try:
            shared_secret = base64.b64decode(self._shared_secret, validate=True)
        except (binascii.Error, ValueError) as exc:
            raise ModernAuthError(EResult.InvalidLoginAuthCode, "STEAM_SHARED_SECRET is not valid base64") from exc
        code = generate_twofactor_code(shared_secret)
        self._call(
            "UpdateAuthSessionWithSteamGuardCode",
            {
                "client_id": client_id,
                "steamid": steamid,
                "code": code,
                "code_type": DEVICE_CODE_GUARD,
            },
        )

    def _poll_session(
        self,
        *,
        client_id: Any,
        request_id: Any,
        interval: float,
        attempts: int = 12,
    ) -> SteamAccessToken:
        delay = min(max(interval, 0.5), 5.0)
        last: dict[str, Any] = {}
        for _ in range(attempts):
            time.sleep(delay)
            last = self._call(
                "PollAuthSessionStatus",
                {"client_id": client_id, "request_id": request_id},
            )
            if last.get("new_guard_data"):
                self._save_guard_data(str(last["new_guard_data"]))
            if last.get("access_token") or last.get("refresh_token"):
                return SteamAccessToken(
                    access_token=str(last.get("access_token") or ""),
                    refresh_token=str(last.get("refresh_token") or ""),
                    steamid=str(last.get("steamid") or ""),
                    account_name=str(last.get("account_name") or self._username),
                )
        raise ModernAuthError(EResult.Timeout, f"timed out waiting for Steam auth tokens; last_keys={sorted(last)}")

    def _load_guard_data(self) -> str:
        try:
            return self._guard_path.read_text(encoding="utf-8").strip()
        except OSError:
            return ""

    def _save_guard_data(self, guard_data: str) -> None:
        self._guard_path.write_text(guard_data, encoding="utf-8")

    @staticmethod
    def _choose_guard_type(confirmations: list[dict[str, Any]]) -> int | None:
        types = [int(item.get("confirmation_type", 0)) for item in confirmations]
        if DEVICE_CODE_GUARD in types:
            return DEVICE_CODE_GUARD
        if EMAIL_CODE_GUARD in types:
            return EMAIL_CODE_GUARD
        return types[0] if types else None

    @staticmethod
    def _encrypt_password(modulus: str, exponent: str, password: str) -> str:
        key = rsa_publickey(intBase(modulus, 16), intBase(exponent, 16))
        encrypted = pkcs1v15_encrypt(key, password.encode("utf-8"))
        return base64.b64encode(encrypted).decode("ascii")

    def _call(self, method: str, payload: dict[str, Any], *, http_method: str = "post") -> dict[str, Any]:
        response = self._raw_call(method, payload, http_method=http_method)
        if response.status_code == 405 and http_method == "post":
            response = self._raw_call(method, payload, http_method="get")
        if response.status_code == 429:
            raise ModernAuthError(EResult.AccountLoginDeniedThrottle, "Steam auth throttled")
        if response.status_code >= 400:
            raise ModernAuthError(EResult.Fail, f"Steam auth HTTP {response.status_code} for {method}")
        try:
            data = response.json()
        except ValueError as exc:
            raise ModernAuthError(EResult.Fail, f"Steam auth returned non-JSON for {method}") from exc
        body = data.get("response", data)
        if int(body.get("eresult", 1) or 1) != 1:
            raise ModernAuthError(_result_from_int(int(body.get("eresult"))), str(body))
        return body

    def _raw_call(self, method: str, payload: dict[str, Any], *, http_method: str) -> requests.Response:
        url = f"{AUTH_BASE}/{method}/v1/"
        data = {"input_json": json.dumps(payload)}
        if http_method == "get":
            return self._session.get(url, params=data, timeout=20)
        return self._session.post(url, data=data, timeout=20)


class EnvSteamLeaderboardClient:
    def __init__(self, env: dict[str, str], tokens_dir: Path) -> None:
        self.username = env.get("STEAM_USERNAME", "")
        self.password = env.get("STEAM_PASSWORD", "")
        self.shared_secret = env.get("STEAM_SHARED_SECRET", "")
        if not self.username or not self.password:
            raise SystemExit("STEAM_USERNAME and STEAM_PASSWORD must be set in the selected .env")
        self.tokens_dir = tokens_dir
        self.client = SteamClient()
        self.client.set_credential_location(str(tokens_dir))
        self.token: SteamAccessToken | None = None

    def __enter__(self) -> "EnvSteamLeaderboardClient":
        self.login()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.disconnect()

    def login(self) -> None:
        authenticator = ModernSteamAuthenticator(
            username=self.username,
            password=self.password,
            shared_secret=self.shared_secret,
            tokens_dir=self.tokens_dir,
        )
        token = authenticator.get_client_logon_token()
        self.token = token

        self.client.connect()
        if not self.client.connected:
            raise RuntimeError("Steam CM connection failed")

        result = self._login_with_access_token(token.refresh_token or token.access_token)
        if result != EResult.OK:
            # Stored refresh token may be stale; regenerate once.
            token = authenticator.get_client_logon_token(force_credentials=True)
            self.token = token
            result = self._login_with_access_token(token.refresh_token or token.access_token)
        if result != EResult.OK:
            raise RuntimeError(f"Steam CM login failed for .env account {self.username!r}: {result.name}")
        self.client.sleep(0.5)

    def disconnect(self) -> None:
        if self.client.connected:
            self.client.disconnect()

    def _login_with_access_token(self, access_token: str) -> EResult:
        result = self.client._pre_login()
        if result != EResult.OK:
            return result

        self.client.username = self.username
        message = MsgProto(EMsg.ClientLogon)
        message.header.steamid = SteamID(type="Individual", universe="Public")
        message.body.protocol_version = 65580
        message.body.client_package_version = 1771
        message.body.client_os_type = EOSType.Windows10
        message.body.client_language = "english"
        message.body.should_remember_password = True
        message.body.supports_rate_limit_response = True
        message.body.chat_mode = self.client.chat_mode
        obfuscated_ip = ip4_to_int(self.client.connection.local_address) ^ 0xF00DBAAD
        message.body.obfuscated_private_ip.v4 = obfuscated_ip
        message.body.deprecated_obfustucated_private_ip = obfuscated_ip
        message.body.account_name = self.username
        message.body.access_token = access_token
        message.body.machine_name = DEVICE_NAME
        self.client.send(message)
        resp = self.client.wait_msg(EMsg.ClientLogOnResponse, timeout=30)
        if resp is None:
            return EResult.Fail
        return _result_from_int(int(resp.body.eresult))

    def find_leaderboard(self, name: str, app_id: int = SC6_APP_ID):
        return self.client.get_leaderboard(app_id, name)

    def current_steam_id(self) -> int:
        steam_id = getattr(self.client, "steam_id", None)
        if steam_id:
            return int(steam_id)
        if self.token and self.token.steamid:
            return int(self.token.steamid)
        raise RuntimeError("Could not determine logged-in SteamID")

    def read_current_entry(self, leaderboard_name: str) -> tuple[dict[str, Any], Any]:
        lb = self.find_leaderboard(leaderboard_name)
        steam_id = self.current_steam_id()
        entries = lb.get_entries(
            0,
            0,
            data_request=lb.ELeaderboardDataRequest.Users,
            steam_ids=[steam_id],
        )
        if not entries:
            return self.status_common(leaderboard_name, lb, None), lb
        entry = entries[0]
        details = unpack_details_bytes(bytes(entry.details or b""))
        entry_dict = {
            "steam_id": str(entry.steam_id_user),
            "rank": int(entry.global_rank),
            "score": int(entry.score),
            "ugc_handle": str(entry.ugc_id),
            "details": details,
            "details_b64": base64.b64encode(bytes(entry.details or b"")).decode("ascii"),
        }
        return self.status_common(leaderboard_name, lb, entry_dict), lb

    def status_common(self, leaderboard_name: str, lb: Any, entry: dict[str, Any] | None) -> dict[str, Any]:
        return {
            "leaderboard": leaderboard_name,
            "leaderboard_id": int(lb.id),
            "leaderboard_entry_count": int(lb.entry_count),
            "account_source": ".env",
            "steam_username": self.username,
            "steam_id": str(self.current_steam_id()),
            "entry": entry,
        }

    def upload_score(self, lb: Any, score: int, details: list[int], *, force: bool = True) -> dict[str, Any]:
        message = MsgProto(EMsg.ClientLBSSetScore)
        message.header.routing_appid = SC6_APP_ID
        message.body.app_id = SC6_APP_ID
        message.body.leaderboard_id = int(lb.id)
        message.body.score = score
        message.body.details = pack_details_bytes(details)
        message.body.upload_score_method = UPLOAD_FORCE_UPDATE if force else UPLOAD_KEEP_BEST

        resp = self.client.send_job_and_wait(message, timeout=30)
        if not resp:
            raise RuntimeError("Timed out waiting for ClientLBSSetScoreResponse")
        result = _result_from_int(int(resp.eresult))
        return {
            "eresult": result.name,
            "success": result == EResult.OK,
            "score_changed": bool(resp.score_changed),
            "leaderboard_entry_count": int(resp.leaderboard_entry_count),
            "global_rank_previous": int(resp.global_rank_previous),
            "global_rank_new": int(resp.global_rank_new),
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Authorized low-impact SC6 Inferno rankmatch production proof using the .env Steam account.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent(
            f"""
            Flash-drive folder contents:
              inferno_rankmatch_proof.py
              .env  (approved SC6ReplayArchive Steam account)

            Examples:
              Status/read only:
                python inferno_rankmatch_proof.py --env .env status

              Dry-run payload only, no write:
                python inferno_rankmatch_proof.py --env .env write --dry-run

              Write one low-score Inferno proof row for the .env Steam account:
                python inferno_rankmatch_proof.py --env .env write --i-understand-production-write

              Restore the saved pre-proof row:
                python inferno_rankmatch_proof.py --env .env restore --i-understand-production-write

            {RANKMATCH_LAYOUT_NOTE}
            """
        ),
    )
    parser.add_argument("action", choices=("status", "write", "restore"))
    parser.add_argument("--env", type=Path, default=Path(".env"))
    parser.add_argument("--tokens-dir", type=Path, default=None)
    parser.add_argument("--leaderboard", default=DEFAULT_LEADERBOARD)
    parser.add_argument("--score", type=int, default=1)
    parser.add_argument("--max-score", type=int, default=DEFAULT_SCORE_GUARD)
    parser.add_argument("--style-id", type=int, default=DEFAULT_STYLE_ID)
    parser.add_argument("--region-id", type=int, default=7)
    parser.add_argument("--language-id", type=int, default=0)
    parser.add_argument("--rank-id", type=int, default=0)
    parser.add_argument("--neutral-style-id", type=int, default=1)
    parser.add_argument("--backup", type=Path, default=Path("inferno_rankmatch_proof_backup.json"))
    parser.add_argument("--overwrite-backup", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--restore-without-backup", action="store_true")
    parser.add_argument("--i-understand-production-write", action="store_true")
    return parser.parse_args()


def load_env(path: Path) -> dict[str, str]:
    if not path.exists():
        raise SystemExit(f"Missing .env file: {path}")
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        key = key.strip()
        value = value.strip()
        if (value.startswith('"') and value.endswith('"')) or (value.startswith("'") and value.endswith("'")):
            value = value[1:-1]
        values[key] = value
    return values


def script_base_dir() -> Path:
    return Path(__file__).resolve().parent


def resolve_path(path: Path, base: Path) -> Path:
    if path.is_absolute():
        return path
    return base / path


def pack_details_bytes(details: list[int]) -> bytes:
    return struct.pack(f"<{len(details)}i", *details)


def pack_details_b64(details: list[int]) -> str:
    return base64.b64encode(pack_details_bytes(details)).decode("ascii")


def unpack_details_bytes(payload: bytes) -> list[int]:
    count = len(payload) // 4
    if count <= 0:
        return []
    return list(struct.unpack(f"<{count}i", payload[: count * 4]))


def normalize_rankmatch_details(
    existing_entry: dict[str, Any] | None,
    *,
    region_id: int,
    language_id: int,
    style_id: int,
    rank_id: int,
) -> list[int]:
    if existing_entry and isinstance(existing_entry.get("details"), list):
        details = [int(v) for v in existing_entry["details"]]
    else:
        details = [region_id, language_id, style_id, rank_id]

    while len(details) < 4:
        defaults = [region_id, language_id, style_id, rank_id]
        details.append(defaults[len(details)])
    details[2] = style_id
    return details


def write_backup(path: Path, payload: dict[str, Any], *, overwrite: bool = False) -> bool:
    if path.exists() and not overwrite:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    tmp.replace(path)
    return True


def read_backup(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def require_write_ack(args: argparse.Namespace) -> None:
    if args.action == "status":
        return
    if args.dry_run:
        return
    if not args.i_understand_production_write:
        raise SystemExit(
            "Refusing production write without --i-understand-production-write. "
            "Run with --dry-run first and keep score low."
        )


def enforce_score_guard(args: argparse.Namespace) -> None:
    if args.action == "write" and args.score > args.max_score:
        raise SystemExit(
            f"Refusing score={args.score}; guard is --max-score {args.max_score}. "
            "For the approved non-disruptive proof, keep this at 1."
        )


def _result_from_int(value: int) -> EResult:
    try:
        return EResult(value)
    except ValueError:
        return EResult.Fail


def redact_status_for_backup(status: dict[str, Any]) -> dict[str, Any]:
    return {
        "leaderboard": status.get("leaderboard"),
        "leaderboard_id": status.get("leaderboard_id"),
        "steam_id": status.get("steam_id"),
        "steam_username": status.get("steam_username"),
        "entry": status.get("entry"),
        "note": "Saved before inferno_rankmatch_proof.py write action.",
    }


def main() -> int:
    args = parse_args()
    base = script_base_dir()
    env_path = resolve_path(args.env, Path.cwd())
    env = load_env(env_path)
    tokens_dir = args.tokens_dir or Path(env.get("STEAM_TOKENS_DIR", ""))
    if not str(tokens_dir):
        tokens_dir = env_path.parent / ".steam_tokens"
    tokens_dir = resolve_path(tokens_dir, env_path.parent)
    args.backup = resolve_path(args.backup, env_path.parent)

    enforce_score_guard(args)
    require_write_ack(args)

    with EnvSteamLeaderboardClient(env, tokens_dir) as steam:
        if args.action == "status":
            status, _lb = steam.read_current_entry(args.leaderboard)
            print(json.dumps(status, indent=2))
            return 0

        if args.action == "write":
            status, lb = steam.read_current_entry(args.leaderboard)
            entry = status.get("entry")
            backup_payload = redact_status_for_backup(status)
            details = normalize_rankmatch_details(
                entry,
                region_id=args.region_id,
                language_id=args.language_id,
                style_id=args.style_id,
                rank_id=args.rank_id,
            )
            proof = {
                "action": "write",
                "account_source": ".env",
                "steam_username": status.get("steam_username"),
                "steam_id": status.get("steam_id"),
                "leaderboard": args.leaderboard,
                "leaderboard_id": status.get("leaderboard_id"),
                "score": args.score,
                "style_id": args.style_id,
                "details": details,
                "details_b64": pack_details_b64(details),
                "layout": RANKMATCH_LAYOUT_NOTE,
                "backup_path": str(args.backup),
                "backup_path_exists": args.backup.exists(),
            }
            if args.dry_run:
                print(json.dumps({"dry_run": True, "proof": proof, "backup_preview": backup_payload}, indent=2))
                return 0

            backup_written = write_backup(args.backup, backup_payload, overwrite=args.overwrite_backup)
            upload = steam.upload_score(lb, args.score, details, force=True)
            after, _ = steam.read_current_entry(args.leaderboard)
            print(json.dumps({"proof": proof, "backup_written": backup_written, "steam_result": upload, "after": after}, indent=2))
            return 0

        if args.action == "restore":
            if args.backup.exists():
                backup = read_backup(args.backup)
                entry = backup.get("entry")
                if entry is None:
                    details = [args.region_id, args.language_id, args.neutral_style_id, args.rank_id]
                    score = 0
                    restore_note = "Backup had no previous entry; writing neutral zero row."
                else:
                    details = [int(v) for v in entry.get("details", [])]
                    score = int(entry.get("score", 0))
                    restore_note = "Restoring saved score/details from backup."
            elif args.restore_without_backup:
                details = [args.region_id, args.language_id, args.neutral_style_id, args.rank_id]
                score = 0
                restore_note = "No backup found; writing neutral zero row."
            else:
                raise SystemExit(
                    f"No backup exists at {args.backup}. Steam has no client delete-entry API; "
                    "pass --restore-without-backup to force a neutral score=0 row."
                )

            status, lb = steam.read_current_entry(args.leaderboard)
            restore = {
                "action": "restore",
                "account_source": ".env",
                "steam_username": status.get("steam_username"),
                "steam_id": status.get("steam_id"),
                "leaderboard": args.leaderboard,
                "leaderboard_id": status.get("leaderboard_id"),
                "score": score,
                "details": details,
                "details_b64": pack_details_b64(details),
                "backup_path": str(args.backup),
                "note": restore_note,
            }
            if args.dry_run:
                print(json.dumps({"dry_run": True, "restore": restore}, indent=2))
                return 0

            upload = steam.upload_score(lb, score, details, force=True)
            after, _ = steam.read_current_entry(args.leaderboard)
            print(json.dumps({"restore": restore, "steam_result": upload, "after": after}, indent=2))
            return 0

    raise AssertionError(f"Unhandled action {args.action}")


if __name__ == "__main__":
    raise SystemExit(main())
