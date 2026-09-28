"""Standalone Steam API diagnostic, run in the same host/box as the game.

This is not an admission gate: a game launched by Steam can receive working
client context while this standalone process fails initialization. Unknown
connection/account values remain null; failed initialization is not logout.

Uses the game's existing SteamUser019 interface, as HorseMod's observer does.
SteamAPI_Init failure is deliberately not labelled a login failure:
https://partner.steamgames.com/doc/api/steam_api#SteamAPI_Init
BLoggedOn reports the current server connection, not cached account presence:
https://partner.steamgames.com/doc/api/ISteamUser#BLoggedOn
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import time


def probe(dll: Path) -> dict:
    result = {"version": 1, "pid": os.getpid(), "observed_unix": time.time(),
              "api_initialized": False, "connected": None, "steam_id": None}
    dll = dll.resolve(strict=True)
    result["api_sha256"] = hashlib.sha256(dll.read_bytes()).hexdigest()
    # Process-local development AppID; never write the game's steam_appid.txt
    # or alter Steam's account/registry state. No RestartAppIfNecessary call.
    os.environ["SteamAppId"] = "544750"
    os.environ["SteamGameId"] = "544750"
    api = ctypes.CDLL(str(dll))

    def bind(name, restype, *arguments):
        function = getattr(api, name)
        function.restype = restype
        function.argtypes = list(arguments)
        return function

    initialize = bind("SteamAPI_Init", ctypes.c_bool)
    shutdown = bind("SteamAPI_Shutdown", None)
    result["client_running"] = bool(bind("SteamAPI_IsSteamRunning", ctypes.c_bool)())
    result["api_initialized"] = bool(initialize())
    if not result["api_initialized"]:
        result["reason"] = "steam_api_initialization_failed"
        return result
    try:
        client = bind("SteamClient", ctypes.c_void_p)()
        user_handle = bind("SteamAPI_GetHSteamUser", ctypes.c_int)()
        pipe = bind("SteamAPI_GetHSteamPipe", ctypes.c_int)()
        if not client or not user_handle or not pipe:
            result["reason"] = "steam_client_handles_unavailable"
            return result
        user = bind("SteamAPI_ISteamClient_GetISteamUser", ctypes.c_void_p,
                    ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p)(
                        client, user_handle, pipe, b"SteamUser019")
        if not user:
            result["reason"] = "steam_user_interface_unavailable"
            return result
        result["steam_id"] = int(bind("SteamAPI_ISteamUser_GetSteamID", ctypes.c_uint64,
                                      ctypes.c_void_p)(user))
        result["connected"] = bool(bind("SteamAPI_ISteamUser_BLoggedOn", ctypes.c_bool,
                                        ctypes.c_void_p)(user))
        result["reason"] = "connected" if result["connected"] else "steam_server_disconnected"
        return result
    finally:
        shutdown()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--run-id", required=True)
    arguments = parser.parse_args()
    try:
        result = probe(arguments.dll)
    except (OSError, AttributeError, ValueError) as error:
        result = {"version": 1, "api_initialized": False, "connected": None,
                  "steam_id": None, "reason": type(error).__name__, "detail": str(error)}
    print("HORSE_STEAM_READINESS=" + json.dumps({**result, "run_id": arguments.run_id}), flush=True)
    return 0 if result["connected"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
