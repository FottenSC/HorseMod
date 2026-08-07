"""Executable-bound native math imported by the SC6 Lux battle code.

SoulcaliburVI imports ``sinf`` and ``cosf`` through
``api-ms-win-crt-math-l1-1-0.dll``.  Calling Python's binary64 ``math``
functions is not instruction-faithful, so the offline reference model loads
the exact 64-bit UCRT implementation and binds its file hash into evidence.
"""

from __future__ import annotations

import ctypes
from dataclasses import dataclass
import hashlib
from pathlib import Path

from lux_numeric import float32


WINDOWS_UCRTBASE_PATH = Path(r"C:\Windows\System32\ucrtbase.dll")
APPROVED_UCRTBASE_SHA256 = (
    "5e7709a6b71bb818260b6f05c5bb3b6ca0c3ca9bc2f58c6242c1cd9d826d0079"
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


@dataclass(frozen=True)
class WindowsUcrtMath:
    path: Path
    sha256: str
    _sinf: object
    _cosf: object

    @classmethod
    def load_verified(
        cls,
        path: Path = WINDOWS_UCRTBASE_PATH,
        expected_sha256: str = APPROVED_UCRTBASE_SHA256,
    ) -> "WindowsUcrtMath":
        resolved = path.resolve(strict=True)
        actual_sha256 = sha256_file(resolved)
        if actual_sha256 != expected_sha256:
            raise RuntimeError(
                "SC6 imported-math provider hash changed: "
                f"expected {expected_sha256}, got {actual_sha256} at {resolved}"
            )
        library = ctypes.WinDLL(str(resolved))
        sinf = library.sinf
        sinf.argtypes = [ctypes.c_float]
        sinf.restype = ctypes.c_float
        cosf = library.cosf
        cosf.argtypes = [ctypes.c_float]
        cosf.restype = ctypes.c_float
        return cls(resolved, actual_sha256, sinf, cosf)

    def sinf(self, value: float) -> float:
        return float32(self._sinf(ctypes.c_float(float32(value))))

    def cosf(self, value: float) -> float:
        return float32(self._cosf(ctypes.c_float(float32(value))))

    def evidence(self) -> dict[str, str]:
        return {
            "api_contract": "api-ms-win-crt-math-l1-1-0.dll",
            "implementation_path": str(self.path),
            "implementation_sha256": self.sha256,
        }
