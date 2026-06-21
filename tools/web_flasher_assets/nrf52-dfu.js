// Browser Web Serial flasher for the Adafruit/Nordic *legacy* SLIP serial DFU
// (dfu_version 0.5) — the protocol the Adafruit nRF52 bootloader speaks and that
// `adafruit-nrfutil dfu serial` (bundled with PlatformIO's nordicnrf52 platform)
// uses for `pio run -t upload`.
//
// This is a faithful port of nordicsemi/dfu/dfu_transport_serial.py +
// util.py + crc16.py from tool-adafruit-nrfutil. Keep the constants and the
// HCI/SLIP framing in sync with that source if it ever changes.
//
// It consumes the two members of the PlatformIO-produced firmware.zip that the
// build step extracts next to this page: the application image (firmware.bin)
// and the init packet (firmware.dat). No in-browser unzip needed.
//
// Chrome/Edge desktop only (Web Serial API). Self-wires onto any
// `[data-nrf-dfu]` element on the page.

const DATA_INTEGRITY_CHECK_PRESENT = 1;
const RELIABLE_PACKET = 1;
const HCI_PACKET_TYPE = 14;

const DFU_INIT_PACKET = 1;
const DFU_START_PACKET = 3;
const DFU_DATA_PACKET = 4;
const DFU_STOP_DATA_PACKET = 5;

const DFU_UPDATE_MODE_APP = 4;

const DFU_PACKET_MAX_SIZE = 512;
const FLASH_PAGE_SIZE = 4096;
const FLASH_PAGE_ERASE_TIME = 0.0897; // s, nrf52840 worst case per page
const FLASH_WORD_WRITE_TIME = 0.0001; // s
const FLASH_PAGE_WRITE_TIME = (FLASH_PAGE_SIZE / 4) * FLASH_WORD_WRITE_TIME;

const BAUD_RATE = 115200;
const ACK_TIMEOUT_MS = 2000;

const SLIP_END = 0xc0;
const SLIP_ESC = 0xdb;
const SLIP_ESC_END = 0xdc;
const SLIP_ESC_ESC = 0xdd;

// ---- helpers ---------------------------------------------------------------

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function int32le(v) {
  return [v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff];
}
function int16le(v) {
  return [v & 0xff, (v >>> 8) & 0xff];
}

// nordicsemi/dfu/crc16.py calc_crc16 — CRC16-CCITT, init 0xffff.
function calcCrc16(bytes, crc = 0xffff) {
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    crc = ((crc >> 8) & 0x00ff) | ((crc << 8) & 0xff00);
    crc ^= b;
    crc ^= (crc & 0x00ff) >> 4;
    crc ^= (crc << 8) << 4;
    crc ^= ((crc & 0x00ff) << 4) << 1;
  }
  return crc & 0xffff;
}

// util.py slip_parts_to_four_bytes — the 4-byte HCI/SLIP header.
function slipHeader(seq, dip, rp, pktType, pktLen) {
  const b0 = seq | (((seq + 1) % 8) << 3) | (dip << 6) | (rp << 7);
  const b1 = pktType | ((pktLen & 0x000f) << 4);
  const b2 = (pktLen & 0x0ff0) >> 4;
  const b3 = (~(b0 + b1 + b2) + 1) & 0xff; // header checksum
  return [b0, b1, b2, b3];
}

// util.py slip_encode_esc_chars
function slipEncode(bytes) {
  const out = [];
  for (const c of bytes) {
    if (c === SLIP_END) out.push(SLIP_ESC, SLIP_ESC_END);
    else if (c === SLIP_ESC) out.push(SLIP_ESC, SLIP_ESC_ESC);
    else out.push(c);
  }
  return out;
}

// dfu_transport_serial.py decode_esc_chars
function slipDecode(bytes) {
  const out = [];
  for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] === SLIP_ESC) {
      const n = bytes[++i];
      if (n === SLIP_ESC_END) out.push(SLIP_END);
      else if (n === SLIP_ESC_ESC) out.push(SLIP_ESC);
      else throw new Error("Bad SLIP escape 0x" + n.toString(16));
    } else {
      out.push(bytes[i]);
    }
  }
  return out;
}

let hciSeq = 0;

// dfu_transport_serial.py HciPacket — wrap a payload into a framed SLIP packet.
function hciPacket(data) {
  hciSeq = (hciSeq + 1) % 8;
  let temp = slipHeader(
    hciSeq,
    DATA_INTEGRITY_CHECK_PRESENT,
    RELIABLE_PACKET,
    HCI_PACKET_TYPE,
    data.length
  );
  temp = temp.concat(data);
  const crc = calcCrc16(temp);
  temp.push(crc & 0xff, (crc >> 8) & 0xff);
  return [SLIP_END, ...slipEncode(temp), SLIP_END];
}

// ---- transport -------------------------------------------------------------

class SerialDfu {
  constructor(port, log) {
    this.port = port;
    this.log = log || (() => {});
    this.rx = []; // accumulated received bytes
    this._pump = null;
    this._reader = null;
    this._writer = null;
  }

  async open() {
    await this.port.open({ baudRate: BAUD_RATE });
    this._writer = this.port.writable.getWriter();
    this._reader = this.port.readable.getReader();
    this._pump = this._readLoop();
  }

  async _readLoop() {
    try {
      for (;;) {
        const { value, done } = await this._reader.read();
        if (done) break;
        if (value) for (const b of value) this.rx.push(b);
      }
    } catch (_) {
      /* reader cancelled on close */
    }
  }

  async close() {
    try {
      if (this._reader) await this._reader.cancel();
    } catch (_) {}
    try {
      if (this._writer) this._writer.releaseLock();
    } catch (_) {}
    try {
      await this.port.close();
    } catch (_) {}
  }

  async _write(bytes) {
    await this._writer.write(new Uint8Array(bytes));
  }

  // dfu_transport_serial.py get_ack_nr — wait for a full SLIP frame and return
  // the acknowledged sequence number from its header.
  async _waitAck() {
    const start = Date.now();
    for (;;) {
      const first = this.rx.indexOf(SLIP_END);
      if (first !== -1) {
        const second = this.rx.indexOf(SLIP_END, first + 1);
        if (second !== -1) {
          const frame = this.rx.slice(first, second + 1);
          this.rx = this.rx.slice(second + 1);
          const decoded = slipDecode(frame.slice(1, -1)); // strip 0xC0 framing
          return (decoded[0] >> 3) & 0x07;
        }
      }
      if (Date.now() - start > ACK_TIMEOUT_MS) {
        hciSeq = 0;
        throw new Error("Timed out waiting for ACK from device.");
      }
      await sleep(5);
    }
  }

  async _sendPacket(payload) {
    await this._write(hciPacket(payload));
    await this._waitAck();
  }

  // send_start_dfu(mode=APP, sd=0, bl=0, app=appSize)
  async sendStart(appSize) {
    const frame = []
      .concat(int32le(DFU_START_PACKET))
      .concat(int32le(DFU_UPDATE_MODE_APP))
      .concat(int32le(0)) // softdevice size
      .concat(int32le(0)) // bootloader size
      .concat(int32le(appSize)); // application size
    await this._sendPacket(frame);
    // Let the bootloader erase the application bank before streaming data.
    const eraseMs =
      Math.max(0.5, (Math.floor(appSize / FLASH_PAGE_SIZE) + 1) * FLASH_PAGE_ERASE_TIME) *
      1000;
    this.log(`Erasing flash (~${(eraseMs / 1000).toFixed(1)}s)…`);
    await sleep(eraseMs);
  }

  // send_init_packet(init) — opcode + .dat + 2-byte padding
  async sendInit(initPacket) {
    const frame = []
      .concat(int32le(DFU_INIT_PACKET))
      .concat(Array.from(initPacket))
      .concat(int16le(0x0000));
    await this._sendPacket(frame);
  }

  // send_firmware(firmware) — 512-byte data packets, then a stop packet.
  async sendFirmware(firmware, onProgress) {
    const total = firmware.length;
    let count = 0;
    for (let i = 0; i < total; i += DFU_PACKET_MAX_SIZE) {
      const chunk = firmware.subarray(i, i + DFU_PACKET_MAX_SIZE);
      const frame = int32le(DFU_DATA_PACKET).concat(Array.from(chunk));
      await this._sendPacket(frame);
      count++;
      if (count % 8 === 0) await sleep(FLASH_PAGE_WRITE_TIME * 1000);
      if (onProgress) onProgress(Math.min(i + DFU_PACKET_MAX_SIZE, total), total);
    }
    await sleep(FLASH_PAGE_WRITE_TIME * 1000);
    await this._sendPacket(int32le(DFU_STOP_DATA_PACKET));
  }

  activateWaitMs(appSize) {
    const erase =
      Math.max(0.5, (Math.floor(appSize / FLASH_PAGE_SIZE) + 1) * FLASH_PAGE_ERASE_TIME);
    const write = (Math.floor(appSize / FLASH_PAGE_SIZE) + 1) * FLASH_PAGE_WRITE_TIME;
    return (erase + write) * 1000;
  }
}

// ---- orchestration ---------------------------------------------------------

async function flash(port, binBuf, datBuf, ui) {
  hciSeq = 0;
  const firmware = new Uint8Array(binBuf);
  const initPacket = new Uint8Array(datBuf);
  const dfu = new SerialDfu(port, ui.log);

  await dfu.open();
  try {
    ui.log("Connected. Starting DFU…");
    await dfu.sendStart(firmware.length);
    ui.log(`Sending init packet (${initPacket.length} bytes)…`);
    await dfu.sendInit(initPacket);
    ui.log(`Sending firmware (${firmware.length} bytes)…`);
    await dfu.sendFirmware(firmware, (done, total) => ui.progress(done, total));
    ui.log("Activating new firmware…");
    await sleep(dfu.activateWaitMs(firmware.length));
  } finally {
    await dfu.close();
  }
  ui.log("Done. The board reboots into the new firmware.");
}

// ---- DOM wiring ------------------------------------------------------------

function wire(root) {
  const btn = root.querySelector("[data-nrf-connect]");
  const bar = root.querySelector("[data-nrf-progress]");
  const logEl = root.querySelector("[data-nrf-log]");
  const binUrl = root.getAttribute("data-bin");
  const datUrl = root.getAttribute("data-dat");

  const log = (msg) => {
    if (logEl) {
      logEl.hidden = false;
      logEl.textContent += (logEl.textContent ? "\n" : "") + msg;
      logEl.scrollTop = logEl.scrollHeight;
    }
  };
  const progress = (done, total) => {
    if (bar) {
      bar.hidden = false;
      bar.max = total;
      bar.value = done;
    }
  };

  if (!("serial" in navigator)) {
    btn.disabled = true;
    btn.textContent = "Web Serial unsupported — use Chrome/Edge";
    return;
  }

  btn.addEventListener("click", async () => {
    btn.disabled = true;
    if (logEl) logEl.textContent = "";
    try {
      log("Fetching firmware…");
      const [binRes, datRes] = await Promise.all([fetch(binUrl), fetch(datUrl)]);
      if (!binRes.ok || !datRes.ok) throw new Error("Could not fetch firmware files.");
      const [binBuf, datBuf] = await Promise.all([
        binRes.arrayBuffer(),
        datRes.arrayBuffer(),
      ]);
      log("Select the Wio's serial port…");
      const port = await navigator.serial.requestPort();
      await flash(port, binBuf, datBuf, { log, progress });
      btn.textContent = "Flashed ✓ — flash again";
    } catch (err) {
      log("Error: " + (err && err.message ? err.message : err));
      log(
        "If the device wasn't in the bootloader, double-tap RESET (the TRACKER L1 drive appears) and try again."
      );
      btn.textContent = "Retry";
    } finally {
      btn.disabled = false;
    }
  });
}

for (const el of document.querySelectorAll("[data-nrf-dfu]")) wire(el);
