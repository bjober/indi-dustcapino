# Persistent DustCapIno device name

Linux names USB serial adapters `/dev/ttyUSB0`, `/dev/ttyUSB1`, and so on in
detection order. Those names can change after a reboot or when another adapter
is reconnected. DustCapIno should instead use a persistent name tied to the
unique serial number stored in its FTDI adapter.

The repository provides an installer that creates `/dev/dustcapino`. It matches
all three identifying attributes of an FT232R adapter:

* FTDI vendor ID `0403`
* FT232R product ID `6001`
* The adapter's unique serial number

Matching the serial number is important. Matching only `0403:6001` would be
ambiguous if a second FTDI adapter were connected.

## 1. Identify the adapter

Connect DustCapIno and find its current `ttyUSB` device under
`/dev/serial/by-id`. Then inspect the udev identity, substituting the current
device name if it is not `ttyUSB2`:

```sh
ls -l /dev/serial/by-id/
udevadm info --query=property --name=/dev/ttyUSB2 \
  | grep -E '^(ID_VENDOR_ID|ID_MODEL_ID|ID_SERIAL_SHORT)='
```

The verified adapter on the original DustCapIno installation reports:

```text
ID_VENDOR_ID=0403
ID_MODEL_ID=6001
ID_SERIAL_SHORT=BG02AEVB
```

## 2. Install the rule

Run the installer with the reported `ID_SERIAL_SHORT` value:

```sh
./scripts/install-udev-rule.sh BG02AEVB dustcapino
```

The script installs `/etc/udev/rules.d/99-dustcapino.rules`, reloads udev, and
requests the new link. It uses `sudo` only for the system-level installation
and udev reload. The resulting rule is equivalent to the template in
`udev/99-dustcapino.rules.example`.

If the link does not appear immediately, disconnect and reconnect only the
DustCapIno USB adapter. Verify the result:

```sh
readlink -f /dev/dustcapino
udevadm info --query=property --name=/dev/dustcapino \
  | grep -E '^(ID_SERIAL_SHORT|DEVLINKS)='
```

## 3. Configure KStars / Ekos

Set the DustCapIno INDI properties to:

```text
Device port: /dev/dustcapino
Baud rate:   115200
```

Save the device configuration. Avoid `AUTO` on systems with other serial
astronomy equipment because automatic probing can open unrelated ports.

KStars Flatpak can use the custom link when it has the same device access that
allows the underlying `/dev/ttyUSB*` adapter. The DustCapIno driver stores the
configured device name and uses only that name on subsequent connections.

If the FTDI adapter is replaced, its serial number will also change. Run the
installer again with the new `ID_SERIAL_SHORT` value before reconnecting in
KStars.
