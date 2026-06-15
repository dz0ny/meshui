#!/usr/bin/env python3

from __future__ import annotations

import argparse
import html
import shutil
import subprocess
from pathlib import Path


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
        "flash_method": "esp-web-tools",
        "chip_label": CHIP_FAMILY,
        "description": "Flash the latest PlatformIO build for the LilyGo T5 ePaper S3 Pro directly from Chrome or Edge with ESP Web Tools. It works as both a standalone mesh device and a companion-connected MeshCore node.",
        "product_url": "https://lilygo.cc/en-us/products/t5-e-paper-s3-pro",
        "product_image": "main.jpeg",
        "screenshots": [
            ("compose.jpeg", "Compose screen"),
            ("contact.jpeg", "Contacts screen"),
            ("map.jpeg", "Map screen"),
            ("sensors.jpeg", "Sensors screen"),
        ],
    },
    {
        "env": "tdeck",
        "slug": "tdeck",
        "device_name": "LilyGo T-Deck",
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
        "flash_method": "uf2",
        "chip_label": "nRF52840",
        "description": "The Wio Tracker L1 is an nRF52840 mono e-ink tracker, so it flashes by drag-and-drop UF2 rather than Web Serial. Download the firmware below and copy it onto the bootloader drive — no toolchain required.",
        "product_url": None,
        "product_image": None,
        "screenshots": [],
    },
]

PROJECT_NAME = "MeshCore t-paper"


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


def build_target_card(target: dict, version: str) -> str:
    safe_version = html.escape(version)
    device_name = html.escape(target["device_name"])
    description = html.escape(target["description"])
    product_url = html.escape(target.get("product_url") or "", quote=True)
    chip_label = html.escape(target.get("chip_label", CHIP_FAMILY))
    method = target.get("flash_method", "esp-web-tools")
    slug = target["slug"]

    product_image_html = ""
    if target.get("product_image"):
        img = html.escape(target["product_image"])
        product_image_html = f"""
          <div class="border border-paper-line bg-[#fcfcfa] p-4">
            <img
              src="{slug}/{img}"
              alt="{device_name} home screen"
              class="mx-auto w-full max-w-[22rem] border border-paper-line object-cover"
            />
          </div>"""

    screenshots_html = ""
    if target.get("screenshots"):
        imgs = "\n".join(
            f'              <img src="{slug}/{html.escape(name)}" alt="{html.escape(alt)}" class="w-full border border-paper-line object-cover" />'
            for name, alt in target["screenshots"]
        )
        screenshots_html = f"""
          <div class="grid gap-3 border border-paper-line bg-[#fcfcfa] p-4">
            <p class="text-sm font-semibold uppercase tracking-[0.08em] text-paper-muted">Screens</p>
            <div class="grid grid-cols-2 gap-3 max-sm:grid-cols-1">
{imgs}
            </div>
          </div>"""

    product_link = ""
    if product_url:
        product_link = f'<a class="text-paper-accent underline decoration-1 underline-offset-2" href="{product_url}">Product Page</a>'

    eyebrow = "UF2 Drag &amp; Drop" if method == "uf2" else "Browser Flasher"

    if method == "uf2":
        uf2_name = f"{slug}-{version}.uf2"
        href = html.escape(f"{slug}/{uf2_name}", quote=True)
        action_html = f"""
          <div class="flex flex-wrap items-center gap-4">
            <a class="inline-block border border-paper-line bg-paper-accent px-5 py-3 text-sm font-semibold text-paper-panel" href="{href}" download>Download UF2 firmware</a>
            {product_link}
          </div>
          <div class="border border-paper-line bg-[#fcfcfa] p-4 text-sm leading-6 text-paper-muted">
            <p class="font-semibold text-paper-text">Install (drag &amp; drop)</p>
            <ol class="mt-2 list-decimal space-y-1 pl-5">
              <li>Connect the tracker to your computer over USB.</li>
              <li>Double-tap the <strong>RESET</strong> button to enter the bootloader — a <code>TRACKER L1</code> drive appears.</li>
              <li>Copy the downloaded <code>.uf2</code> onto that drive. The board reboots into the new firmware automatically.</li>
            </ol>
          </div>"""
    else:
        manifest = html.escape(f"{slug}/manifest.json", quote=True)
        action_html = f"""
          <div class="flex flex-wrap items-center gap-4">
            <esp-web-install-button manifest="{manifest}"></esp-web-install-button>
            {product_link}
          </div>"""

    return f"""
      <section class="overflow-hidden border border-paper-line bg-paper-panel">
        <div class="border-b border-paper-line p-7 max-sm:p-5">
          <p class="mb-3 text-xs font-semibold uppercase tracking-[0.08em] text-paper-muted">{eyebrow}</p>
          <h1 class="text-[clamp(1.5rem,5vw,2.4rem)] font-bold leading-none">{device_name}</h1>
          <p class="mt-4 max-w-[34rem] text-base leading-6 text-paper-muted">{description}</p>
        </div>
        <div class="grid gap-5 p-7 max-sm:p-5">
          <div class="flex flex-wrap gap-2">
            <span class="border border-paper-line bg-[#fcfcfa] px-3 py-2 text-sm">Device: {device_name}</span>
            <span class="border border-paper-line bg-[#fcfcfa] px-3 py-2 text-sm">Chip: {chip_label}</span>
            <span class="border border-paper-line bg-[#fcfcfa] px-3 py-2 text-sm">Build: {safe_version}</span>
          </div>{action_html}{product_image_html}{screenshots_html}
        </div>
      </section>"""


def build_page(version: str, repo_url: str) -> str:
    safe_repo_url = html.escape(repo_url, quote=True)
    cards = "\n".join(build_target_card(t, version) for t in TARGETS)
    return f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>{PROJECT_NAME} Web Flasher</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Montserrat:wght@400;500;600;700&display=swap" rel="stylesheet">
    <script src="https://cdn.tailwindcss.com"></script>
    <script>
      tailwind.config = {{
        theme: {{
          extend: {{
            fontFamily: {{
              sans: ["Montserrat", "sans-serif"]
            }},
            colors: {{
              paper: {{
                bg: "#efefec",
                panel: "#f8f8f5",
                line: "#d7d7d1",
                text: "#20201d",
                muted: "#6c6c67",
                accent: "#2b2b28"
              }}
            }}
          }}
        }}
      }};
    </script>
    <script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
    <style>
      :root {{
        --esp-tools-button-color: #2b2b28;
        --esp-tools-button-text-color: #f8f8f5;
        --esp-tools-button-border-radius: 0;
      }}

      body {{
        color-scheme: light;
        font-family: "Montserrat", sans-serif;
      }}

      esp-web-install-button::part(button) {{
        font-family: "Montserrat", sans-serif;
        font-weight: 600;
        letter-spacing: 0.01em;
      }}
    </style>
  </head>
  <body class="min-h-screen bg-gradient-to-b from-[#f4f4f1] to-paper-bg text-paper-text">
    <main class="mx-auto w-[min(46rem,calc(100vw-2rem))] py-10 max-sm:pt-4 max-sm:pb-8 space-y-6">
      <div class="px-7 max-sm:px-5">
        <h1 class="text-[clamp(2rem,6vw,3.2rem)] font-bold leading-none">{PROJECT_NAME}</h1>
        <p class="mt-2 text-paper-muted">Select a device to flash. <a class="text-paper-accent underline decoration-1 underline-offset-2" href="{safe_repo_url}">Repository</a></p>
      </div>
{cards}
      <div class="px-7 max-sm:px-5">
        <p class="text-sm leading-6 text-paper-muted">Use a USB data cable. ESP32 boards (T5 ePaper, T-Deck) flash in-browser over Web Serial (Chrome/Edge); the nRF52 Wio Tracker L1 installs by copying its UF2 onto the bootloader drive.</p>
        <ol class="list-decimal space-y-1 pl-5 text-sm leading-6 text-paper-muted mt-2">
          <li>Put the board in bootloader mode if the browser cannot detect it.</li>
          <li>Choose erase when you want a clean install (Web Serial only).</li>
        </ol>
      </div>
    </main>
  </body>
</html>
"""


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

        for name, _ in target.get("screenshots", []):
            img_path = assets_dir / name
            if img_path.is_file():
                shutil.copy2(img_path, target_dir / name)

    # Write the unified index page
    (output_dir / "index.html").write_text(build_page(args.version, args.repo_url), encoding="utf-8")


if __name__ == "__main__":
    main()
