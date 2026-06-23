// MeshCore PaperUI web flasher — Preact front-end.
//
// All page content is rendered client-side from the config blob the build step
// injects as window.__FLASHER__ (see tools/build_web_flasher.py). Preact + htm
// are pulled from esm.sh; esp-web-tools (a custom element) and the nRF52 Web
// Serial DFU module (./nrf52-dfu.mjs) are loaded by the shell / imported here.

import { h, render } from "https://esm.sh/preact@10.24.3";
import {
  useState,
  useEffect,
  useRef,
} from "https://esm.sh/preact@10.24.3/hooks";
import htm from "https://esm.sh/htm@3.1.1";
import { wire as wireDfu } from "./nrf52-dfu.mjs";

const html = htm.bind(h);
const cfg = window.__FLASHER__ || { targets: [] };

// Load a classic (non-module) script once, resolving when it has executed.
const scriptCache = {};
function loadScript(src) {
  if (scriptCache[src]) return scriptCache[src];
  scriptCache[src] = new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error(src + " failed to load"));
    document.head.appendChild(s);
  });
  return scriptCache[src];
}

// ---- Actions ---------------------------------------------------------------

function btnClass() {
  return (
    "inline-flex items-center justify-center gap-2 whitespace-nowrap rounded-md " +
    "text-sm font-medium ring-offset-background transition-colors focus-visible:outline-none " +
    "focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 " +
    "disabled:pointer-events-none disabled:opacity-50 bg-primary text-primary-foreground " +
    "hover:bg-primary/90 h-10 px-4 py-2"
  );
}

function ProductLink({ url }) {
  if (!url) return null;
  return html`<a
    class="text-sm font-medium text-primary underline-offset-4 hover:underline"
    href=${url}
    >Product page</a
  >`;
}

function EspAction({ action, productUrl }) {
  return html`<div class="flex flex-wrap items-center gap-3">
    <esp-web-install-button manifest=${action.manifest}></esp-web-install-button>
    <${ProductLink} url=${productUrl} />
  </div>`;
}

function Uf2Action({ action, productUrl }) {
  return html`<div class="space-y-4">
    <div class="flex flex-wrap items-center gap-3">
      <a class=${btnClass()} href=${action.uf2} download>Download UF2 firmware</a>
      <${ProductLink} url=${productUrl} />
    </div>
    <div
      class="rounded-md border bg-muted/40 p-4 text-sm leading-6 text-muted-foreground"
    >
      <p class="font-medium text-foreground">Install (drag & drop)</p>
      <ol class="mt-2 list-decimal space-y-1 pl-5">
        <li>Connect the tracker to your computer over USB.</li>
        <li>
          Double-tap the <strong>RESET</strong> button to enter the bootloader —
          a <code>TRACKER L1</code> drive appears.
        </li>
        <li>
          Copy the downloaded <code>.uf2</code> onto that drive. The board
          reboots into the new firmware automatically.
        </li>
      </ol>
    </div>
  </div>`;
}

function DfuAction({ action, productUrl }) {
  const root = useRef(null);
  useEffect(() => {
    if (root.current) wireDfu(root.current);
  }, []);
  return html`<div class="space-y-4">
    <div
      ref=${root}
      data-nrf-dfu
      data-bin=${action.bin}
      data-dat=${action.dat}
      class="space-y-4"
    >
      <div class="flex flex-wrap items-center gap-3">
        <button data-nrf-connect class=${btnClass()}>Connect & Flash</button>
        <${ProductLink} url=${productUrl} />
      </div>
      <progress data-nrf-progress hidden class="ui-progress h-2 w-full"></progress>
      <pre
        data-nrf-log
        hidden
        class="max-h-44 overflow-auto whitespace-pre-wrap rounded-md border bg-muted/40 p-3 font-mono text-xs leading-5 text-muted-foreground"
      ></pre>
    </div>
    <div
      class="rounded-md border bg-muted/40 p-4 text-sm leading-6 text-muted-foreground"
    >
      <p class="font-medium text-foreground">Flash over Web Serial (Chrome/Edge)</p>
      <ol class="mt-2 list-decimal space-y-1 pl-5">
        <li>Connect the tracker to your computer with a USB data cable.</li>
        <li>
          Double-tap the <strong>RESET</strong> button to enter the bootloader —
          a <code>TRACKER L1</code> drive appears.
        </li>
        <li>
          Click <strong>Connect & Flash</strong> and pick the tracker's serial
          port. Progress shows below.
        </li>
      </ol>
      <p class="mt-3">
        Prefer drag-and-drop?
        <a
          class="text-sm font-medium text-primary underline-offset-4 hover:underline"
          href=${action.uf2}
          download
          >Download the UF2</a
        >
        and copy it onto the <code>TRACKER L1</code> drive instead.
      </p>
    </div>
  </div>`;
}

function Action({ target }) {
  if (target.method === "web-serial-dfu")
    return html`<${DfuAction} action=${target.action} productUrl=${target.productUrl} />`;
  if (target.method === "uf2")
    return html`<${Uf2Action} action=${target.action} productUrl=${target.productUrl} />`;
  return html`<${EspAction} action=${target.action} productUrl=${target.productUrl} />`;
}

// ---- Screen gallery --------------------------------------------------------

function Gallery({ shots, maxW, pixelated }) {
  const [i, setI] = useState(0);
  const [auto, setAuto] = useState(shots.length > 1);
  useEffect(() => {
    if (!auto) return;
    const t = setInterval(() => setI((n) => (n + 1) % shots.length), 3000);
    return () => clearInterval(t);
  }, [auto, shots.length]);
  const pick = (n) => {
    setAuto(false);
    setI(n);
  };
  const cur = shots[i];
  return html`<div class="space-y-3 rounded-md border bg-muted/30 p-4">
    <p class="text-xs font-medium uppercase tracking-wide text-muted-foreground">
      Screens
    </p>
    <div class="gallery">
      <div class="slideshow mx-auto" style=${{ maxWidth: maxW }}>
        ${shots.map(
          (s, n) => html`<img
            key=${s.src}
            src=${s.src}
            alt=${s.name}
            class=${"slide rounded-md border object-contain" +
            (pixelated ? " pixelated" : "") +
            (n === i ? " is-active" : "")}
          />`
        )}
      </div>
      <div class="space-y-1 text-center">
        <p class="text-sm font-medium text-foreground">${cur.name}</p>
        <p class="text-xs text-muted-foreground">${cur.desc}</p>
      </div>
      <div class="flex flex-wrap justify-center gap-2">
        ${shots.map(
          (s, n) => html`<button
            key=${s.src}
            type="button"
            onClick=${() => pick(n)}
            data-state=${n === i ? "active" : "inactive"}
            class="rounded-full border px-3 py-1 text-xs font-medium transition-colors data-[state=active]:bg-primary data-[state=active]:text-primary-foreground data-[state=inactive]:bg-background data-[state=inactive]:text-muted-foreground data-[state=inactive]:hover:text-foreground"
          >
            ${s.name}
          </button>`
        )}
      </div>
    </div>
  </div>`;
}

// ---- Live previews (WASM) --------------------------------------------------

const T5_SCREENS = [
  [0, "Home"], [1, "Contacts"], [2, "Messages"], [3, "Settings"], [8, "Display"],
  [9, "GPS settings"], [10, "Mesh settings"], [15, "Bluetooth"], [4, "GPS"], [5, "Battery"],
  [6, "Mesh"], [7, "Status"], [25, "Trail"], [29, "Team"], [35, "Waypoints"], [12, "Lock"],
];

function T5Preview() {
  const canvasRef = useRef(null);
  const langRef = useRef(null);
  const [status, setStatus] = useState("Loading preview…");

  useEffect(() => {
    let M = null,
      down = false,
      tickTimer = null,
      cancelled = false;
    const canvas = canvasRef.current;
    const ctx = canvas.getContext("2d");

    function draw() {
      const w = M._sim_w(),
        h = M._sim_h(),
        st = M._sim_stride(),
        ptr = M._sim_pixels();
      const src = M.HEAPU8.subarray(ptr, ptr + st * h);
      const img = ctx.createImageData(w, h);
      for (let y = 0; y < h; y++)
        for (let x = 0; x < w; x++) {
          const v = src[y * st + x],
            o = (y * w + x) * 4;
          img.data[o] = v;
          img.data[o + 1] = v;
          img.data[o + 2] = v;
          img.data[o + 3] = 255;
        }
      ctx.putImageData(img, 0, 0);
    }
    function boot() {
      M._sim_boot(+langRef.current.value);
      draw();
    }
    function panelXY(e) {
      const r = canvas.getBoundingClientRect();
      return [
        Math.round((e.clientX - r.left) * (540 / r.width)),
        Math.round((e.clientY - r.top) * (960 / r.height)),
      ];
    }

    loadScript("sim_t5.js")
      .then(() => {
        if (typeof SimT5Module !== "function")
          throw new Error("sim_t5.js failed to load");
        return SimT5Module();
      })
      .then((mod) => {
        if (cancelled) return;
        M = mod;
        setStatus("");
        boot();
        canvas.__boot = boot; // exposed for the lang/reset handlers below
        tickTimer = setInterval(() => {
          M._sim_tick(250);
          draw();
        }, 250);

        canvas.addEventListener("pointerdown", (e) => {
          e.preventDefault();
          down = true;
          canvas.setPointerCapture(e.pointerId);
          const p = panelXY(e);
          M._sim_touch(p[0], p[1], 1);
          draw();
        });
        canvas.addEventListener("pointermove", (e) => {
          if (!down) return;
          const p = panelXY(e);
          M._sim_touch(p[0], p[1], 1);
          draw();
        });
        const up = (e) => {
          if (!down) return;
          down = false;
          const p = panelXY(e);
          M._sim_touch(p[0], p[1], 0);
          M._sim_tick(40);
          draw();
        };
        canvas.addEventListener("pointerup", up);
        canvas.addEventListener("pointercancel", up);
        canvas.__goto = (v) => {
          M._sim_goto(v);
          draw();
        };
      })
      .catch((err) => {
        if (!cancelled) setStatus("Preview failed: " + err.message);
      });

    return () => {
      cancelled = true;
      if (tickTimer) clearInterval(tickTimer);
    };
  }, []);

  return html`<div class="space-y-3 rounded-md border bg-muted/30 p-4">
    <p class="text-xs font-medium uppercase tracking-wide text-muted-foreground">
      Live preview
    </p>
    <p class="text-xs text-muted-foreground">
      The real LVGL e-paper UI running in your browser (WebAssembly) on mock data
      — tap and scroll it like the touchscreen before you flash.
    </p>
    <div class="sim sim-t5 flex flex-col items-center gap-3">
      <div class="flex flex-wrap items-center justify-center gap-2">
        <select
          class="ctl"
          onChange=${(e) =>
            canvasRef.current.__goto &&
            canvasRef.current.__goto(+e.target.value)}
        >
          ${T5_SCREENS.map(
            (r) => html`<option key=${r[0]} value=${r[0]}>${r[1]}</option>`
          )}
        </select>
        <select
          ref=${langRef}
          class="ctl"
          onChange=${() => canvasRef.current.__boot && canvasRef.current.__boot()}
        >
          <option value="0">EN</option>
          <option value="1">SL</option>
        </select>
        <button
          class="ctl"
          type="button"
          onClick=${() => canvasRef.current.__boot && canvasRef.current.__boot()}
        >
          Reset
        </button>
      </div>
      <canvas ref=${canvasRef} width="540" height="960"></canvas>
      <p class="text-xs text-muted-foreground">${status}</p>
    </div>
  </div>`;
}

const WIO_SCREENS = [
  [31, "Dashboard"], [0, "Home menu"], [2, "Messages"], [26, "Quick reply"], [29, "Team"],
  [25, "Trail"], [35, "Waypoints"], [36, "Waypoint detail"], [27, "Compass"], [3, "Settings"],
  [9, "GPS settings"], [10, "Mesh settings"], [8, "Display"], [30, "Sound"], [28, "Privacy"],
  [15, "Bluetooth"], [5, "Battery"], [7, "Status"], [4, "GPS info"], [6, "Mesh info"], [32, "Provision"],
];
const WIO_KEYMAP = {
  ArrowUp: 85, ArrowDown: 68, ArrowLeft: 76, ArrowRight: 82,
  Enter: 69, " ": 69, Backspace: 66, Escape: 66,
};

function WioPreview({ active }) {
  const canvasRef = useRef(null);
  const langRef = useRef(null);
  const ctl = useRef({}); // exposed sim handles
  const [status, setStatus] = useState("Loading preview…");
  const [invertLabel, setInvertLabel] = useState("Dark mode");

  useEffect(() => {
    let M = null,
      inverted = false,
      tickTimer = null,
      cancelled = false;
    const canvas = canvasRef.current;
    const ctx = canvas.getContext("2d");

    function draw() {
      const w = M._sim_w(),
        h = M._sim_h(),
        ptr = M._sim_pixels();
      const gray = M.HEAPU8.subarray(ptr, ptr + w * h);
      const img = ctx.createImageData(w, h);
      for (let i = 0; i < w * h; i++) {
        const v = gray[i];
        img.data[i * 4] = v;
        img.data[i * 4 + 1] = v;
        img.data[i * 4 + 2] = v;
        img.data[i * 4 + 3] = 255;
      }
      const off = document.createElement("canvas");
      off.width = w;
      off.height = h;
      off.getContext("2d").putImageData(img, 0, 0);
      const scale = 2; // Wio 250x122 mono panel, doubled
      canvas.width = w * scale;
      canvas.height = h * scale;
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(off, 0, 0, canvas.width, canvas.height);
    }
    function boot() {
      M._sim_boot(0, +langRef.current.value); // target 0 = Wio panel
      inverted = false;
      setInvertLabel("Dark mode");
      draw();
    }
    function key(k) {
      M._sim_key(k);
      draw();
    }

    loadScript("sim.js")
      .then(() => {
        if (typeof SimModule !== "function")
          throw new Error("sim.js failed to load");
        return SimModule();
      })
      .then((mod) => {
        if (cancelled) return;
        M = mod;
        setStatus("");
        boot();
        tickTimer = setInterval(() => {
          M._sim_tick(250);
          draw();
        }, 250);
        ctl.current = {
          key,
          boot,
          goto: (v) => {
            M._sim_goto(v);
            draw();
          },
          invert: () => {
            inverted = !inverted;
            M._sim_set_invert(inverted ? 1 : 0);
            setInvertLabel(inverted ? "Light mode" : "Dark mode");
            draw();
          },
        };
      })
      .catch((err) => {
        if (!cancelled) setStatus("Preview failed: " + err.message);
      });

    return () => {
      cancelled = true;
      if (tickTimer) clearInterval(tickTimer);
    };
  }, []);

  // Arrow-key steering, but only while this panel is the active tab.
  useEffect(() => {
    if (!active) return;
    const onKey = (e) => {
      const k = WIO_KEYMAP[e.key];
      if (k !== undefined && ctl.current.key) {
        e.preventDefault();
        ctl.current.key(k);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [active]);

  const dpad = [
    ["u", 85, "▲"], ["l", 76, "◀"], ["e", 69, "●"],
    ["r", 82, "▶"], ["d", 68, "▼"], ["b", 66, "⤲"],
  ];
  return html`<div class="space-y-3 rounded-md border bg-muted/30 p-4">
    <p class="text-xs font-medium uppercase tracking-wide text-muted-foreground">
      Live preview
    </p>
    <p class="text-xs text-muted-foreground">
      The real mono UI running in your browser (WebAssembly) on mock data —
      navigate it with the joystick or arrow keys before you flash.
    </p>
    <div class="sim sim-wio flex flex-col items-center gap-3">
      <div class="flex flex-wrap items-center justify-center gap-2">
        <select
          class="ctl"
          onChange=${(e) => ctl.current.goto && ctl.current.goto(+e.target.value)}
        >
          ${WIO_SCREENS.map(
            (r) => html`<option key=${r[0]} value=${r[0]}>${r[1]}</option>`
          )}
        </select>
        <select
          ref=${langRef}
          class="ctl"
          onChange=${() => ctl.current.boot && ctl.current.boot()}
        >
          <option value="0">EN</option>
          <option value="1">SL</option>
        </select>
        <button
          class="ctl"
          type="button"
          onClick=${() => ctl.current.invert && ctl.current.invert()}
        >
          ${invertLabel}
        </button>
        <button
          class="ctl"
          type="button"
          onClick=${() => ctl.current.boot && ctl.current.boot()}
        >
          Reset
        </button>
      </div>
      <canvas ref=${canvasRef}></canvas>
      <div class="dpad">
        ${dpad.map(
          ([pos, k, glyph]) => html`<button
            key=${pos}
            class=${"ctl " + pos}
            onClick=${() => ctl.current.key && ctl.current.key(k)}
          >
            ${glyph}
          </button>`
        )}
      </div>
      <p class="text-xs text-muted-foreground">${status}</p>
    </div>
  </div>`;
}

function Preview({ target, active }) {
  if (target.preview === "t5") return html`<${T5Preview} />`;
  if (target.preview === "wio") return html`<${WioPreview} active=${active} />`;
  return null;
}

// ---- Panel + tabs ----------------------------------------------------------

function Panel({ target, active }) {
  return html`<div role="tabpanel" aria-labelledby=${"tab-" + target.slug}>
    <div class="rounded-lg border bg-card text-card-foreground shadow-sm">
      <div class="flex flex-col space-y-1.5 p-6">
        <div>
          <span
            class="inline-flex items-center rounded-md border px-2.5 py-0.5 text-xs font-semibold text-foreground"
            >${target.eyebrow}</span
          >
        </div>
        <h2 class="text-2xl font-semibold leading-none tracking-tight">
          ${target.deviceName}
        </h2>
        <p class="text-sm text-muted-foreground">${target.description}</p>
      </div>
      <div class="space-y-6 p-6 pt-0">
        <div class="flex flex-wrap gap-2">
          <span
            class="inline-flex items-center rounded-md border border-transparent bg-secondary px-2.5 py-0.5 text-xs font-semibold text-secondary-foreground"
            >Chip: ${target.chipLabel}</span
          >
          <span
            class="inline-flex items-center rounded-md border border-transparent bg-secondary px-2.5 py-0.5 text-xs font-semibold text-secondary-foreground"
            >Build: ${cfg.version}</span
          >
        </div>
        <${Preview} target=${target} active=${active} />
        <${Action} target=${target} />
        ${target.productImage &&
        html`<div class="rounded-md border bg-muted/30 p-4">
          <img
            src=${target.productImage}
            alt=${target.deviceName + " home screen"}
            class="mx-auto w-full max-w-sm rounded-md border object-cover"
          />
        </div>`}
        ${target.screenshots &&
        target.screenshots.length > 0 &&
        html`<${Gallery}
          shots=${target.screenshots}
          maxW=${target.screenMaxW}
          pixelated=${target.pixelated}
        />`}
      </div>
    </div>
  </div>`;
}

// Firmware highlights, rendered once above the device tabs. A per-feature note
// (e.g. "Wio Tracker L1") flags anything that isn't on every build.
function Features({ items }) {
  if (!items || !items.length) return null;
  return html`<section
    class="rounded-lg border bg-card text-card-foreground shadow-sm"
  >
    <div class="flex flex-col space-y-1.5 p-6 pb-3">
      <h2 class="text-lg font-semibold leading-none tracking-tight">
        Firmware highlights
      </h2>
    </div>
    <div class="grid gap-4 p-6 pt-0 sm:grid-cols-2">
      ${items.map(
        (f) => html`<div key=${f.title} class="space-y-1">
          <div class="flex flex-wrap items-center gap-2">
            <h3 class="text-sm font-semibold text-foreground">${f.title}</h3>
            ${f.note &&
            html`<span
              class="inline-flex items-center rounded-md border border-transparent bg-secondary px-2 py-0.5 text-[10px] font-semibold uppercase tracking-wide text-secondary-foreground"
              >${f.note}</span
            >`}
          </div>
          <p class="text-sm text-muted-foreground">${f.desc}</p>
        </div>`
      )}
    </div>
  </section>`;
}

function App() {
  const targets = cfg.targets || [];
  const [activeSlug, setActiveSlug] = useState(
    targets.length ? targets[0].slug : null
  );
  return html`<main
    class="mx-auto w-[min(48rem,calc(100vw-2rem))] space-y-8 py-12 max-sm:py-8"
  >
    <header class="space-y-2">
      <h1 class="text-3xl font-bold tracking-tight sm:text-4xl">
        ${cfg.projectName}
      </h1>
      <p class="text-muted-foreground">
        Pick your device, then flash it from the browser.${" "}
        <a
          class="font-medium text-primary underline-offset-4 hover:underline"
          href=${cfg.repoUrl}
          >Repository</a
        >
      </p>
    </header>
    <${Features} items=${cfg.features} />
    <div class="space-y-4">
      <div
        role="tablist"
        class="inline-flex h-auto w-full flex-wrap items-center justify-center gap-1 rounded-md bg-muted p-1 text-muted-foreground"
      >
        ${targets.map(
          (t) => html`<button
            key=${t.slug}
            role="tab"
            id=${"tab-" + t.slug}
            aria-controls=${t.slug}
            aria-selected=${t.slug === activeSlug}
            data-state=${t.slug === activeSlug ? "active" : "inactive"}
            onClick=${() => setActiveSlug(t.slug)}
            class="inline-flex flex-1 items-center justify-center whitespace-nowrap rounded-sm px-3 py-1.5 text-sm font-medium ring-offset-background transition-all focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring data-[state=active]:bg-background data-[state=active]:text-foreground data-[state=active]:shadow-sm"
          >
            ${t.tabLabel}
          </button>`
        )}
      </div>
      ${targets.map(
        (t) => html`<div key=${t.slug} hidden=${t.slug !== activeSlug}>
          <${Panel} target=${t} active=${t.slug === activeSlug} />
        </div>`
      )}
    </div>
    <div
      class="rounded-md border bg-muted/40 p-4 text-sm leading-6 text-muted-foreground"
    >
      <p>
        Use a USB data cable and Chrome or Edge on desktop. ESP32 boards (T5
        ePaper, T-Deck) flash in-browser over Web Serial with ESP Web Tools; the
        nRF52 Wio Tracker L1 flashes over Web Serial too — double-tap RESET into
        the bootloader first, or fall back to copying its UF2 onto the bootloader
        drive.
      </p>
      <ol class="mt-2 list-decimal space-y-1 pl-5">
        <li>Put the board in bootloader mode if the browser cannot detect it.</li>
        <li>Choose erase when you want a clean install (ESP32 Web Serial only).</li>
      </ol>
    </div>
  </main>`;
}

render(html`<${App} />`, document.getElementById("app"));
