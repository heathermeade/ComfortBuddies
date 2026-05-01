/*
Running parent code:
node serial-parent.js parentB /dev/cu.yourPortHere
*/

const { SerialPort, ReadlineParser } = require("serialport");
const { io } = require("socket.io-client");
 
const bearId = process.argv[2] || "parentB";
const roomId = "room-1";
const serialPath = process.argv[3] || "/dev/cu.usbmodemE072A1E2CFE42";
 
const port = new SerialPort({
  path: serialPath,
  baudRate: 115200,
});
 
const parser = port.pipe(new ReadlineParser({ delimiter: "\n" }));
 
const socket = io("https://comfort-buddies-dd5cf7328898.herokuapp.com/", {
  transports: ["websocket"],
});

// ── State tracking ────────────────────────────────────────────────────────────
// Track the last known color for each child bear
// slot 1 = first child to connect, slot 2 = second child
const childColors = {
  child1: null,
  child2: null,
};

// Map bearId → slot (assigned dynamically when first heartbeat arrives)
const bearSlots = {};
 
function getSlot(incomingBearId) {
  if (bearSlots[incomingBearId]) return bearSlots[incomingBearId];
 
  // Assign to next available slot
  if (!Object.values(bearSlots).includes("child1")) {
    bearSlots[incomingBearId] = "child1";
    console.log(`Assigned ${incomingBearId} to slot child1 (left half)`);
  } else if (!Object.values(bearSlots).includes("child2")) {
    bearSlots[incomingBearId] = "child2";
    console.log(`Assigned ${incomingBearId} to slot child2 (right half)`);
  }
 
  return bearSlots[incomingBearId];
}
 
function sendRingUpdate() {
  // Build command: COLOR:child1:<color>:child2:<color>
  // "none" means that half stays off
  const c1 = childColors.child1 || "none";
  const c2 = childColors.child2 || "none";
  const cmd = `COLOR:${c1}:${c2}\n`;
 
  port.write(cmd, (err) => {
    if (err) console.error("Error writing COLOR:", err.message);
    else console.log(`Sent ring update → child1: ${c1}, child2: ${c2}`);
  });
}

// Clear a child's color after 30 seconds of no heartbeat
const heartbeatTimers = {};
 
function resetChildAfterTimeout(slot) {
  if (heartbeatTimers[slot]) clearTimeout(heartbeatTimers[slot]);
 
  heartbeatTimers[slot] = setTimeout(() => {
    console.log(`${slot} heartbeat timed out — turning off that half`);
    childColors[slot] = null;
    sendRingUpdate();
  }, 30000); // 30 seconds — adjust to match your fakebear.js interval
}
 
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
// Parent bear sends hugs via the pressure sensor
 
parser.on("data", (line) => {
  const msg = line.trim();
  console.log("Arduino says:", msg);
 
  if (msg === "HUG:1") {
    socket.emit("hug", { value: 1 });
    console.log(`${bearId} sent hug: 1`);
  } else if (msg === "HUG:0") {
    socket.emit("hug", { value: 0 });
    console.log(`${bearId} sent hug: 0`);
  }
});
 
// ── Server → Arduino ──────────────────────────────────────────────────────────
// Parent bear receives heartbeat from child → vibrate + glow in received color
 
socket.on("heartbeat", (msg) => {
  console.log(`${bearId} received heartbeat:`, msg);
 
  // Vibrate motor
  port.write("BUZZ\n", (err) => {
    if (err) {
      console.error("Error writing BUZZ to Arduino:", err.message);
    } else {
      console.log("Sent BUZZ command to Arduino");
    }
  });

  // Figure out which slot this child belongs to
  const slot = getSlot(msg.bearId);
  if (!slot) {
    console.log("No slot available for", msg.bearId);
    return;
  }
 
  // Update that child's color and refresh the ring
  childColors[slot] = msg.bpm;
  sendRingUpdate();
 
  // Reset this child's color after timeout (no new heartbeat received)
  resetChildAfterTimeout(slot);
});
 
socket.on("bear_status", (msg) => {
  console.log(`${bearId} received bear_status:`, msg);
});
