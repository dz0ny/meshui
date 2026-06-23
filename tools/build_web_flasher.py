#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["esptool"]
# ///
"""Generate the unified MeshCore PaperUI web flasher page.

Run with `uv run tools/build_web_flasher.py ...`. The HTML lives in
tools/web_flasher_assets/*.html as string.Template files ($placeholders) —
no inline page strings, no third-party templating engine.
"""

from __future__ import annotations

import argparse
import html
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path
from string import Template


BOOTLOADER_OFFSET = "0x0"
PARTITIONS_OFFSET = "0x8000"
BOOT_APP0_OFFSET = "0xe000"
APP_OFFSET = "0x10000"
FLASH_MODE = "dio"
FLASH_FREQ = "80m"
FLASH_SIZE = "16MB"
CHIP = "esp32s3"
CHIP_FAMILY = "ESP32-S3"

TARGETS = [
    {
        "env": "wio-tracker-l1",
        "slug": "wio-tracker-l1",
        "device_name": "Seeed Wio Tracker L1",
        "tab_label": "Wio Tracker L1",
        "flash_method": "web-serial-dfu",
        "chip_label": "nRF52840",
        "description": "The Wio Tracker L1 is an nRF52840 mono e-ink tracker. Double-tap RESET to enter the bootloader, then flash it straight from Chrome or Edge over Web Serial — no toolchain or drag-and-drop required. A UF2 download is provided as a fallback.",
        "product_url": None,
        "product_image": None,
        # Rendered by the native UI simulator (see tools/gen_wio_shots.sh). 1-bit
        # e-ink renders, so the gallery shows them pixel-crisp (no smoothing).
        "pixelated": True,
        # Native 250x122 mono panel — show at ~2x, pixel-doubled, not column-wide.
        "screen_max_w": "31rem",
        # Embed the interactive WASM build of the mono UI (tools/sim/build_web.sh)
        # as a live "Live preview" section in this panel.
        "preview": "wio",
        "screenshots": [
            ("wio-chat.png", "Messages", "Read and reply to mesh messages on the mono e-ink display."),
            ("wio-team.png", "Team", "Roster of nearby team members with their last-heard status."),
            ("wio-trail.png", "Trail", "Record your GPS breadcrumb track with live distance, time, and speed."),
            ("wio-waypoints.png", "Waypoints", "Saved locations and shared points of interest."),
            ("wio-gps.png", "GPS status", "Fix quality, satellite count, coordinates, and altitude."),
            ("wio-compass.png", "Compass", "Live heading with bearing and distance to a selected waypoint."),
            ("wio-settings.png", "Settings", "Radio, display, and device options."),
            ("wio-keyboard.png", "On-screen keyboard", "Compose text on-device with the button-driven keyboard."),
        ],
    },
    {
        "env": "t5-epaper",
        "slug": "lilygo-t5-epaper-pro",
        "device_name": "LilyGo T5 ePaper S3 Pro",
        "tab_label": "T5 ePaper",
        "flash_method": "esp-web-tools",
        "chip_label": CHIP_FAMILY,
        "description": "Flash the latest PlatformIO build for the LilyGo T5 ePaper S3 Pro directly from Chrome or Edge with ESP Web Tools. It works as both a standalone mesh device and a companion-connected MeshCore node.",
        "product_url": "https://lilygo.cc/en-us/products/t5-e-paper-s3-pro",
        "product_image": None,
        # Portrait 540x960 e-paper renders — cap the stage so they show roughly
        # device-sized instead of stretching to the full column width.
        "screen_max_w": "17rem",
        # Rendered by the native LVGL simulator (tools/gen_t5_shots.sh) — the real
        # on-device screens on mock data, not device photos.
        "preview": "t5",
        "screenshots": [
            ("t5-home.png", "Home", "Big clock, GPS + battery, unread count, and the main menu."),
            ("t5-chat.png", "Messages", "Read and reply to a conversation with the on-screen keyboard."),
            ("t5-contacts.png", "Contacts", "Saved mesh contacts — favorites first; tap to open a chat."),
            ("t5-compose.png", "Compose", "Write a new message to a contact or channel."),
            ("t5-gps.png", "GPS", "Fix quality, satellite count, coordinates, altitude, and speed."),
            ("t5-battery.png", "Battery", "Charge, voltage, current, and charger status."),
            ("t5-mesh.png", "Mesh", "Radio config and live link stats (peers, RSSI, SNR)."),
            ("t5-trail.png", "Trail", "GPS breadcrumb track with live distance, time, and pace."),
            ("t5-compass.png", "Compass", "Bearing and distance to a selected waypoint or contact."),
            ("t5-waypoints.png", "Waypoints", "Saved locations and shared points of interest."),
        ],
    },
]

PROJECT_NAME = "MeshCore PaperUI Flasher"

# Firmware highlights shown once at the top of the page. Each entry is
# (title, description, note) — note flags a device-specific feature, "" if it
# applies to every build.
FEATURES = [
    (
        "Share waypoints",
        "Drop a pin and send it over the mesh as a meter-precise location (way:/geo:). "
        "Tap a received point to navigate to it or save it to your waypoints.",
        "",
    ),
    (
        "Fast GPS updates",
        "Position refreshes quickly for responsive distance, bearing, compass, "
        "and live trail tracking.",
        "",
    ),
    (
        "Single-channel display",
        "Messages focus on one mesh channel at a time for a clean, readable view on e-ink.",
        "",
    ),
    (
        "Clone device settings",
        "Copy one device's channels, contacts, and preferences onto another over BLE — "
        "Share on the source, Receive on the target. Your identity stays put.",
        "Wio Tracker L1",
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, help="Base .pio/build directory")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repo-url", required=True)
    parser.add_argument("--boot-app0")
    return parser.parse_args()


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def find_boot_app0(explicit_path: str | None) -> Path:
    if explicit_path:
        path = Path(explicit_path)
        if path.is_file():
            return path
        raise FileNotFoundError(path)

    platformio_core = Path.home() / ".platformio"
    path = platformio_core / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"
    if path.is_file():
        return path
    raise FileNotFoundError(path)


# HTML templates live next to the page assets; substitution uses string.Template
# ($placeholders), so literal { } in the CSS/JS stay untouched.
TEMPLATE_DIR = Path(__file__).resolve().parent / "web_flasher_assets"


def load_template(name: str) -> Template:
    return Template((TEMPLATE_DIR / name).read_text(encoding="utf-8"))


EYEBROWS = {
    "web-serial-dfu": "Web Serial Flasher",
    "uf2": "UF2 Drag & Drop",
    "esp-web-tools": "Browser Flasher",
}


def build_target_config(target: dict, version: str) -> dict:
    """Serialize one target into the JSON the Preact app (app.mjs) renders.

    The app builds all DOM from this config, so every per-device value the page
    needs — labels, asset paths, flash-action attributes — is emitted here.
    """
    slug = target["slug"]
    method = target.get("flash_method", "esp-web-tools")

    if method == "web-serial-dfu":
        action = {
            "bin": f"{slug}/{slug}-{version}.bin",
            "dat": f"{slug}/{slug}-{version}.dat",
            "uf2": f"{slug}/{slug}-{version}.uf2",
        }
    elif method == "uf2":
        action = {"uf2": f"{slug}/{slug}-{version}.uf2"}
    else:
        action = {"manifest": f"{slug}/manifest.json"}

    return {
        "slug": slug,
        "tabLabel": target.get("tab_label", target["device_name"]),
        "deviceName": target["device_name"],
        "description": target["description"],
        "chipLabel": target.get("chip_label", CHIP_FAMILY),
        "eyebrow": EYEBROWS.get(method, EYEBROWS["esp-web-tools"]),
        "method": method,
        "productUrl": target.get("product_url"),
        "productImage": (
            f"{slug}/{target['product_image']}" if target.get("product_image") else None
        ),
        "screenMaxW": target.get("screen_max_w", "20rem"),
        "pixelated": bool(target.get("pixelated")),
        "preview": target.get("preview"),
        "action": action,
        "screenshots": [
            {"src": f"{slug}/{name}", "name": scr_name, "desc": desc}
            for (name, scr_name, desc) in target.get("screenshots", [])
        ],
    }


def build_page(version: str, repo_url: str) -> str:
    config = {
        "projectName": PROJECT_NAME,
        "repoUrl": repo_url,
        "version": version,
        "features": [
            {"title": title, "desc": desc, "note": note}
            for (title, desc, note) in FEATURES
        ],
        "targets": [build_target_config(t, version) for t in TARGETS],
    }
    # Embed as JSON in a <script>; </ is escaped so the literal can't close the tag.
    config_json = json.dumps(config, ensure_ascii=False).replace("</", "<\\/")
    return load_template("flasher.html").substitute(
        project_name=html.escape(PROJECT_NAME),
        config=config_json,
    )


def main() -> None:
    args = parse_args()
    build_base = Path(args.build_dir)
    output_dir = Path(args.output_dir)
    boot_app0 = find_boot_app0(args.boot_app0)
    assets_dir = Path(__file__).resolve().parent.parent / "assets"

    output_dir.mkdir(parents=True, exist_ok=True)

    for target in TARGETS:
        env = target["env"]
        slug = target["slug"]
        method = target.get("flash_method", "esp-web-tools")
        build_dir = build_base / env

        target_dir = output_dir / slug
        target_dir.mkdir(parents=True, exist_ok=True)

        if method == "uf2":
            # nRF52840: no ESP Web Tools / esptool. Publish the UF2 for drag-and-drop.
            uf2 = build_dir / "firmware.uf2"
            if not uf2.is_file():
                raise FileNotFoundError(uf2)
            shutil.copy2(uf2, target_dir / f"{slug}-{args.version}.uf2")
        elif method == "web-serial-dfu":
            # nRF52840: flash in-browser over Web Serial (legacy Nordic SLIP DFU,
            # the protocol adafruit-nrfutil speaks). The browser flasher
            # (nrf52-dfu.mjs) needs the application image + init packet, which the
            # PlatformIO-produced firmware.zip (the adafruit-nrfutil DFU package)
            # already contains. Extract them next to the page. Ship the UF2 too as
            # a drag-and-drop fallback.
            dfu_zip = build_dir / "firmware.zip"
            uf2 = build_dir / "firmware.uf2"
            for path in (dfu_zip, uf2):
                if not path.is_file():
                    raise FileNotFoundError(path)
            with zipfile.ZipFile(dfu_zip) as zf:
                with zf.open("firmware.bin") as src, open(
                    target_dir / f"{slug}-{args.version}.bin", "wb"
                ) as dst:
                    shutil.copyfileobj(src, dst)
                with zf.open("firmware.dat") as src, open(
                    target_dir / f"{slug}-{args.version}.dat", "wb"
                ) as dst:
                    shutil.copyfileobj(src, dst)
            shutil.copy2(uf2, target_dir / f"{slug}-{args.version}.uf2")
        else:
            bootloader = build_dir / "bootloader.bin"
            partitions = build_dir / "partitions.bin"
            firmware = build_dir / "firmware.bin"

            for path in (bootloader, partitions, firmware, boot_app0):
                if not path.is_file():
                    raise FileNotFoundError(path)

            merged_firmware = target_dir / "merged-firmware.bin"

            run(
                [
                    sys.executable,
                    "-m",
                    "esptool",
                    "--chip",
                    CHIP,
                    "merge-bin",
                    "-o",
                    str(merged_firmware),
                    "--flash-mode",
                    FLASH_MODE,
                    "--flash-freq",
                    FLASH_FREQ,
                    "--flash-size",
                    FLASH_SIZE,
                    BOOTLOADER_OFFSET,
                    str(bootloader),
                    PARTITIONS_OFFSET,
                    str(partitions),
                    BOOT_APP0_OFFSET,
                    str(boot_app0),
                    APP_OFFSET,
                    str(firmware),
                ]
            )

            manifest = f"""{{\n  "name": "{target['device_name']}",\n  "version": "{args.version}",\n  "new_install_prompt_erase": true,\n  "builds": [\n    {{\n      "chipFamily": "{CHIP_FAMILY}",\n      "parts": [\n        {{\n          "path": "merged-firmware.bin",\n          "offset": 0\n        }}\n      ]\n    }}\n  ]\n}}"""

            (target_dir / "manifest.json").write_text(manifest, encoding="utf-8")
            shutil.copy2(merged_firmware, target_dir / f"{slug}-{args.version}.bin")

        # Copy images if they exist
        if target.get("product_image"):
            img_path = assets_dir / target["product_image"]
            if img_path.is_file():
                shutil.copy2(img_path, target_dir / target["product_image"])

        for shot in target.get("screenshots", []):
            name = shot[0]
            img_path = assets_dir / name
            if img_path.is_file():
                shutil.copy2(img_path, target_dir / name)

    # Build + ship each interactive WASM preview a target asks for. "wio" is the
    # mono UI (tools/sim/build_web.sh -> sim.js); "t5" is the LVGL UI
    # (tools/sim/t5/build_t5_web.sh -> sim_t5.js). Compiled with Emscripten; the
    # build scripts error clearly if em++ is absent.
    tools = Path(__file__).resolve().parent
    PREVIEW_BUILDS = {
        "wio": (tools / "sim" / "build_web.sh", tools / "sim" / "web", ("sim.js", "sim.wasm")),
        "t5":  (tools / "sim" / "t5" / "build_t5_web.sh", tools / "sim" / "t5" / "web", ("sim_t5.js", "sim_t5.wasm")),
    }
    for kind in {t.get("preview") for t in TARGETS if t.get("preview")}:
        script, web_dir, artifacts = PREVIEW_BUILDS[kind]
        if not all((web_dir / a).is_file() for a in artifacts):
            run(["bash", str(script)])
        for a in artifacts:
            path = web_dir / a
            if not path.is_file():
                raise FileNotFoundError(path)
            shutil.copy2(path, output_dir / a)

    # Ship the Web Serial nRF52 DFU flasher module alongside the page.
    if any(t.get("flash_method") == "web-serial-dfu" for t in TARGETS):
        dfu_js = Path(__file__).resolve().parent / "web_flasher_assets" / "nrf52-dfu.mjs"
        if not dfu_js.is_file():
            raise FileNotFoundError(dfu_js)
        shutil.copy2(dfu_js, output_dir / "nrf52-dfu.mjs")

    # Ship the Preact front-end module alongside the page.
    app_js = TEMPLATE_DIR / "app.mjs"
    if not app_js.is_file():
        raise FileNotFoundError(app_js)
    shutil.copy2(app_js, output_dir / "app.mjs")

    # Write the unified index page
    (output_dir / "index.html").write_text(build_page(args.version, args.repo_url), encoding="utf-8")


if __name__ == "__main__":
    main()
