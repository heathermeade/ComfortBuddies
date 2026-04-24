// test-pulse.js
// Pretends to be the pulse-sensor bear.
// - Connects to the Heroku Socket.IO server
// - Joins room-1
// - Cycles through colors (red → blue → purple) and sends each as the
//   payload of the "heartbeat" event (the only relayed event we can
//   piggyback on without changing the deployed server).
// - Logs any incoming "hug" events (this is what will trigger the purr later)
//
// Run:
//   node test-pulse.js                      # defaults: bearA, 2s interval
//   node test-pulse.js bearA 3000           # custom bearId + interval (ms)
//   SERVER_URL=http://localhost:3000 node test-pulse.js   # override server

const { io } = require("socket.io-client");

const bearId     = process.argv[2] || "bearA";
const intervalMs = parseInt(process.argv[3], 10) || 2000;
const roomId     = "room-1";
const SERVER_URL =
  process.env.SERVER_URL ||
  "https://comfort-buddies-dd5cf7328898.herokuapp.com";

const COLORS = [
  { name: "red",    r: 255, g: 0,   b: 0   },
  { name: "blue",   r: 0,   g: 0,   b: 255 },
  { name: "purple", r: 128, g: 0,   b: 128 },
];

const socket = io(SERVER_URL, { transports: ["websocket"] });

socket.on("connect", () => {
  console.log(`[${bearId}] connected as ${socket.id} → ${SERVER_URL}`);
  socket.emit("join_room", { roomId, bearId });

  let i = 0;
  setInterval(() => {
    const c = COLORS[i % COLORS.length];
    // Server only forwards the `bpm` field of the heartbeat payload, so
    // we smuggle the color name through that field.
    socket.emit("heartbeat", { bpm: c.name });
    console.log(
      `[${bearId}] sent color ${c.name.padEnd(6)} rgb(${c.r},${c.g},${c.b})`
    );
    i++;
  }, intervalMs);
});

socket.on("connect_error", (err) => {
  console.error(`[${bearId}] connect_error:`, err.message);
});

socket.on("disconnect", (reason) => {
  console.log(`[${bearId}] disconnected: ${reason}`);
});

// What the pulse-sensor bear cares about:
// when the OTHER bear sends a hug, the server relays it here.
socket.on("hug", (msg) => {
  console.log(`[${bearId}] >>> HUG received:`, msg, "  → would trigger PURR");
});

socket.on("heartbeat", (msg) => {
  console.log(`[${bearId}] (echo) heartbeat from peer:`, msg);
});


socket.on("bear_status", (msg) => {
  console.log(`[${bearId}] bear_status:`, msg);
});
