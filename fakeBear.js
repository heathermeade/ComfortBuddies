const { io } = require("socket.io-client");

const bearId = process.argv[2] || "bearA";
const roomId = "room-1";

const socket = io("https://comfort-buddies-dd5cf7328898.herokuapp.com/", {
  transports: ["websocket"]
});

const colors = ["blue", "red", "purple"];
let colorIndex = 0;

socket.on("connect", () => {
  console.log(`${bearId} connected with socket id ${socket.id}`);

  socket.emit("join_room", { roomId, bearId });

  // Send immediately on connect
  socket.emit("heartbeat", { bpm: colors[colorIndex] });
  console.log(`${bearId} sent heartbeat immediately: ${colors[colorIndex]}`);
  colorIndex = (colorIndex + 1) % colors.length;

  // Alternate through colors every 15 seconds
  setInterval(() => {
    const color = colors[colorIndex];
    socket.emit("heartbeat", { bpm: color });
    console.log(`${bearId} sent heartbeat: ${color}`);
    colorIndex = (colorIndex + 1) % colors.length;
  }, 15000);
});

socket.on("bear_status", (msg) => {
  console.log(`${bearId} received bear_status:`, msg);
});

socket.on("heartbeat", (msg) => {
  console.log(`${bearId} received heartbeat:`, msg);
});

socket.on("hug", (msg) => {
  console.log(`${bearId} received hug:`, msg);
});

socket.on("disconnect", () => {
  console.log(`${bearId} disconnected`);
});