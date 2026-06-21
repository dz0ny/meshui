#!/usr/bin/env python3

from __future__ import annotations

import argparse
import html
import shutil
import subprocess
import zipfile
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
        "flash_method": "web-serial-dfu",
        "chip_label": "nRF52840",
        "description": "The Wio Tracker L1 is an nRF52840 mono e-ink tracker. Double-tap RESET to enter the bootloader, then flash it straight from Chrome or Edge over Web Serial — no toolchain or drag-and-drop required. A UF2 download is provided as a fallback.",
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

    eyebrow = "Browser Flasher"
    if method == "uf2":
        eyebrow = "UF2 Drag &amp; Drop"
    elif method == "web-serial-dfu":
        eyebrow = "Web Serial Flasher"

    if method == "web-serial-dfu":
        bin_name = f"{slug}-{version}.bin"
        dat_name = f"{slug}-{version}.dat"
        uf2_name = f"{slug}-{version}.uf2"
        bin_attr = html.escape(f"{slug}/{bin_name}", quote=True)
        dat_attr = html.escape(f"{slug}/{dat_name}", quote=True)
        uf2_href = html.escape(f"{slug}/{uf2_name}", quote=True)
        action_html = f"""
          <div
            data-nrf-dfu
            data-bin="{bin_attr}"
            data-dat="{dat_attr}"
            class="grid gap-3"
          >
            <div class="flex flex-wrap items-center gap-4">
              <button data-nrf-connect class="inline-block border border-paper-line bg-paper-accent px-5 py-3 text-sm font-semibold text-paper-panel disabled:opacity-60">Connect &amp; Flash</button>
              {product_link}
            </div>
            <progress data-nrf-progress hidden class="h-2 w-full"></progress>
            <pre data-nrf-log hidden class="max-h-44 overflow-auto whitespace-pre-wrap border border-paper-line bg-[#fcfcfa] p-3 text-xs leading-5 text-paper-muted"></pre>
          </div>
          <div class="border border-paper-line bg-[#fcfcfa] p-4 text-sm leading-6 text-paper-muted">
            <p class="font-semibold text-paper-text">Flash over Web Serial (Chrome/Edge)</p>
            <ol class="mt-2 list-decimal space-y-1 pl-5">
              <li>Connect the tracker to your computer with a USB data cable.</li>
              <li>Double-tap the <strong>RESET</strong> button to enter the bootloader — a <code>TRACKER L1</code> drive appears.</li>
              <li>Click <strong>Connect &amp; Flash</strong> and pick the tracker's serial port. Progress shows below.</li>
            </ol>
            <p class="mt-3">Prefer drag-and-drop? <a class="text-paper-accent underline decoration-1 underline-offset-2" href="{uf2_href}" download>Download the UF2</a> and copy it onto the <code>TRACKER L1</code> drive instead.</p>
          </div>"""
    elif method == "uf2":
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
    <script type="module" src="nrf52-dfu.js"></script>
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
        <p class="text-sm leading-6 text-paper-muted">Use a USB data cable and Chrome or Edge on desktop. ESP32 boards (T5 ePaper, T-Deck) flash in-browser over Web Serial with ESP Web Tools; the nRF52 Wio Tracker L1 flashes over Web Serial too — double-tap RESET into the bootloader first, or fall back to copying its UF2 onto the bootloader drive.</p>
        <ol class="list-decimal space-y-1 pl-5 text-sm leading-6 text-paper-muted mt-2">
          <li>Put the board in bootloader mode if the browser cannot detect it.</li>
          <li>Choose erase when you want a clean install (ESP32 Web Serial only).</li>
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

        for name, _ in target.get("screenshots", []):
            img_path = assets_dir / name
            if img_path.is_file():
                shutil.copy2(img_path, target_dir / name)

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
