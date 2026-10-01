#!/bin/sh

set -eu

serial=${1:-}
device_name=${2:-dustcapino}

case "$serial" in
    ''|*[!A-Za-z0-9._-]*)
        echo "Usage: $0 FTDI_SERIAL [device-name]" >&2
        echo "Example: $0 BG02AEVB dustcapino" >&2
        exit 2
        ;;
esac

case "$device_name" in
    ''|*[!A-Za-z0-9._-]*)
        echo "Invalid device name: $device_name" >&2
        exit 2
        ;;
esac

rule_path="/etc/udev/rules.d/99-${device_name}.rules"
temporary_rule=$(mktemp)
trap 'rm -f "$temporary_rule"' EXIT HUP INT TERM

printf '%s\n' \
    "# DustCapIno FTDI adapter, serial ${serial}" \
    "SUBSYSTEM==\"tty\", ATTRS{idVendor}==\"0403\", ATTRS{idProduct}==\"6001\", ATTRS{serial}==\"${serial}\", SYMLINK+=\"${device_name}\", GROUP=\"dialout\", MODE=\"0660\", TAG+=\"uaccess\"" \
    >"$temporary_rule"

as_root()
{
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

as_root install -m 0644 "$temporary_rule" "$rule_path"
as_root udevadm control --reload-rules
as_root udevadm trigger --action=add --subsystem-match=tty
as_root udevadm settle

echo "Installed $rule_path"

if [ -L "/dev/${device_name}" ]; then
    echo "/dev/${device_name} -> $(readlink -f "/dev/${device_name}")"
else
    echo "The rule is loaded. Reconnect the FTDI adapter, then check /dev/${device_name}."
fi
