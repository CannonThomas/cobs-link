// JavaScript port of the wire format, used by the interactive demo.
// tests/js_check.mjs holds it to the same vectors as the C library.
(function (root) {
  function crc16(bytes) {
    let crc = 0xffff;
    for (const b of bytes) {
      crc ^= b << 8;
      for (let i = 0; i < 8; i++) {
        crc = (crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1) & 0xffff;
      }
    }
    return crc ^ 0xffff;
  }

  function cobsEncode(bytes) {
    const out = [0];
    let codeIdx = 0, code = 1;
    const close = () => { out[codeIdx] = code; codeIdx = out.length; out.push(0); code = 1; };
    for (const b of bytes) {
      if (code === 0xff) close();
      if (b === 0) close();
      else { out.push(b); code++; }
    }
    out[codeIdx] = code;
    return out;
  }

  // Returns {ok, data} or {ok: false, error}
  function cobsDecode(bytes) {
    const out = [];
    let r = 0;
    while (r < bytes.length) {
      const code = bytes[r++];
      if (code === 0) return { ok: false, error: "zero inside frame" };
      if (code - 1 > bytes.length - r) return { ok: false, error: "block runs past end of frame" };
      for (let i = 1; i < code; i++) {
        if (bytes[r] === 0) return { ok: false, error: "zero inside block" };
        out.push(bytes[r++]);
      }
      if (code !== 0xff && r < bytes.length) out.push(0);
    }
    return { ok: true, data: out };
  }

  function encodeFrame(payload) {
    const crc = crc16(payload);
    return cobsEncode([...payload, crc >> 8, crc & 0xff]).concat([0]);
  }

  // Feed a byte stream to a receiver; returns one result per delimiter seen.
  function receive(stream) {
    const results = [];
    let cur = [];
    for (const b of stream) {
      if (b !== 0) { cur.push(b); continue; }
      if (cur.length === 0) continue;
      const dec = cobsDecode(cur);
      if (!dec.ok) results.push({ status: "framing", detail: dec.error });
      else if (dec.data.length < 2) results.push({ status: "framing", detail: "frame too short" });
      else {
        const body = dec.data.slice(0, -2);
        const want = (dec.data[dec.data.length - 2] << 8) | dec.data[dec.data.length - 1];
        const got = crc16(body);
        if (got === want) results.push({ status: "ok", payload: body });
        else results.push({ status: "crc", detail: `expected ${hex16(got)}, frame says ${hex16(want)}` });
      }
      cur = [];
    }
    if (cur.length) results.push({ status: "pending", detail: `${cur.length} byte(s), no delimiter yet` });
    return results;
  }

  const hex16 = (v) => v.toString(16).toUpperCase().padStart(4, "0");

  // Indices of the COBS code bytes in an encoded frame.
  function codePositions(frame) {
    const pos = new Set();
    for (let i = 0; i < frame.length - 1 && frame[i] !== 0; i += frame[i]) pos.add(i);
    return pos;
  }

  const api = { crc16, cobsEncode, cobsDecode, encodeFrame, receive, codePositions };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.CobsLink = api;
})(typeof self !== "undefined" ? self : this);
