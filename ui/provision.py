"""Put the plugged-in Meshtastic node on the private swarm channel (default name: Lora4).

    python provision.py                          # auto-detect the node on USB
    python provision.py --port /dev/cu.usbmodem1101
    python provision.py --show-url               # also print the channel URL/QR for phones (contains the key)

The channel key lives in .swarm_channel.json next to this script (git-ignored). The first run creates it with a
random 256-bit key. Every node and every teammate's laptop must use the same file, so share it privately
(never commit it). Run this once on each node after flashing it.
"""

import argparse
import base64
import hashlib
import json
import secrets
import sys
import time
from pathlib import Path

CHANNEL_FILE = Path(__file__).resolve().parent / ".swarm_channel.json"


def load_or_create_channel(name):
    if CHANNEL_FILE.exists():
        channel = json.loads(CHANNEL_FILE.read_text())
        if len(base64.b64decode(channel["psk"])) != 32:
            sys.exit(f"{CHANNEL_FILE} does not hold a 256-bit key.")
        return channel
    channel = {"name": name, "psk": base64.b64encode(secrets.token_bytes(32)).decode()}
    CHANNEL_FILE.write_text(json.dumps(channel, indent=2) + "\n")
    CHANNEL_FILE.chmod(0o600)
    print(f"Created a new private key in {CHANNEL_FILE}. Share this file privately with your team.")
    return channel


def fingerprint(psk):
    return hashlib.sha256(psk).hexdigest()[:8]


def connect(port):
    from meshtastic.serial_interface import SerialInterface

    return SerialInterface(devPath=port)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port of the node (default: auto-detect)")
    parser.add_argument("--name", default="Lora4", help="channel name used when creating a new key file")
    parser.add_argument("--show-url", action="store_true", help="print the channel URL (includes the key)")
    args = parser.parse_args()

    channel = load_or_create_channel(args.name)
    psk = base64.b64decode(channel["psk"])

    from meshtastic.protobuf import channel_pb2, config_pb2

    interface = connect(args.port)
    try:
        node = interface.localNode
        user = (interface.getMyNodeInfo() or {}).get("user", {})
        print(f"Node: {user.get('longName', '?')} ({user.get('id', '?')}, {user.get('hwModel', '?')})")

        primary = node.getChannelByChannelIndex(0)
        primary.role = channel_pb2.Channel.Role.PRIMARY
        primary.settings.name = channel["name"]
        primary.settings.psk = psk
        primary.settings.uplink_enabled = False
        primary.settings.downlink_enabled = False
        # Full-precision positions on our private channel so the dashboard's lake heat map is accurate.
        primary.settings.module_settings.position_precision = 32
        node.writeChannel(0)
        print(f"Primary channel set to '{channel['name']}' (key fingerprint {fingerprint(psk)}).")
        if args.show_url:
            print("Channel URL:", node.getURL())

        lora = node.localConfig.lora
        if lora.region == config_pb2.Config.LoRaConfig.RegionCode.UNSET:
            lora.region = config_pb2.Config.LoRaConfig.RegionCode.US
            node.writeConfig("lora")
            print("LoRa region was unset; set to US (the node reboots).")
    finally:
        interface.close()

    time.sleep(8)
    interface = connect(args.port)
    try:
        primary = interface.localNode.getChannelByChannelIndex(0)
        ok = primary.settings.name == channel["name"] and bytes(primary.settings.psk) == psk
        region = config_pb2.Config.LoRaConfig.RegionCode.Name(interface.localNode.localConfig.lora.region)
        print(f"Verified: channel '{primary.settings.name}', key {'matches' if ok else 'DOES NOT match'}, region {region}.")
        if not ok:
            sys.exit(1)
    finally:
        interface.close()


if __name__ == "__main__":
    main()
