// test-listener.js
// Joins room-1 as a passive observer and logs every event the server relays.
// Use this in a second terminal while test-pulse.js is running.
//
// Run:
//   node test-listener.js                 # default bearId "listener"
//   node test-listener.js bearB

const { io } = require("socket.io-client");

const bearId = process.argv[2] || "listener";
const roomId = "room-1";
const SERVER_URL =
  process.env.SERVER_URL ||
  "https://comfort-buddies-dd5cf7328898.herokuapp.com";

const socket = io(SERVER_URL, { transports: ["websocket"] });

const stamp = () => new Date().toLocaleTimeString();

socket.on("connect", () => {
  console.log(`[${stamp()}] [${bearId}] connected as ${socket.id}`);
  console.log(`[${stamp()}] [${bearId}] joining room "${roomId}"...`);
  socket.emit("join_room", { roomId, bearId });
});

socket.on("connect_error", (err) => {
  console.error(`[${stamp()}] [${bearId}] connect_error:`, err.message);
});

socket.on("disconnect", (reason) => {
  console.log(`[${stamp()}] [${bearId}] disconnected: ${reason}`);
});

// Log every relayed event from the server
socket.on("heartbeat", (msg) => {
  // We're reusing the heartbeat event to carry a color name in `bpm`.
  console.log(`[${stamp()}] COLOR      ${String(msg.bpm).padEnd(6)}  from=${msg.bearId}`);
});

socket.on("hug", (msg) => {
  console.log(`[${stamp()}] HUG        value=${msg.value}  from=${msg.bearId}`);
});

socket.on("bear_status", (msg) => {
  console.log(
    `[${stamp()}] STATUS     ${msg.bearId} → ${msg.status}`
  );
});

// Catch-all for anything else (works on socket.io v4 client)
socket.onAny((event, ...args) => {
  if (!["heartbeat", "hug", "bear_status"].includes(event)) {
    console.log(`[${stamp()}] (other)    ${event}`, ...args);
  }
});
