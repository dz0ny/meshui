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
        "tab_label": "T5 ePaper",
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
        # e-ink renders, so the slideshow shows them pixel-crisp (no smoothing).
        "pixelated": True,
        "screenshots": [
            ("wio-chat.png", "Messages"),
            ("wio-team.png", "Team"),
            ("wio-waypoints.png", "Waypoints"),
            ("wio-gps.png", "GPS status"),
            ("wio-compass.png", "Compass"),
            ("wio-settings.png", "Settings"),
            ("wio-keyboard.png", "On-screen keyboard"),
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


# shadcn/ui component class strings (faithful static port — same markup the React
# components emit, on the Tailwind CDN). Kept as constants so the f-strings below
# stay readable.
BTN_PRIMARY = (
    "inline-flex items-center justify-center gap-2 whitespace-nowrap rounded-md text-sm "
    "font-medium ring-offset-background transition-colors focus-visible:outline-none "
    "focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 "
    "disabled:pointer-events-none disabled:opacity-50 bg-primary text-primary-foreground "
    "hover:bg-primary/90 h-10 px-4 py-2"
)
BTN_LINK = "text-sm font-medium text-primary underline-offset-4 hover:underline"
BADGE_SECONDARY = (
    "inline-flex items-center rounded-md border border-transparent bg-secondary px-2.5 "
    "py-0.5 text-xs font-semibold text-secondary-foreground"
)
BADGE_OUTLINE = (
    "inline-flex items-center rounded-md border px-2.5 py-0.5 text-xs font-semibold text-foreground"
)
INSTR_PANEL = "rounded-md border bg-muted/40 p-4 text-sm leading-6 text-muted-foreground"
LOG_PRE = (
    "max-h-44 overflow-auto whitespace-pre-wrap rounded-md border bg-muted/40 p-3 "
    "font-mono text-xs leading-5 text-muted-foreground"
)


def build_target_panel(target: dict, version: str, active: bool) -> str:
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
            <div class="rounded-md border bg-muted/30 p-4">
              <img
                src="{slug}/{img}"
                alt="{device_name} home screen"
                class="mx-auto w-full max-w-sm rounded-md border object-cover"
              />
            </div>"""

    screenshots_html = ""
    if target.get("screenshots"):
        pix = " pixelated" if target.get("pixelated") else ""
        imgs = "\n".join(
            f'                <img src="{slug}/{html.escape(name)}" alt="{html.escape(alt)}"'
            f' class="slide w-full rounded-md border object-contain{pix}{" is-active" if i == 0 else ""}" />'
            for i, (name, alt) in enumerate(target["screenshots"])
        )
        screenshots_html = f"""
            <div class="space-y-3 rounded-md border bg-muted/30 p-4">
              <p class="text-xs font-medium uppercase tracking-wide text-muted-foreground">Screens</p>
              <div class="slideshow" data-slideshow>
{imgs}
              </div>
            </div>"""

    product_link = ""
    if product_url:
        product_link = f'<a class="{BTN_LINK}" href="{product_url}">Product page</a>'

    eyebrow = "Browser Flasher"
    if method == "uf2":
        eyebrow = "UF2 Drag &amp; Drop"
    elif method == "web-serial-dfu":
        eyebrow = "Web Serial Flasher"

    if method == "web-serial-dfu":
        bin_attr = html.escape(f"{slug}/{slug}-{version}.bin", quote=True)
        dat_attr = html.escape(f"{slug}/{slug}-{version}.dat", quote=True)
        uf2_href = html.escape(f"{slug}/{slug}-{version}.uf2", quote=True)
        action_html = f"""
            <div data-nrf-dfu data-bin="{bin_attr}" data-dat="{dat_attr}" class="space-y-4">
              <div class="flex flex-wrap items-center gap-3">
                <button data-nrf-connect class="{BTN_PRIMARY}">Connect &amp; Flash</button>
                {product_link}
              </div>
              <progress data-nrf-progress hidden class="ui-progress h-2 w-full"></progress>
              <pre data-nrf-log hidden class="{LOG_PRE}"></pre>
            </div>
            <div class="{INSTR_PANEL}">
              <p class="font-medium text-foreground">Flash over Web Serial (Chrome/Edge)</p>
              <ol class="mt-2 list-decimal space-y-1 pl-5">
                <li>Connect the tracker to your computer with a USB data cable.</li>
                <li>Double-tap the <strong>RESET</strong> button to enter the bootloader — a <code>TRACKER L1</code> drive appears.</li>
                <li>Click <strong>Connect &amp; Flash</strong> and pick the tracker's serial port. Progress shows below.</li>
              </ol>
              <p class="mt-3">Prefer drag-and-drop? <a class="{BTN_LINK}" href="{uf2_href}" download>Download the UF2</a> and copy it onto the <code>TRACKER L1</code> drive instead.</p>
            </div>"""
    elif method == "uf2":
        href = html.escape(f"{slug}/{slug}-{version}.uf2", quote=True)
        action_html = f"""
            <div class="flex flex-wrap items-center gap-3">
              <a class="{BTN_PRIMARY}" href="{href}" download>Download UF2 firmware</a>
              {product_link}
            </div>
            <div class="{INSTR_PANEL}">
              <p class="font-medium text-foreground">Install (drag &amp; drop)</p>
              <ol class="mt-2 list-decimal space-y-1 pl-5">
                <li>Connect the tracker to your computer over USB.</li>
                <li>Double-tap the <strong>RESET</strong> button to enter the bootloader — a <code>TRACKER L1</code> drive appears.</li>
                <li>Copy the downloaded <code>.uf2</code> onto that drive. The board reboots into the new firmware automatically.</li>
              </ol>
            </div>"""
    else:
        manifest = html.escape(f"{slug}/manifest.json", quote=True)
        action_html = f"""
            <div class="flex flex-wrap items-center gap-3">
              <esp-web-install-button manifest="{manifest}"></esp-web-install-button>
              {product_link}
            </div>"""

    hidden = "" if active else " hidden"
    return f"""
      <div role="tabpanel" data-panel="{slug}" aria-labelledby="tab-{slug}"{hidden}>
        <div class="rounded-lg border bg-card text-card-foreground shadow-sm">
          <div class="flex flex-col space-y-1.5 p-6">
            <div><span class="{BADGE_OUTLINE}">{eyebrow}</span></div>
            <h2 class="text-2xl font-semibold leading-none tracking-tight">{device_name}</h2>
            <p class="text-sm text-muted-foreground">{description}</p>
          </div>
          <div class="space-y-6 p-6 pt-0">
            <div class="flex flex-wrap gap-2">
              <span class="{BADGE_SECONDARY}">Chip: {chip_label}</span>
              <span class="{BADGE_SECONDARY}">Build: {safe_version}</span>
            </div>{action_html}{product_image_html}{screenshots_html}
          </div>
        </div>
      </div>"""


def build_tablist(targets: list[dict]) -> str:
    triggers = []
    for i, t in enumerate(targets):
        slug = t["slug"]
        label = html.escape(t.get("tab_label", t["device_name"]))
        state = "active" if i == 0 else "inactive"
        triggers.append(
            f'        <button role="tab" id="tab-{slug}" data-tab="{slug}" data-state="{state}" '
            f'aria-controls="{slug}" class="inline-flex flex-1 items-center justify-center '
            f'whitespace-nowrap rounded-sm px-3 py-1.5 text-sm font-medium ring-offset-background '
            f'transition-all focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring '
            f'data-[state=active]:bg-background data-[state=active]:text-foreground '
            f'data-[state=active]:shadow-sm">{label}</button>'
        )
    inner = "\n".join(triggers)
    return (
        '      <div role="tablist" class="inline-flex h-auto w-full flex-wrap items-center '
        'justify-center gap-1 rounded-md bg-muted p-1 text-muted-foreground">\n'
        f"{inner}\n"
        "      </div>"
    )


def build_page(version: str, repo_url: str) -> str:
    safe_repo_url = html.escape(repo_url, quote=True)
    tablist = build_tablist(TARGETS)
    panels = "\n".join(
        build_target_panel(t, version, active=(i == 0)) for i, t in enumerate(TARGETS)
    )
    return f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>{PROJECT_NAME}</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
    <script src="https://cdn.tailwindcss.com"></script>
    <script>
      tailwind.config = {{
        theme: {{
          extend: {{
            fontFamily: {{ sans: ["Inter", "ui-sans-serif", "system-ui", "sans-serif"] }},
            colors: {{
              border: "hsl(var(--border))",
              input: "hsl(var(--input))",
              ring: "hsl(var(--ring))",
              background: "hsl(var(--background))",
              foreground: "hsl(var(--foreground))",
              primary: {{ DEFAULT: "hsl(var(--primary))", foreground: "hsl(var(--primary-foreground))" }},
              secondary: {{ DEFAULT: "hsl(var(--secondary))", foreground: "hsl(var(--secondary-foreground))" }},
              muted: {{ DEFAULT: "hsl(var(--muted))", foreground: "hsl(var(--muted-foreground))" }},
              accent: {{ DEFAULT: "hsl(var(--accent))", foreground: "hsl(var(--accent-foreground))" }},
              card: {{ DEFAULT: "hsl(var(--card))", foreground: "hsl(var(--card-foreground))" }}
            }},
            borderRadius: {{
              lg: "var(--radius)",
              md: "calc(var(--radius) - 2px)",
              sm: "calc(var(--radius) - 4px)"
            }}
          }}
        }}
      }};
    </script>
    <script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
    <script type="module" src="nrf52-dfu.js"></script>
    <style>
      /* shadcn/ui "neutral" base color — light theme tokens */
      :root {{
        --background: 0 0% 100%;
        --foreground: 0 0% 3.9%;
        --card: 0 0% 100%;
        --card-foreground: 0 0% 3.9%;
        --primary: 0 0% 9%;
        --primary-foreground: 0 0% 98%;
        --secondary: 0 0% 96.1%;
        --secondary-foreground: 0 0% 9%;
        --muted: 0 0% 96.1%;
        --muted-foreground: 0 0% 45.1%;
        --accent: 0 0% 96.1%;
        --accent-foreground: 0 0% 9%;
        --border: 0 0% 89.8%;
        --input: 0 0% 89.8%;
        --ring: 0 0% 3.9%;
        --radius: 0.5rem;

        --esp-tools-button-color: hsl(var(--primary));
        --esp-tools-button-text-color: hsl(var(--primary-foreground));
        --esp-tools-button-border-radius: calc(var(--radius) - 2px);
      }}

      body {{ color-scheme: light; font-family: "Inter", ui-sans-serif, system-ui, sans-serif; }}

      esp-web-install-button::part(button) {{
        font-family: "Inter", ui-sans-serif, system-ui, sans-serif;
        font-weight: 500;
        font-size: 0.875rem;
        padding: 0.5rem 1rem;
        line-height: 1.5rem;
      }}

      /* screen slideshow — all slides stacked in one grid cell, cross-faded */
      .slideshow {{ display: grid; }}
      .slideshow > .slide {{ grid-area: 1 / 1; opacity: 0; transition: opacity .6s ease; }}
      .slideshow > .slide.is-active {{ opacity: 1; }}
      .pixelated {{ image-rendering: pixelated; }}

      /* native <progress> styled as a shadcn Progress bar */
      progress.ui-progress {{ -webkit-appearance: none; appearance: none; border: 0; }}
      progress.ui-progress::-webkit-progress-bar {{ background: hsl(var(--secondary)); border-radius: 9999px; }}
      progress.ui-progress::-webkit-progress-value {{ background: hsl(var(--primary)); border-radius: 9999px; }}
      progress.ui-progress::-moz-progress-bar {{ background: hsl(var(--primary)); border-radius: 9999px; }}
    </style>
  </head>
  <body class="min-h-screen bg-background text-foreground antialiased">
    <main class="mx-auto w-[min(48rem,calc(100vw-2rem))] space-y-8 py-12 max-sm:py-8">
      <header class="space-y-2">
        <h1 class="text-3xl font-bold tracking-tight sm:text-4xl">{PROJECT_NAME}</h1>
        <p class="text-muted-foreground">Pick your device, then flash it from the browser. <a class="font-medium text-primary underline-offset-4 hover:underline" href="{safe_repo_url}">Repository</a></p>
      </header>
      <div class="space-y-4">
{tablist}
{panels}
      </div>
      <div class="{INSTR_PANEL}">
        <p>Use a USB data cable and Chrome or Edge on desktop. ESP32 boards (T5 ePaper, T-Deck) flash in-browser over Web Serial with ESP Web Tools; the nRF52 Wio Tracker L1 flashes over Web Serial too — double-tap RESET into the bootloader first, or fall back to copying its UF2 onto the bootloader drive.</p>
        <ol class="mt-2 list-decimal space-y-1 pl-5">
          <li>Put the board in bootloader mode if the browser cannot detect it.</li>
          <li>Choose erase when you want a clean install (ESP32 Web Serial only).</li>
        </ol>
      </div>
    </main>
    <script>
      (function () {{
        var tabs = document.querySelectorAll("[data-tab]");
        var panels = document.querySelectorAll("[data-panel]");
        function activate(slug) {{
          tabs.forEach(function (t) {{
            t.setAttribute("data-state", t.dataset.tab === slug ? "active" : "inactive");
          }});
          panels.forEach(function (p) {{ p.hidden = p.dataset.panel !== slug; }});
        }}
        tabs.forEach(function (t) {{
          t.addEventListener("click", function () {{ activate(t.dataset.tab); }});
        }});
      }})();

      // Auto-rotating screen slideshows: one slide visible, advance every 3s.
      (function () {{
        document.querySelectorAll("[data-slideshow]").forEach(function (box) {{
          var slides = box.querySelectorAll(".slide");
          if (slides.length < 2) return;
          var i = 0;
          setInterval(function () {{
            slides[i].classList.remove("is-active");
            i = (i + 1) % slides.length;
            slides[i].classList.add("is-active");
          }}, 3000);
        }});
      }})();
    </script>
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
