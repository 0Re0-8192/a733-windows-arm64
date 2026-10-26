"""Safely replace only the two fixed A733 raw boot regions on an owned SD.

The default mode is read-only. Applying requires an enrolled manifest, exact
media/GPT/current-region identity, locked volumes, durable backups, explicit
--apply, and unbuffered readback verification.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wintypes
import hashlib
import json
import os
import struct
import subprocess
import sys
import time
import uuid
import zlib
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_SHARE_READ = 1
FILE_SHARE_WRITE = 2
FILE_ATTRIBUTE_NORMAL = 0x00000080
FILE_FLAG_WRITE_THROUGH = 0x80000000
FILE_FLAG_NO_BUFFERING = 0x20000000
FILE_BEGIN = 0
FSCTL_LOCK_VOLUME = 0x00090018
FSCTL_UNLOCK_VOLUME = 0x0009001C
FSCTL_DISMOUNT_VOLUME = 0x00090020
IOCTL_STORAGE_GET_DEVICE_NUMBER = 0x002D1080
IOCTL_STORAGE_CHECK_VERIFY = 0x002D4800
IOCTL_STORAGE_EJECTION_CONTROL = 0x002D4808
IOCTL_STORAGE_LOAD_MEDIA = 0x002D480C
MOVEFILE_REPLACE_EXISTING = 0x00000001
MOVEFILE_WRITE_THROUGH = 0x00000008
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
CHUNK_SIZE = 1024 * 1024
SAFE_BACKUP_ROOT = Path(os.environ.get("A733_WORKDIR", ".")) / "work" / "backup" / "a733_boot_regions"
A733_BOOT_REGIONS = {
    "boot0": (0x020000, 0x03C000),
    "sunxi-package": (0xC00000, 0x160000),
}


if os.name != "nt":
    raise SystemExit("This tool only supports Windows physical disks.")


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateFileW.argtypes = [
    wintypes.LPCWSTR,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.LPVOID,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.HANDLE,
]
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.SetFilePointerEx.argtypes = [
    wintypes.HANDLE,
    ctypes.c_longlong,
    ctypes.POINTER(ctypes.c_longlong),
    wintypes.DWORD,
]
kernel32.SetFilePointerEx.restype = wintypes.BOOL
kernel32.ReadFile.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    wintypes.LPVOID,
]
kernel32.ReadFile.restype = wintypes.BOOL
kernel32.WriteFile.argtypes = kernel32.ReadFile.argtypes
kernel32.WriteFile.restype = wintypes.BOOL
kernel32.FlushFileBuffers.argtypes = [wintypes.HANDLE]
kernel32.FlushFileBuffers.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL
kernel32.DeviceIoControl.argtypes = [
    wintypes.HANDLE,
    wintypes.DWORD,
    wintypes.LPVOID,
    wintypes.DWORD,
    wintypes.LPVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    wintypes.LPVOID,
]
kernel32.DeviceIoControl.restype = wintypes.BOOL
kernel32.VirtualAlloc.argtypes = [
    wintypes.LPVOID,
    ctypes.c_size_t,
    wintypes.DWORD,
    wintypes.DWORD,
]
kernel32.VirtualAlloc.restype = wintypes.LPVOID
kernel32.VirtualFree.argtypes = [wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD]
kernel32.VirtualFree.restype = wintypes.BOOL
kernel32.MoveFileExW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD]
kernel32.MoveFileExW.restype = wintypes.BOOL


INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value


class SafetyError(RuntimeError):
    pass


class StorageDeviceNumber(ctypes.Structure):
    _fields_ = [
        ("device_type", wintypes.DWORD),
        ("device_number", wintypes.DWORD),
        ("partition_number", wintypes.DWORD),
    ]


class PreventMediaRemoval(ctypes.Structure):
    _fields_ = [("prevent_media_removal", wintypes.BYTE)]


@dataclass(frozen=True)
class GptPartition:
    number: int
    type_guid: str
    unique_guid: str
    first_lba: int
    last_lba: int
    attributes: int
    name: str

    @property
    def lba_count(self) -> int:
        return self.last_lba - self.first_lba + 1


@dataclass(frozen=True)
class GptIdentity:
    disk_guid: str
    fingerprint_sha256: str
    logical_sector_size: int
    disk_last_lba: int
    first_usable_lba: int
    last_usable_lba: int
    primary_entries_lba: int
    backup_entries_lba: int
    entry_count: int
    entry_size: int
    partitions: tuple[GptPartition, ...]
    protected_ranges: tuple[tuple[int, int, str], ...]


def sha256_bytes(data: bytes | bytearray | memoryview) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(CHUNK_SIZE):
            digest.update(chunk)
    return digest.hexdigest().upper()


def normalize_text(value: Any) -> str:
    return "" if value is None else str(value).strip()


def normalize_guid(value: Any) -> str:
    text = normalize_text(value).strip("{}").lower()
    if not text:
        return ""
    try:
        return str(uuid.UUID(text))
    except ValueError:
        return text


def needs_enrollment(value: Any) -> bool:
    return normalize_text(value).upper().startswith("ENROLL_")


def run_powershell_json(script: str) -> Any:
    prefix = (
        "$ErrorActionPreference='Stop'; "
        "[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false); "
    )
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-Command", prefix + script],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise SafetyError(f"PowerShell query failed: {detail}")
    output = result.stdout.strip()
    return None if not output else json.loads(output)


def query_target(disk_number: int) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    disk = run_powershell_json(
        f"Get-Disk -Number {disk_number} | ForEach-Object {{ "
        "[pscustomobject]@{Number=$_.Number;FriendlyName=$_.FriendlyName;"
        "SerialNumber=$_.SerialNumber;UniqueId=$_.UniqueId;BusType=([string]$_.BusType);"
        "PartitionStyle=([string]$_.PartitionStyle);Size=$_.Size;"
        "OperationalStatus=@($_.OperationalStatus | ForEach-Object {[string]$_});"
        "IsBoot=$_.IsBoot;IsSystem=$_.IsSystem;IsReadOnly=$_.IsReadOnly;"
        "IsOffline=$_.IsOffline;LogicalSectorSize=$_.LogicalSectorSize;"
        "PhysicalSectorSize=$_.PhysicalSectorSize;Guid=([string]$_.Guid)} } | "
        "ConvertTo-Json -Compress"
    )
    if not isinstance(disk, dict):
        raise SafetyError(f"Disk {disk_number} was not found.")

    partitions = run_powershell_json(
        f"@(Get-Partition -DiskNumber {disk_number} | ForEach-Object {{ "
        "[pscustomobject]@{PartitionNumber=$_.PartitionNumber;Offset=$_.Offset;"
        "Size=$_.Size;GptType=([string]$_.GptType);Guid=([string]$_.Guid);"
        "IsBoot=$_.IsBoot;IsHidden=$_.IsHidden} }) | ConvertTo-Json -Compress"
    )
    if partitions is None:
        partition_list: list[dict[str, Any]] = []
    elif isinstance(partitions, dict):
        partition_list = [partitions]
    else:
        partition_list = list(partitions)
    return disk, partition_list


def query_volume_ids(disk_number: int) -> list[str]:
    values = run_powershell_json(
        f"@(Get-Partition -DiskNumber {disk_number} | Get-Volume | "
        "Where-Object UniqueId | Select-Object -ExpandProperty UniqueId -Unique) | "
        "ConvertTo-Json -Compress"
    )
    if values is None:
        return []
    if isinstance(values, str):
        return [values.rstrip("\\")]
    return [str(value).rstrip("\\") for value in values]


def parse_manifest(path: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="ascii"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise SafetyError(f"Cannot read manifest {path}: {exc}") from exc
    for key in ("image", "image_sha256", "target", "regions"):
        if key not in manifest:
            raise SafetyError(f"Manifest is missing {key!r}.")
    if not isinstance(manifest["regions"], list) or not manifest["regions"]:
        raise SafetyError("Manifest regions must be a non-empty list.")
    return manifest


def validate_source(manifest: dict[str, Any]) -> tuple[Path, list[dict[str, Any]]]:
    image = Path(manifest["image"]).resolve(strict=True)
    expected_image_hash = normalize_text(manifest["image_sha256"]).upper()
    actual_image_hash = sha256_file(image)
    if actual_image_hash != expected_image_hash:
        raise SafetyError(
            f"Image SHA256 mismatch: expected {expected_image_hash}, got {actual_image_hash}."
        )

    manifest_names = {normalize_text(region.get("name")) for region in manifest["regions"]}
    if manifest_names != set(A733_BOOT_REGIONS) or len(manifest["regions"]) != len(
        A733_BOOT_REGIONS
    ):
        raise SafetyError("Manifest must contain exactly boot0 and sunxi-package.")

    image_size = image.stat().st_size
    regions: list[dict[str, Any]] = []
    with image.open("rb") as stream:
        for raw_region in manifest["regions"]:
            name = normalize_text(raw_region.get("name"))
            offset = int(raw_region.get("offset"))
            size = int(raw_region.get("size"))
            if (offset, size) != A733_BOOT_REGIONS.get(name):
                raise SafetyError(
                    f"Region {name!r} must use {A733_BOOT_REGIONS.get(name)!r}, "
                    f"got {(offset, size)!r}."
                )
            if offset + size > image_size:
                raise SafetyError(f"Image is too short for region {name}.")
            expected_hash = normalize_text(raw_region.get("sha256")).upper()
            current_hash = normalize_text(raw_region.get("current_sha256")).upper()
            if len(current_hash) != 64:
                raise SafetyError(f"Region {name} must pin current_sha256.")
            stream.seek(offset)
            data = stream.read(size)
            actual_hash = sha256_bytes(data)
            if actual_hash != expected_hash:
                raise SafetyError(
                    f"Region {name} source hash mismatch: expected {expected_hash}, "
                    f"got {actual_hash}."
                )
            regions.append(
                {
                    "name": name,
                    "offset": offset,
                    "size": size,
                    "sha256": actual_hash,
                    "current_sha256": current_hash,
                    "data": data,
                }
            )
    return image, regions


def open_handle(path: str, write: bool, unbuffered: bool = False) -> int:
    access = GENERIC_READ | (GENERIC_WRITE if write else 0)
    flags = FILE_ATTRIBUTE_NORMAL
    if write:
        flags |= FILE_FLAG_WRITE_THROUGH
    if unbuffered:
        flags |= FILE_FLAG_NO_BUFFERING
    handle = kernel32.CreateFileW(
        path,
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        None,
        OPEN_EXISTING,
        flags,
        None,
    )
    if handle == INVALID_HANDLE_VALUE:
        raise SafetyError(f"Cannot open {path}: Win32 error {ctypes.get_last_error()}.")
    return handle


def close_handle(handle: int | None) -> None:
    if handle not in (None, INVALID_HANDLE_VALUE):
        kernel32.CloseHandle(handle)


def device_io_control(handle: int, code: int, operation: str) -> None:
    returned = wintypes.DWORD(0)
    if not kernel32.DeviceIoControl(
        handle, code, None, 0, None, 0, ctypes.byref(returned), None
    ):
        raise SafetyError(f"{operation} failed: Win32 error {ctypes.get_last_error()}.")


def set_ejection_control(handle: int, prevent: bool) -> None:
    request = PreventMediaRemoval(1 if prevent else 0)
    returned = wintypes.DWORD(0)
    if not kernel32.DeviceIoControl(
        handle,
        IOCTL_STORAGE_EJECTION_CONTROL,
        ctypes.byref(request),
        ctypes.sizeof(request),
        None,
        0,
        ctypes.byref(returned),
        None,
    ):
        operation = "Prevent" if prevent else "Allow"
        raise SafetyError(
            f"{operation} physical media ejection failed: "
            f"Win32 error {ctypes.get_last_error()}."
        )


def load_media(handle: int) -> None:
    returned = wintypes.DWORD(0)
    if not kernel32.DeviceIoControl(
        handle,
        IOCTL_STORAGE_LOAD_MEDIA,
        None,
        0,
        None,
        0,
        ctypes.byref(returned),
        None,
    ):
        raise SafetyError(
            f"Load physical media failed: Win32 error {ctypes.get_last_error()}."
        )


def get_media_change_count(handle: int) -> int:
    output = wintypes.DWORD(0)
    returned = wintypes.DWORD(0)
    if not kernel32.DeviceIoControl(
        handle,
        IOCTL_STORAGE_CHECK_VERIFY,
        None,
        0,
        ctypes.byref(output),
        ctypes.sizeof(output),
        ctypes.byref(returned),
        None,
    ):
        raise SafetyError(
            "IOCTL_STORAGE_CHECK_VERIFY failed: "
            f"Win32 error {ctypes.get_last_error()}."
        )
    if returned.value != ctypes.sizeof(output):
        raise SafetyError("IOCTL_STORAGE_CHECK_VERIFY did not return a media-change count.")
    return int(output.value)


def get_storage_device_number(handle: int) -> StorageDeviceNumber:
    output = StorageDeviceNumber()
    returned = wintypes.DWORD(0)
    if not kernel32.DeviceIoControl(
        handle,
        IOCTL_STORAGE_GET_DEVICE_NUMBER,
        None,
        0,
        ctypes.byref(output),
        ctypes.sizeof(output),
        ctypes.byref(returned),
        None,
    ):
        raise SafetyError(
            f"IOCTL_STORAGE_GET_DEVICE_NUMBER failed: Win32 error {ctypes.get_last_error()}."
        )
    if returned.value < ctypes.sizeof(output):
        raise SafetyError("IOCTL_STORAGE_GET_DEVICE_NUMBER returned a short structure.")
    return output


def set_offset(handle: int, offset: int) -> None:
    new_position = ctypes.c_longlong(0)
    if not kernel32.SetFilePointerEx(handle, offset, ctypes.byref(new_position), FILE_BEGIN):
        raise SafetyError(
            f"SetFilePointerEx failed at 0x{offset:X}: Win32 error {ctypes.get_last_error()}."
        )


def _allocate_aligned(size: int) -> int:
    pointer = kernel32.VirtualAlloc(None, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    if not pointer:
        raise SafetyError(f"VirtualAlloc({size}) failed: Win32 error {ctypes.get_last_error()}.")
    return pointer


def _free_aligned(pointer: int) -> None:
    if not kernel32.VirtualFree(pointer, 0, MEM_RELEASE):
        raise SafetyError(f"VirtualFree failed: Win32 error {ctypes.get_last_error()}.")


def read_exact(handle: int, offset: int, size: int, alignment: int = 1) -> bytes:
    if offset % alignment or size % alignment:
        raise SafetyError(
            f"Unaligned raw read 0x{offset:X}+0x{size:X} for alignment {alignment}."
        )
    set_offset(handle, offset)
    data = bytearray()
    while len(data) < size:
        count = min(CHUNK_SIZE, size - len(data))
        if count % alignment:
            count -= count % alignment
        pointer = _allocate_aligned(count)
        try:
            read = wintypes.DWORD(0)
            if not kernel32.ReadFile(handle, pointer, count, ctypes.byref(read), None):
                raise SafetyError(
                    f"ReadFile failed at 0x{offset + len(data):X}: "
                    f"Win32 error {ctypes.get_last_error()}."
                )
            if read.value != count:
                raise SafetyError(
                    f"Short read at 0x{offset + len(data):X}: {read.value} of {count}."
                )
            data.extend(ctypes.string_at(pointer, read.value))
        finally:
            _free_aligned(pointer)
    return bytes(data)


def write_exact(handle: int, offset: int, data: bytes, alignment: int) -> None:
    if offset % alignment or len(data) % alignment:
        raise SafetyError(
            f"Unaligned raw write 0x{offset:X}+0x{len(data):X} for alignment {alignment}."
        )
    set_offset(handle, offset)
    written_total = 0
    while written_total < len(data):
        count = min(CHUNK_SIZE, len(data) - written_total)
        if count % alignment:
            count -= count % alignment
        chunk = data[written_total : written_total + count]
        pointer = _allocate_aligned(count)
        try:
            ctypes.memmove(pointer, chunk, count)
            written = wintypes.DWORD(0)
            if not kernel32.WriteFile(handle, pointer, count, ctypes.byref(written), None):
                raise SafetyError(
                    f"WriteFile failed at 0x{offset + written_total:X}: "
                    f"Win32 error {ctypes.get_last_error()}."
                )
            if written.value != count:
                raise SafetyError(
                    f"Short write at 0x{offset + written_total:X}: "
                    f"{written.value} of {count}."
                )
            written_total += count
        finally:
            _free_aligned(pointer)
    if not kernel32.FlushFileBuffers(handle):
        raise SafetyError(f"FlushFileBuffers failed: Win32 error {ctypes.get_last_error()}.")


def _parse_gpt_header(sector: bytes, sector_size: int, label: str) -> dict[str, Any]:
    if len(sector) != sector_size or sector[:8] != b"EFI PART":
        raise SafetyError(f"{label} GPT header signature/size is invalid.")
    revision, header_size, header_crc, reserved = struct.unpack_from("<IIII", sector, 8)
    if revision < 0x00010000 or header_size < 92 or header_size > sector_size or reserved != 0:
        raise SafetyError(f"{label} GPT header fields are invalid.")
    checked = bytearray(sector[:header_size])
    struct.pack_into("<I", checked, 16, 0)
    if zlib.crc32(checked) & 0xFFFFFFFF != header_crc:
        raise SafetyError(f"{label} GPT header CRC32 is invalid.")
    current_lba, backup_lba, first_usable, last_usable = struct.unpack_from(
        "<QQQQ", sector, 24
    )
    disk_guid = str(uuid.UUID(bytes_le=sector[56:72]))
    entries_lba, entry_count, entry_size, entries_crc = struct.unpack_from(
        "<QIII", sector, 72
    )
    if entry_count <= 0 or entry_count > 16384 or entry_size < 128 or entry_size % 8:
        raise SafetyError(f"{label} GPT entry geometry is unsafe.")
    return {
        "current_lba": current_lba,
        "backup_lba": backup_lba,
        "first_usable": first_usable,
        "last_usable": last_usable,
        "disk_guid": disk_guid,
        "entries_lba": entries_lba,
        "entry_count": entry_count,
        "entry_size": entry_size,
        "entries_crc": entries_crc,
    }


def _parse_gpt_partitions(entries: bytes, header: dict[str, Any]) -> tuple[GptPartition, ...]:
    partitions = []
    for index in range(header["entry_count"]):
        offset = index * header["entry_size"]
        entry = entries[offset : offset + header["entry_size"]]
        if entry[:16] == bytes(16):
            continue
        type_guid = str(uuid.UUID(bytes_le=entry[:16]))
        unique_guid = str(uuid.UUID(bytes_le=entry[16:32]))
        first_lba, last_lba, attributes = struct.unpack_from("<QQQ", entry, 32)
        if first_lba > last_lba:
            raise SafetyError(f"GPT partition {index + 1} has an inverted LBA range.")
        raw_name = entry[56 : min(len(entry), 128)]
        name = raw_name.decode("utf-16-le", errors="replace").split("\x00", 1)[0]
        partitions.append(
            GptPartition(
                number=index + 1,
                type_guid=type_guid,
                unique_guid=unique_guid,
                first_lba=first_lba,
                last_lba=last_lba,
                attributes=attributes,
                name=name,
            )
        )
    ordered = sorted(partitions, key=lambda item: item.first_lba)
    for left, right in zip(ordered, ordered[1:]):
        if left.last_lba >= right.first_lba:
            raise SafetyError(
                f"GPT partitions {left.number} and {right.number} overlap."
            )
    return tuple(partitions)


def read_gpt_identity(
    handle: int, disk_size: int, sector_size: int
) -> GptIdentity:
    if sector_size <= 0 or disk_size % sector_size:
        raise SafetyError("Disk size/logical sector size is invalid.")
    disk_last_lba = disk_size // sector_size - 1
    pmbr = read_exact(handle, 0, sector_size, sector_size)
    if pmbr[510:512] != b"\x55\xAA" or not any(
        pmbr[446 + index * 16 + 4] == 0xEE for index in range(4)
    ):
        raise SafetyError("Protective MBR is invalid or lacks a 0xEE entry.")

    primary_sector = read_exact(handle, sector_size, sector_size, sector_size)
    primary = _parse_gpt_header(primary_sector, sector_size, "primary")
    if primary["current_lba"] != 1 or primary["backup_lba"] != disk_last_lba:
        raise SafetyError("Primary GPT does not point to LBA1/last-LBA headers.")

    backup_sector = read_exact(
        handle, primary["backup_lba"] * sector_size, sector_size, sector_size
    )
    backup = _parse_gpt_header(backup_sector, sector_size, "backup")
    matching_fields = (
        "disk_guid",
        "first_usable",
        "last_usable",
        "entry_count",
        "entry_size",
        "entries_crc",
    )
    if backup["current_lba"] != disk_last_lba or backup["backup_lba"] != 1:
        raise SafetyError("Backup GPT does not point back to the primary header.")
    if any(primary[field] != backup[field] for field in matching_fields):
        raise SafetyError("Primary and backup GPT headers disagree.")

    entries_size = primary["entry_count"] * primary["entry_size"]
    entries_span = (entries_size + sector_size - 1) // sector_size * sector_size
    primary_entries = read_exact(
        handle, primary["entries_lba"] * sector_size, entries_span, sector_size
    )
    backup_entries = read_exact(
        handle, backup["entries_lba"] * sector_size, entries_span, sector_size
    )
    if zlib.crc32(primary_entries[:entries_size]) & 0xFFFFFFFF != primary["entries_crc"]:
        raise SafetyError("Primary GPT entry-array CRC32 is invalid.")
    if zlib.crc32(backup_entries[:entries_size]) & 0xFFFFFFFF != backup["entries_crc"]:
        raise SafetyError("Backup GPT entry-array CRC32 is invalid.")
    if primary_entries[:entries_size] != backup_entries[:entries_size]:
        raise SafetyError("Primary and backup GPT entry arrays differ.")

    partitions = _parse_gpt_partitions(primary_entries[:entries_size], primary)
    for partition in partitions:
        if (
            partition.first_lba < primary["first_usable"]
            or partition.last_lba > primary["last_usable"]
        ):
            raise SafetyError(f"GPT partition {partition.number} is outside usable LBAs.")

    protected_ranges = (
        (0, sector_size, "PMBR"),
        (sector_size, 2 * sector_size, "primary GPT header"),
        (
            primary["entries_lba"] * sector_size,
            primary["entries_lba"] * sector_size + entries_span,
            "primary GPT entries",
        ),
        (
            backup["entries_lba"] * sector_size,
            backup["entries_lba"] * sector_size + entries_span,
            "backup GPT entries",
        ),
        (
            backup["current_lba"] * sector_size,
            (backup["current_lba"] + 1) * sector_size,
            "backup GPT header",
        ),
    )
    fingerprint = sha256_bytes(
        pmbr
        + primary_sector
        + primary_entries
        + backup_entries
        + backup_sector
    )
    return GptIdentity(
        disk_guid=primary["disk_guid"],
        fingerprint_sha256=fingerprint,
        logical_sector_size=sector_size,
        disk_last_lba=disk_last_lba,
        first_usable_lba=primary["first_usable"],
        last_usable_lba=primary["last_usable"],
        primary_entries_lba=primary["entries_lba"],
        backup_entries_lba=backup["entries_lba"],
        entry_count=primary["entry_count"],
        entry_size=primary["entry_size"],
        partitions=partitions,
        protected_ranges=protected_ranges,
    )


def validate_target_metadata(
    expected: dict[str, Any],
    actual: dict[str, Any],
    powershell_partitions: list[dict[str, Any]],
    apply: bool,
) -> None:
    exact_fields = {
        "Number": int(expected["disk_number"]),
        "FriendlyName": normalize_text(expected["friendly_name"]),
        "SerialNumber": normalize_text(expected["serial_number"]),
        "BusType": normalize_text(expected["bus_type"]),
        "PartitionStyle": normalize_text(expected["partition_style"]),
        "Size": int(expected["size"]),
        "LogicalSectorSize": int(expected["logical_sector_size"]),
    }
    for field, wanted in exact_fields.items():
        got = actual.get(field)
        got = int(got) if field in {"Number", "Size", "LogicalSectorSize"} else normalize_text(got)
        if got != wanted:
            raise SafetyError(f"Target {field} mismatch: expected {wanted!r}, got {got!r}.")
    if bool(actual.get("IsBoot")) or bool(actual.get("IsSystem")):
        raise SafetyError("Refusing to use a host boot/system disk.")
    if bool(actual.get("IsOffline")):
        raise SafetyError("Target disk is offline.")
    operational = actual.get("OperationalStatus")
    values = operational if isinstance(operational, list) else [operational]
    if "online" not in {normalize_text(value).casefold() for value in values}:
        raise SafetyError(f"Target disk is not Online: {operational!r}.")
    if apply and bool(actual.get("IsReadOnly")):
        raise SafetyError("Target disk is read-only.")
    expected_partitions = expected.get("partitions")
    if not isinstance(expected_partitions, list) or not expected_partitions:
        raise SafetyError("Manifest target must pin partitions.")
    if len(powershell_partitions) != len(expected_partitions):
        raise SafetyError("PowerShell partition count does not match the manifest.")


def validate_gpt_and_layout(
    expected: dict[str, Any],
    actual_disk: dict[str, Any],
    powershell_partitions: list[dict[str, Any]],
    gpt: GptIdentity,
    regions: list[dict[str, Any]],
    apply: bool,
) -> bool:
    enrolled = True
    expected_disk_guid = expected.get("gpt_disk_guid")
    expected_fingerprint = expected.get("gpt_fingerprint_sha256")
    for value in (expected_disk_guid, expected_fingerprint):
        if needs_enrollment(value):
            enrolled = False
    if apply and not enrolled:
        raise SafetyError("Manifest GPT identity is not enrolled; --apply is forbidden.")
    if enrolled:
        if normalize_guid(expected_disk_guid) != normalize_guid(gpt.disk_guid):
            raise SafetyError("Raw GPT disk GUID does not match the manifest.")
        if normalize_text(expected_fingerprint).upper() != gpt.fingerprint_sha256:
            raise SafetyError("Raw GPT fingerprint does not match the manifest.")

    powershell_disk_guid = normalize_guid(actual_disk.get("Guid"))
    if powershell_disk_guid and powershell_disk_guid != normalize_guid(gpt.disk_guid):
        raise SafetyError("PowerShell and raw GPT disk GUIDs disagree.")

    expected_by_number = {int(item["number"]): item for item in expected["partitions"]}
    ps_by_number = {int(item["PartitionNumber"]): item for item in powershell_partitions}
    raw_by_number = {item.number: item for item in gpt.partitions}
    if set(expected_by_number) != set(ps_by_number) or set(expected_by_number) != set(raw_by_number):
        raise SafetyError("Manifest, PowerShell, and raw GPT partition numbers disagree.")

    for number, expected_partition in expected_by_number.items():
        raw = raw_by_number[number]
        ps = ps_by_number[number]
        expected_values = {
            "offset": int(expected_partition["offset"]),
            "size": int(expected_partition["size"]),
            "type_guid": normalize_guid(expected_partition["gpt_type"]),
        }
        raw_values = {
            "offset": raw.first_lba * gpt.logical_sector_size,
            "size": raw.lba_count * gpt.logical_sector_size,
            "type_guid": normalize_guid(raw.type_guid),
        }
        ps_values = {
            "offset": int(ps["Offset"]),
            "size": int(ps["Size"]),
            "type_guid": normalize_guid(ps["GptType"]),
        }
        if raw_values != expected_values or ps_values != expected_values:
            raise SafetyError(f"Partition {number} geometry/type mismatch.")
        expected_unique = expected_partition.get("unique_guid")
        if needs_enrollment(expected_unique):
            enrolled = False
            if apply:
                raise SafetyError(f"Partition {number} unique GUID is not enrolled.")
        elif normalize_guid(expected_unique) != normalize_guid(raw.unique_guid):
            raise SafetyError(f"Partition {number} unique GUID mismatch.")
        ps_guid = normalize_guid(ps.get("Guid"))
        if ps_guid and ps_guid != normalize_guid(raw.unique_guid):
            raise SafetyError(f"PowerShell/raw GPT partition {number} GUID mismatch.")

    occupied = list(gpt.protected_ranges)
    occupied.extend(
        (
            partition.first_lba * gpt.logical_sector_size,
            (partition.last_lba + 1) * gpt.logical_sector_size,
            f"partition {partition.number}",
        )
        for partition in gpt.partitions
    )
    for region in regions:
        start = region["offset"]
        end = start + region["size"]
        for occupied_start, occupied_end, label in occupied:
            if start < occupied_end and occupied_start < end:
                raise SafetyError(f"Region {region['name']} overlaps {label}.")
    return enrolled


def read_region_observations(
    handle: int, regions: list[dict[str, Any]], sector_size: int
) -> tuple[list[dict[str, Any]], dict[str, bytes]]:
    observations = []
    data_by_name = {}
    for region in regions:
        data = read_exact(handle, region["offset"], region["size"], sector_size)
        current_hash = sha256_bytes(data)
        if current_hash != region["current_sha256"]:
            raise SafetyError(
                f"Current {region['name']} fingerprint mismatch: expected "
                f"{region['current_sha256']}, got {current_hash}."
            )
        data_by_name[region["name"]] = data
        observations.append(
            {
                "name": region["name"],
                "offset": region["offset"],
                "size": region["size"],
                "current_sha256": current_hash,
                "candidate_sha256": region["sha256"],
                "matches_candidate": data == region["data"],
            }
        )
    return observations, data_by_name


def durable_write_bytes(path: Path, data: bytes) -> None:
    with path.open("xb", buffering=0) as stream:
        total = 0
        while total < len(data):
            written = stream.write(data[total:])
            if written is None or written <= 0:
                raise SafetyError(f"Backup write failed for {path}.")
            total += written
        stream.flush()
        os.fsync(stream.fileno())


def durable_write_json(path: Path, value: dict[str, Any]) -> None:
    data = (json.dumps(value, indent=2, ensure_ascii=True) + "\n").encode("ascii")
    expected_value = json.loads(data.decode("ascii"))
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        durable_write_bytes(temporary, data)
        persisted = temporary.read_bytes()
        if persisted != data or json.loads(persisted.decode("ascii")) != expected_value:
            raise SafetyError(f"Temporary JSON verification failed for {path}.")
        if not kernel32.MoveFileExW(
            str(temporary),
            str(path),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
        ):
            raise SafetyError(
                f"Atomic JSON replacement failed for {path}: "
                f"Win32 error {ctypes.get_last_error()}."
            )
        final_data = path.read_bytes()
        if final_data != data or json.loads(final_data.decode("ascii")) != expected_value:
            raise SafetyError(f"Durable JSON verification failed for {path}.")
    finally:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass


def gpt_public_dict(gpt: GptIdentity) -> dict[str, Any]:
    value = asdict(gpt)
    value["protected_ranges"] = [
        {"offset": start, "size": end - start, "name": name}
        for start, end, name in gpt.protected_ranges
    ]
    return value


def validate_open_physical_handle(handle: int, disk_number: int) -> None:
    device = get_storage_device_number(handle)
    if int(device.device_number) != disk_number:
        raise SafetyError(
            f"Opened physical handle maps to disk {device.device_number}, expected {disk_number}."
        )


def assert_locked_media_identity(
    handle: int,
    disk_size: int,
    sector_size: int,
    expected_change_count: int,
    expected_gpt_fingerprint: str,
    expected_region_data: dict[str, bytes],
    regions: list[dict[str, Any]],
) -> GptIdentity:
    current_change_count = get_media_change_count(handle)
    if current_change_count != expected_change_count:
        raise SafetyError(
            "Physical media change count changed during the protected write session: "
            f"expected {expected_change_count}, got {current_change_count}."
        )
    gpt = read_gpt_identity(handle, disk_size, sector_size)
    if gpt.fingerprint_sha256 != expected_gpt_fingerprint:
        raise SafetyError("GPT identity changed during the protected write session.")
    for region in regions:
        actual = read_exact(handle, region["offset"], region["size"], sector_size)
        if actual != expected_region_data[region["name"]]:
            raise SafetyError(
                f"Raw region {region['name']} changed during the protected write session."
            )
    return gpt


def apply_regions(
    disk_path: str,
    disk_number: int,
    image: Path,
    manifest: dict[str, Any],
    regions: list[dict[str, Any]],
) -> Path:
    if not ctypes.windll.shell32.IsUserAnAdmin():
        raise SafetyError("--apply requires an elevated terminal.")

    expected_target = manifest["target"]
    sector_size = int(expected_target["logical_sector_size"])
    volume_handles: list[int] = []
    anchor_handle: int | None = None
    write_handle: int | None = None
    verify_handle: int | None = None
    ejection_prevented = False
    result_path: Path | None = None
    result: dict[str, Any] = {"status": "not-started", "regions": []}

    try:
        actual_target, ps_partitions = query_target(disk_number)
        validate_target_metadata(expected_target, actual_target, ps_partitions, True)
        anchor_handle = open_handle(disk_path, write=False, unbuffered=True)
        validate_open_physical_handle(anchor_handle, disk_number)
        anchor_gpt = read_gpt_identity(
            anchor_handle, int(actual_target["Size"]), sector_size
        )
        validate_gpt_and_layout(
            expected_target,
            actual_target,
            ps_partitions,
            anchor_gpt,
            regions,
            True,
        )
        _, anchor_data = read_region_observations(
            anchor_handle, regions, sector_size
        )

        write_handle = open_handle(disk_path, write=True, unbuffered=True)
        validate_open_physical_handle(write_handle, disk_number)
        write_gpt = read_gpt_identity(
            write_handle, int(actual_target["Size"]), sector_size
        )
        if write_gpt.fingerprint_sha256 != anchor_gpt.fingerprint_sha256:
            raise SafetyError("GPT identity changed between anchor and write handles.")
        _, before_by_name = read_region_observations(
            write_handle, regions, sector_size
        )
        if any(before_by_name[name] != anchor_data[name] for name in before_by_name):
            raise SafetyError("Raw region identity changed before locking/writing.")
        close_handle(anchor_handle)
        anchor_handle = None
        set_ejection_control(write_handle, True)
        ejection_prevented = True
        load_media(write_handle)
        time.sleep(1.0)
        media_removal_mode = "ejection-control-prevented-and-loaded"
        actual_target, ps_partitions = query_target(disk_number)
        validate_target_metadata(expected_target, actual_target, ps_partitions, True)
        media_change_count = get_media_change_count(write_handle)
        locked_gpt = assert_locked_media_identity(
            write_handle,
            int(actual_target["Size"]),
            sector_size,
            media_change_count,
            write_gpt.fingerprint_sha256,
            before_by_name,
            regions,
        )

        expected_volume_ids = {
            normalize_text(value).rstrip("\\").casefold()
            for value in expected_target["volume_unique_ids"]
        }
        volume_ids = query_volume_ids(disk_number)
        actual_volume_ids = {value.casefold() for value in volume_ids}
        if actual_volume_ids != expected_volume_ids:
            raise SafetyError(
                f"Volume identity mismatch: expected {sorted(expected_volume_ids)}, "
                f"got {sorted(actual_volume_ids)}."
            )
        expected_partition_numbers = {
            int(item["number"]) for item in expected_target["partitions"]
        }
        locked_partition_numbers = set()
        for volume_id in volume_ids:
            handle = open_handle(volume_id, write=True)
            try:
                device = get_storage_device_number(handle)
                if int(device.device_number) != disk_number:
                    raise SafetyError(f"Volume {volume_id} maps to the wrong disk.")
                partition_number = int(device.partition_number)
                if partition_number not in expected_partition_numbers:
                    raise SafetyError(
                        f"Volume {volume_id} maps to unexpected partition {partition_number}."
                    )
                device_io_control(handle, FSCTL_LOCK_VOLUME, f"Lock {volume_id}")
                device_io_control(handle, FSCTL_DISMOUNT_VOLUME, f"Dismount {volume_id}")
            except Exception:
                close_handle(handle)
                raise
            volume_handles.append(handle)
            locked_partition_numbers.add(partition_number)
        if locked_partition_numbers != expected_partition_numbers:
            raise SafetyError("Not every expected partition volume was locked.")
        locked_gpt = assert_locked_media_identity(
            write_handle,
            int(actual_target["Size"]),
            sector_size,
            media_change_count,
            locked_gpt.fingerprint_sha256,
            before_by_name,
            regions,
        )

        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        backup_dir = SAFE_BACKUP_ROOT.resolve() / f"disk{disk_number}_{stamp}"
        backup_dir.mkdir(parents=True, exist_ok=False)
        result_path = backup_dir / "result.json"
        result = {
            "status": "backing-up",
            "timestamp": datetime.now().astimezone().isoformat(),
            "disk": actual_target,
            "gpt": gpt_public_dict(locked_gpt),
            "media_removal_mode": media_removal_mode,
            "media_change_count": media_change_count,
            "image": str(image),
            "image_sha256": manifest["image_sha256"],
            "regions": [],
        }
        for region in regions:
            before = before_by_name[region["name"]]
            backup_path = backup_dir / f"{region['name']}.before.bin"
            durable_write_bytes(backup_path, before)
            before_hash = sha256_bytes(before)
            if sha256_file(backup_path) != before_hash:
                raise SafetyError(f"Durable backup verification failed for {region['name']}.")
            result["regions"].append(
                {
                    "name": region["name"],
                    "offset": region["offset"],
                    "size": region["size"],
                    "backup": str(backup_path),
                    "before_sha256": before_hash,
                    "candidate_sha256": region["sha256"],
                    "action": "skipped-identical" if before == region["data"] else "write",
                    "write_started": False,
                    "readback_verified": False,
                }
            )
        result["status"] = "backups-verified-write-not-started"
        durable_write_json(result_path, result)

        result_by_name = {entry["name"]: entry for entry in result["regions"]}
        expected_region_data = dict(before_by_name)
        write_order = sorted(regions, key=lambda region: region["name"] == "boot0")
        for region in write_order:
            entry = result_by_name[region["name"]]
            if before_by_name[region["name"]] == region["data"]:
                entry["readback_verified"] = True
                entry["after_sha256"] = entry["before_sha256"]
                durable_write_json(result_path, result)
                continue
            assert_locked_media_identity(
                write_handle,
                int(actual_target["Size"]),
                sector_size,
                media_change_count,
                locked_gpt.fingerprint_sha256,
                expected_region_data,
                regions,
            )
            entry["write_started"] = True
            result["status"] = f"writing-{region['name']}"
            durable_write_json(result_path, result)
            assert_locked_media_identity(
                write_handle,
                int(actual_target["Size"]),
                sector_size,
                media_change_count,
                locked_gpt.fingerprint_sha256,
                expected_region_data,
                regions,
            )
            write_exact(
                write_handle,
                region["offset"],
                region["data"],
                sector_size,
            )
            expected_region_data[region["name"]] = region["data"]
            assert_locked_media_identity(
                write_handle,
                int(actual_target["Size"]),
                sector_size,
                media_change_count,
                locked_gpt.fingerprint_sha256,
                expected_region_data,
                regions,
            )
            result["status"] = f"written-{region['name']}-awaiting-readback"
            durable_write_json(result_path, result)

        assert_locked_media_identity(
            write_handle,
            int(actual_target["Size"]),
            sector_size,
            media_change_count,
            locked_gpt.fingerprint_sha256,
            expected_region_data,
            regions,
        )
        verify_handle = open_handle(disk_path, write=False, unbuffered=True)
        validate_open_physical_handle(verify_handle, disk_number)
        verify_gpt = read_gpt_identity(
            verify_handle, int(actual_target["Size"]), sector_size
        )
        if verify_gpt.fingerprint_sha256 != anchor_gpt.fingerprint_sha256:
            raise SafetyError("GPT changed during the raw-region write.")
        for region in regions:
            after = read_exact(
                verify_handle, region["offset"], region["size"], sector_size
            )
            entry = result_by_name[region["name"]]
            entry["after_sha256"] = sha256_bytes(after)
            if after != region["data"]:
                result["status"] = f"readback-failed-{region['name']}"
                durable_write_json(result_path, result)
                raise SafetyError(f"Unbuffered readback mismatch for {region['name']}.")
            entry["readback_verified"] = True
            durable_write_json(result_path, result)
        result["status"] = "verified"
        durable_write_json(result_path, result)
        return backup_dir
    except Exception as exc:
        result["status"] = "failed"
        result["error"] = str(exc)
        if result_path is not None:
            try:
                durable_write_json(result_path, result)
            except Exception:
                pass
        raise
    finally:
        close_handle(verify_handle)
        for handle in reversed(volume_handles):
            try:
                device_io_control(handle, FSCTL_UNLOCK_VOLUME, "Unlock volume")
            except SafetyError:
                pass
            close_handle(handle)
        if write_handle is not None and ejection_prevented:
            try:
                set_ejection_control(write_handle, False)
                load_media(write_handle)
                time.sleep(1.0)
            except SafetyError:
                pass
        close_handle(write_handle)
        close_handle(anchor_handle)


def inspect(
    disk_path: str,
    disk_number: int,
    manifest: dict[str, Any],
    regions: list[dict[str, Any]],
    apply: bool,
) -> tuple[dict[str, Any], list[dict[str, Any]], GptIdentity, list[dict[str, Any]], bool]:
    expected_target = manifest["target"]
    actual_target, ps_partitions = query_target(disk_number)
    validate_target_metadata(expected_target, actual_target, ps_partitions, apply)
    sector_size = int(expected_target["logical_sector_size"])
    handle = open_handle(disk_path, write=False, unbuffered=True)
    try:
        validate_open_physical_handle(handle, disk_number)
        gpt = read_gpt_identity(handle, int(actual_target["Size"]), sector_size)
        enrolled = validate_gpt_and_layout(
            expected_target,
            actual_target,
            ps_partitions,
            gpt,
            regions,
            apply,
        )
        observations, _ = read_region_observations(handle, regions, sector_size)
    finally:
        close_handle(handle)
    expected_volume_ids = {
        normalize_text(value).rstrip("\\").casefold()
        for value in expected_target["volume_unique_ids"]
    }
    actual_volume_ids = {value.casefold() for value in query_volume_ids(disk_number)}
    if actual_volume_ids != expected_volume_ids:
        raise SafetyError("Volume UniqueId set does not match the manifest.")
    return actual_target, ps_partitions, gpt, observations, enrolled


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Write only after all checks/backups. Without this flag, inspect only.",
    )
    args = parser.parse_args()

    manifest = parse_manifest(args.manifest.resolve())
    image, regions = validate_source(manifest)
    expected_target = manifest["target"]
    disk_number = int(expected_target["disk_number"])
    disk_path = rf"\\.\PhysicalDrive{disk_number}"
    actual_target, _, gpt, observations, enrolled = inspect(
        disk_path, disk_number, manifest, regions, args.apply
    )

    print(f"Mode        : {'APPLY' if args.apply else 'READ-ONLY DRY RUN'}")
    print(f"Image       : {image}")
    print(f"Image SHA256: {manifest['image_sha256']}")
    print(
        f"Target      : {disk_path} / {actual_target.get('FriendlyName')} / "
        f"serial {normalize_text(actual_target.get('SerialNumber'))} / "
        f"{actual_target.get('Size')} bytes"
    )
    print(f"GPT GUID    : {gpt.disk_guid}")
    print(f"GPT SHA256  : {gpt.fingerprint_sha256}")
    for partition in gpt.partitions:
        print(
            f"GPT part {partition.number}: unique={partition.unique_guid} "
            f"type={partition.type_guid} first_lba=0x{partition.first_lba:X} "
            f"last_lba=0x{partition.last_lba:X}"
        )
    for item in observations:
        state = "identical" if item["matches_candidate"] else "will-change"
        print(
            f"[{item['name']}] offset=0x{item['offset']:08X} size=0x{item['size']:X} "
            f"current={item['current_sha256']} candidate={item['candidate_sha256']} {state}"
        )

    if not enrolled:
        print("Manifest GPT/partition identity requires read-only enrollment; --apply is forbidden.")
    if not args.apply:
        print("Dry run complete. No disk writes, locks, or dismounts were performed.")
        return 0
    if not enrolled:
        raise SafetyError("Refusing --apply with an unenrolled manifest.")

    backup_dir = apply_regions(
        disk_path, disk_number, image, manifest, regions
    )
    print(f"Apply complete. Durable backups and result: {backup_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (SafetyError, OSError, ValueError, KeyError, struct.error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
