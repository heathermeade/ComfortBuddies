/*
Running child code:
node serial-child.js childA /dev/cu.yourPortHere
*/

const { SerialPort, ReadlineParser } = require("serialport");
const { io } = require("socket.io-client");
 
const bearId = process.argv[2] || "childA";
const roomId = "room-1";
// Julia add your Arduino here (replace part in quotations)
const serialPath = process.argv[3] || "/dev/cu.usbmodemE072A1E2CFE42";
 
const port = new SerialPort({
  path: serialPath,
  baudRate: 115200,
});
 
const parser = port.pipe(new ReadlineParser({ delimiter: "\n" }));
 
const socket = io("https://comfort-buddies-dd5cf7328898.herokuapp.com/", {
  transports: ["websocket"],
});
 
// ── Socket connection ─────────────────────────────────────────────────────────
 
socket.on("connect", () => {
  console.log(`${bearId} connected to server as ${socket.id}`);
  socket.emit("join_room", { roomId, bearId });
});
 
socket.on("disconnect", () => {
  console.log(`${bearId} disconnected from server`);
});
 
socket.on("connect_error", (err) => {
  console.error("Socket.IO connect error:", err.message);
});
 
// ── Serial port ───────────────────────────────────────────────────────────────
 
port.on("open", () => {
  console.log(`Serial port open: ${serialPath}`);
});
 
port.on("error", (err) => {
  console.error("Serial error:", err.message);
});
 
// ── Arduino → Server ──────────────────────────────────────────────────────────
// Child bear sends heartbeat (as a color) via the heart rate sensor
 
parser.on("data", (line) => {
  const msg = line.trim();
  console.log("Arduino says:", msg);
 
  if (msg.startsWith("BPM:")) {
    // Arduino sends color name based on BPM range e.g. "BPM:red"
    const color = msg.slice(4).trim();
    if (color.length > 0) {
      socket.emit("heartbeat", { bpm: color });
      console.log(`${bearId} sent heartbeat color: ${color}`);
    }
  }
});
 
// ── Server → Arduino ──────────────────────────────────────────────────────────
// Child bear receives hug from mama → glow + purr (vibration motor)
 
socket.on("hug", (msg) => {
  console.log(`${bearId} received hug:`, msg);
 
  const cmd = msg.value ? "HUG:1\n" : "HUG:0\n";
  port.write(cmd, (err) => {
    if (err) {
      console.error(`Error writing ${cmd.trim()} to Arduino:`, err.message);
    } else {
      console.log(`Sent ${cmd.trim()} to Arduino — child bear purring/glowing`);
    }
  });
});
 
socket.on("bear_status", (msg) => {
  console.log(`${bearId} received bear_status:`, msg);
});
 