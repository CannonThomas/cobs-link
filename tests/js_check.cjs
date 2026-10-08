// Hold the demo's JavaScript port to the frames the Python reference produces.
//   python3 tests/js_vectors.py | node tests/js_check.cjs
const cl = require("../docs/cobs_link.js");
const lines = require("fs").readFileSync(0, "utf8").trim().split("\n");
let failures = 0;
for (const line of lines) {
  const [p, f] = line.split(" ");
  const payload = (p === "-" ? "" : p).match(/../g)?.map((h) => parseInt(h, 16)) ?? [];
  const frame = cl.encodeFrame(payload).map((b) => b.toString(16).padStart(2, "0")).join("");
  const back = cl.receive(f.match(/../g).map((h) => parseInt(h, 16)));
  const ok = frame === f && back.length === 1 && back[0].status === "ok" &&
             back[0].payload.join() === payload.join();
  if (!ok) { failures++; console.log(`mismatch for ${payload.length}-byte payload`); }
}
console.log(`js-check: ${lines.length} payloads, ${failures} failures`);
process.exit(failures ? 1 : 0);
