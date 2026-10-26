#!/usr/bin/env python3
"""Install the A733 boot-start service and its test certificate offline."""

import argparse
import shutil
import struct
import sys
from pathlib import Path

import hivex


REG_SZ = 1
REG_EXPAND_SZ = 2
REG_BINARY = 3
REG_DWORD = 4
REG_MULTI_SZ = 7
SDHOST_CLASS_GUID = "{a0a588a4-c46f-4b37-b7ea-c82fe89870c6}"
SDHOST_CLASS_KEY = "{a0a588a4-c46f-4b37-b7ea-c82fe89870c6}\\0000"


def get_or_add_key(hive, parent, name):
    child = hive.node_get_child(parent, name)
    return child if child else hive.node_add_child(parent, name)


def get_or_add_path(hive, path):
    node = hive.root()
    for name in path.split("\\"):
        node = get_or_add_key(hive, node, name)
    return node


def set_value(hive, node, name, value_type, data):
    hive.node_set_value(node, {"key": name, "t": value_type, "value": data})


def utf16(value):
    return (value + "\0").encode("utf-16le")


def utf16_multi(values):
    return ("\0".join(values) + "\0\0").encode("utf-16le")


def dword(value):
    return struct.pack("<I", value)


def current_control_set(hive):
    select = hive.node_get_child(hive.root(), "Select")
    if not select:
        raise RuntimeError("SYSTEM hive has no Select key")
    current = hive.node_get_value(select, "Current")
    if not current:
        raise RuntimeError("SYSTEM hive has no Select\\Current value")
    return f"ControlSet{hive.value_dword(current):03d}"


def apply_system(system_hive, driver_source, driver_destination):
    hive = hivex.Hivex(str(system_hive), write=True, unsafe=True)
    try:
        control_set = current_control_set(hive)
        service = get_or_add_path(hive, f"{control_set}\\Services\\a733smhc")
        set_value(hive, service, "Type", REG_DWORD, dword(1))
        set_value(hive, service, "Start", REG_DWORD, dword(0))
        set_value(hive, service, "ErrorControl", REG_DWORD, dword(1))
        set_value(hive, service, "ImagePath", REG_EXPAND_SZ, utf16(r"\SystemRoot\System32\drivers\a733smhc.sys"))
        set_value(hive, service, "Group", REG_SZ, utf16("System Bus Extender"))
        set_value(hive, service, "BootFlags", REG_DWORD, dword(8))

        # ACPI exposes AWI0001 as the host hardware ID and PNP0D40 as its
        # standard SD-host compatible ID. Register both before the boot volume
        # is needed so early PnP can bind the host on either match path.
        for hardware_id in ("ACPI#AWI0001", "ACPI#PNP0D40"):
            critical_device = get_or_add_path(
                hive,
                f"{control_set}\\Control\\CriticalDeviceDatabase\\{hardware_id}",
            )
            set_value(hive, critical_device, "Service", REG_SZ, utf16("a733smhc"))
            set_value(
                hive,
                critical_device,
                "ClassGUID",
                REG_SZ,
                utf16(SDHOST_CLASS_GUID),
            )

        # DISM application creates the Windows directory but does not retain
        # the WinPE PnP installation for the firmware-described host. Seed the
        # persisted ACPI devnode and its SDHost class link so boot-time PnP can
        # start the parent before resolving the system volume.
        class_node = get_or_add_path(
            hive,
            f"{control_set}\\Control\\Class\\{SDHOST_CLASS_KEY}",
        )
        set_value(hive, class_node, "Class", REG_SZ, utf16("SDHost"))
        set_value(hive, class_node, "ClassGUID", REG_SZ, utf16(SDHOST_CLASS_GUID))
        set_value(hive, class_node, "Service", REG_SZ, utf16("a733smhc"))

        devnode = get_or_add_path(
            hive,
            f"{control_set}\\Enum\\ACPI\\AWI0001\\0",
        )
        set_value(hive, devnode, "Class", REG_SZ, utf16("SDHost"))
        set_value(hive, devnode, "ClassGUID", REG_SZ, utf16(SDHOST_CLASS_GUID))
        set_value(hive, devnode, "DeviceDesc", REG_SZ,
                  utf16("Allwinner A733 SMHC0 SD Host Controller"))
        set_value(hive, devnode, "Driver", REG_SZ, utf16(SDHOST_CLASS_KEY))
        set_value(hive, devnode, "Mfg", REG_SZ, utf16("A733 bring-up"))
        set_value(hive, devnode, "Service", REG_SZ, utf16("a733smhc"))
        set_value(hive, devnode, "ConfigFlags", REG_DWORD, dword(0))
        set_value(
            hive,
            devnode,
            "HardwareID",
            REG_MULTI_SZ,
            utf16_multi(("ACPI\\VEN_AWI&DEV_0001", "ACPI\\AWI0001", "*AWI0001")),
        )
        set_value(
            hive,
            devnode,
            "CompatibleIDs",
            REG_MULTI_SZ,
            utf16_multi(("ACPI\\PNP0D40", "PNP0D40")),
        )

        # The inbox SD storage class driver normally relies on BootFlags=8 to
        # be promoted for an SD boot. Force its existing service to boot-start
        # for this self-hosted image so the SD child is eligible during the
        # early PnP pass even when Winload cannot classify the firmware path.
        sdstor = get_or_add_path(hive, f"{control_set}\\Services\\sdstor")
        set_value(hive, sdstor, "Start", REG_DWORD, dword(0))

        parameters = get_or_add_key(hive, service, "Parameters")
        set_value(
            hive,
            parameters,
            "SdCmdFlags",
            REG_BINARY,
            bytes.fromhex("05 01 06 01 08 11 09 19 0A 19 0D 11 10 01 11 01 12 01 17 01 18 05 19 05 1A 01 1B 01 1C 01 20 05 21 05 26 05 2A 01 34 02 35 02 37 01 38 01 22 01 23 05 24 01 25 01"),
        )
        set_value(
            hive,
            parameters,
            "SdAppCmdFlags",
            REG_BINARY,
            bytes.fromhex("06 01 0D 01 16 01 17 01 33 01 12 01 19 01 1A 01 26 01 2B 01 2C 01 2D 01 2E 01 2F 01 30 01 31 01"),
        )
        hive.commit(str(system_hive))
    finally:
        del hive

    destination = Path(driver_destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(driver_source, destination)
    if driver_source.read_bytes() != destination.read_bytes():
        raise RuntimeError("boot driver copy verification failed")


def add_certificate_store_entry(hive, store, thumbprint, blob):
    key = get_or_add_path(
        hive,
        f"Microsoft\\SystemCertificates\\{store}\\Certificates\\{thumbprint}",
    )
    set_value(hive, key, "Blob", REG_BINARY, blob)


def apply_software(software_hive, thumbprint, root_blob, publisher_blob):
    hive = hivex.Hivex(str(software_hive), write=True, unsafe=True)
    try:
        add_certificate_store_entry(hive, "ROOT", thumbprint, root_blob)
        add_certificate_store_entry(hive, "TrustedPublisher", thumbprint, publisher_blob)
        hive.commit(str(software_hive))
    finally:
        del hive


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--system-hive", type=Path, required=True)
    parser.add_argument("--software-hive", type=Path, required=True)
    parser.add_argument("--driver-source", type=Path, required=True)
    parser.add_argument("--driver-destination", type=Path, required=True)
    parser.add_argument("--certificate-thumbprint", required=True)
    parser.add_argument("--root-blob", type=Path, required=True)
    parser.add_argument("--publisher-blob", type=Path, required=True)
    return parser.parse_args()


def main():
    args = parse_args()
    for path in (args.system_hive, args.software_hive, args.driver_source, args.root_blob, args.publisher_blob):
        if not path.is_file():
            raise RuntimeError(f"missing input: {path}")
    thumbprint = args.certificate_thumbprint.upper()
    if len(thumbprint) != 40 or any(char not in "0123456789ABCDEF" for char in thumbprint):
        raise RuntimeError("certificate thumbprint must be a SHA-1 hex value")
    apply_system(args.system_hive, args.driver_source, args.driver_destination)
    apply_software(args.software_hive, thumbprint, args.root_blob.read_bytes(), args.publisher_blob.read_bytes())
    print(f"prepared offline A733 boot service with certificate {thumbprint}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        raise
