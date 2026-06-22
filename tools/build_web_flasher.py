#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = []
# ///
"""Generate the unified MeshCore MeshUI web flasher page.

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
        "env": "t5-epaper",
        "slug": "lilygo-t5-epaper-pro",
        "device_name": "LilyGo T5 ePaper S3 Pro",
        "tab_label": "T5 ePaper",
        "flash_method": "esp-web-tools",
        "chip_label": CHIP_FAMILY,
        "description": "Flash the latest PlatformIO build for the LilyGo T5 ePaper S3 Pro directly from Chrome or Edge with ESP Web Tools. It works as both a standalone mesh device and a companion-connected MeshCore node.",
        "product_url": "https://lilygo.cc/en-us/products/t5-e-paper-s3-pro",
        "product_image": "main.jpeg",
        # Portrait 540x960 e-paper renders — cap the stage so they show roughly
        # device-sized instead of stretching to the full column width.
        "screen_max_w": "17rem",
        "screenshots": [
            ("compose.jpeg", "Compose", "Write and send a message to a contact or channel using the on-screen keyboard."),
            ("contact.jpeg", "Contacts", "Your saved mesh contacts — tap one to open its conversation."),
            ("map.jpeg", "Map", "Plots heard node positions and your own GPS fix on an offline tile map."),
            ("sensors.jpeg", "Sensors", "Live telemetry: battery, GPS lock, and environment readings."),
        ],
    },
    {
        "env": "tdeck",
        "slug": "tdeck",
        "device_name": "LilyGo T-Deck",
        "tab_label": "T-Deck",
        "flash_method": "esp-web-tools",
        "chip_label": CHIP_FAMILY,
        "description": "Flash the latest PlatformIO build for the LilyGo T-Deck directly from Chrome or Edge with ESP Web Tools. It works as both a standalone mesh device and a companion-connected MeshCore node.",
        "product_url": "https://lilygo.cc/en-us/products/t-deck",
        "product_image": None,
        "screenshots": [],
    },
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
        "preview": True,
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
]

PROJECT_NAME = "MeshCore MeshUI Flasher"


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


# Tab trigger + gallery rows are the only repeated fragments built in Python.
TAB_TRIGGER = Template(
    '        <button role="tab" id="tab-$slug" data-tab="$slug" data-state="$state" '
    'aria-controls="$slug" class="inline-flex flex-1 items-center justify-center '
    "whitespace-nowrap rounded-sm px-3 py-1.5 text-sm font-medium ring-offset-background "
    "transition-all focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring "
    "data-[state=active]:bg-background data-[state=active]:text-foreground "
    'data-[state=active]:shadow-sm">$label</button>'
)
SLIDE_IMG = Template(
    '                  <img src="$src" alt="$alt"'
    ' class="slide rounded-md border object-contain$pix$active" />'
)
SCREEN_CHIP = Template(
    '                <button type="button" data-go="$i" data-state="$state"'
    ' class="rounded-full border px-3 py-1 text-xs font-medium transition-colors'
    " data-[state=active]:bg-primary data-[state=active]:text-primary-foreground"
    " data-[state=inactive]:bg-background data-[state=inactive]:text-muted-foreground"
    ' data-[state=inactive]:hover:text-foreground">$name</button>'
)
PRODUCT_LINK = Template(
    '<a class="text-sm font-medium text-primary underline-offset-4 hover:underline" '
    'href="$url">Product page</a>'
)


def build_target_panel(target: dict, version: str, active: bool) -> str:
    device_name = html.escape(target["device_name"])
    product_url = html.escape(target.get("product_url") or "", quote=True)
    method = target.get("flash_method", "esp-web-tools")
    slug = target["slug"]

    product_link = PRODUCT_LINK.substitute(url=product_url) if product_url else ""

    product_image_html = ""
    if target.get("product_image"):
        product_image_html = load_template("fragment_product_image.html").substitute(
            src=f"{slug}/{html.escape(target['product_image'])}",
            device_name=device_name,
        )

    screenshots_html = ""
    if target.get("screenshots"):
        shots = target["screenshots"]
        pix = " pixelated" if target.get("pixelated") else ""
        imgs = "\n".join(
            SLIDE_IMG.substitute(
                src=f"{slug}/{html.escape(name)}",
                alt=html.escape(scr_name),
                pix=pix,
                active=" is-active" if i == 0 else "",
            )
            for i, (name, scr_name, _desc) in enumerate(shots)
        )
        chips = "\n".join(
            SCREEN_CHIP.substitute(
                i=i,
                state="active" if i == 0 else "inactive",
                name=html.escape(scr_name),
            )
            for i, (_name, scr_name, _desc) in enumerate(shots)
        )
        captions = html.escape(
            json.dumps([{"name": n, "desc": d} for (_f, n, d) in shots]),
            quote=True,
        )
        screenshots_html = load_template("fragment_screens.html").substitute(
            captions=captions,
            max_w=target.get("screen_max_w", "20rem"),
            imgs=imgs,
            chips=chips,
            first_name=html.escape(shots[0][1]),
            first_desc=html.escape(shots[0][2]),
        )

    if method == "web-serial-dfu":
        eyebrow = "Web Serial Flasher"
        action_html = load_template("fragment_action_dfu.html").substitute(
            bin_attr=html.escape(f"{slug}/{slug}-{version}.bin", quote=True),
            dat_attr=html.escape(f"{slug}/{slug}-{version}.dat", quote=True),
            uf2_href=html.escape(f"{slug}/{slug}-{version}.uf2", quote=True),
            product_link=product_link,
        )
    elif method == "uf2":
        eyebrow = "UF2 Drag &amp; Drop"
        action_html = load_template("fragment_action_uf2.html").substitute(
            uf2_href=html.escape(f"{slug}/{slug}-{version}.uf2", quote=True),
            product_link=product_link,
        )
    else:
        eyebrow = "Browser Flasher"
        action_html = load_template("fragment_action_esp.html").substitute(
            manifest=html.escape(f"{slug}/manifest.json", quote=True),
            product_link=product_link,
        )

    # Interactive WASM preview (mono UI) — only where the build provides it.
    preview_html = load_template("fragment_preview.html").template if target.get("preview") else ""

    return load_template("flasher_panel.html").substitute(
        slug=slug,
        hidden="" if active else " hidden",
        eyebrow=eyebrow,
        device_name=device_name,
        description=html.escape(target["description"]),
        chip_label=html.escape(target.get("chip_label", CHIP_FAMILY)),
        version=html.escape(version),
        action_html=action_html,
        product_image_html=product_image_html,
        screenshots_html=screenshots_html,
        preview_html=preview_html,
    )


def build_tablist(targets: list[dict]) -> str:
    triggers = "\n".join(
        TAB_TRIGGER.substitute(
            slug=t["slug"],
            state="active" if i == 0 else "inactive",
            label=html.escape(t.get("tab_label", t["device_name"])),
        )
        for i, t in enumerate(targets)
    )
    return (
        '      <div role="tablist" class="inline-flex h-auto w-full flex-wrap items-center '
        'justify-center gap-1 rounded-md bg-muted p-1 text-muted-foreground">\n'
        f"{triggers}\n"
        "      </div>"
    )


def build_page(version: str, repo_url: str) -> str:
    return load_template("flasher.html").substitute(
        project_name=html.escape(PROJECT_NAME),
        repo_url=html.escape(repo_url, quote=True),
        tablist=build_tablist(TARGETS),
        panels="\n".join(
            build_target_panel(t, version, active=(i == 0))
            for i, t in enumerate(TARGETS)
        ),
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
            # (nrf52-dfu.js) needs the application image + init packet, which the
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
                    "python",
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

    # Build + ship the interactive WASM mono-UI preview if any target wants it.
    if any(t.get("preview") for t in TARGETS):
        sim_dir = Path(__file__).resolve().parent / "sim"
        build_web = sim_dir / "build_web.sh"
        sim_js = sim_dir / "web" / "sim.js"
        sim_wasm = sim_dir / "web" / "sim.wasm"
        if not (sim_js.is_file() and sim_wasm.is_file()):
            # Compile with Emscripten (build_web.sh errors clearly if em++ is absent).
            run(["bash", str(build_web)])
        for path in (sim_js, sim_wasm):
            if not path.is_file():
                raise FileNotFoundError(path)
            shutil.copy2(path, output_dir / path.name)

    # Ship the Web Serial nRF52 DFU flasher module alongside the page.
    if any(t.get("flash_method") == "web-serial-dfu" for t in TARGETS):
        dfu_js = Path(__file__).resolve().parent / "web_flasher_assets" / "nrf52-dfu.js"
        if not dfu_js.is_file():
            raise FileNotFoundError(dfu_js)
        shutil.copy2(dfu_js, output_dir / "nrf52-dfu.js")

    # Write the unified index page
    (output_dir / "index.html").write_text(build_page(args.version, args.repo_url), encoding="utf-8")


if __name__ == "__main__":
    main()
