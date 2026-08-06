import { writeFile } from "node:fs/promises";

const endpoint = process.argv[2] || "http://127.0.0.1:9223";
const output = process.argv[3] || "/tmp/event-horizon-smoke.png";
const aimX = Math.max(0.1, Math.min(0.9, Number(process.argv[4] || 0.50)));
const targets = await (await fetch(`${endpoint}/json/list`)).json();
const target = targets.find((item) => item.type === "page");
if (!target) throw new Error("No Chrome page target is available");

const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  socket.addEventListener("open", resolve, { once: true });
  socket.addEventListener("error", reject, { once: true });
});

let commandId = 0;
const pending = new Map();
const consoleMessages = [];
socket.addEventListener("message", ({ data }) => {
  const message = JSON.parse(data);
  if (message.id && pending.has(message.id)) {
    const { resolve, reject } = pending.get(message.id);
    pending.delete(message.id);
    if (message.error) reject(new Error(message.error.message));
    else resolve(message.result);
  } else if (message.method === "Runtime.consoleAPICalled") {
    consoleMessages.push(message.params.args.map((arg) => arg.value ?? arg.description).join(" "));
  } else if (message.method === "Runtime.exceptionThrown") {
    consoleMessages.push(`EXCEPTION: ${message.params.exceptionDetails.text}`);
  }
});

function call(method, params = {}) {
  const id = ++commandId;
  socket.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
}

const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
const evaluate = async (expression) => {
  const result = await call("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.text);
  return result.result.value;
};

await call("Page.enable");
await call("Runtime.enable");
await call("Page.navigate", { url: "http://127.0.0.1:8342/" });
await sleep(3500);

const boot = await evaluate(`({
  className: document.querySelector('#app')?.className,
  engine: document.querySelector('#engineName')?.textContent,
  status: document.querySelector('#bootStatus')?.textContent
})`);
if (!boot.className?.includes("ready")) throw new Error(`App did not become ready: ${JSON.stringify(boot)}`);
if (boot.engine !== "WEBGPU") throw new Error(`Expected WEBGPU, received ${boot.engine}`);

await evaluate(`(() => {
  document.querySelector('[data-type="star"]').click();
  const rate = document.querySelector('#timeRate');
  rate.value = '3';
  rate.dispatchEvent(new Event('input', { bubbles: true }));
  document.querySelector('#launchButton').click();
  const canvas = document.querySelector('#viewport');
  const rect = canvas.getBoundingClientRect();
  canvas.dispatchEvent(new PointerEvent('pointerdown', {
    bubbles: true,
    pointerId: 41,
    clientX: rect.left + rect.width * ${aimX.toFixed(4)},
    clientY: rect.top + rect.height * 0.50
  }));
  return true;
})()`);
await sleep(900);

const scenario = await evaluate(`({
  feed: document.querySelector('#eventFeed')?.innerText,
  time: document.querySelector('#simClock')?.textContent,
  targeting: document.querySelector('#app')?.classList.contains('aiming')
})`);
const screenshot = await call("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
await writeFile(output, Buffer.from(screenshot.data, "base64"));

console.log(JSON.stringify({ boot, scenario, consoleMessages, output }, null, 2));
socket.close();
