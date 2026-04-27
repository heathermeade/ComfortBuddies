const { io } = require("socket.io-client");

const bearId = process.argv[2] || "bearA";
const roomId = "room-1";

const socket = io("https://comfort-buddies-dd5cf7328898.herokuapp.com/", {
  transports: ["websocket"]
});

socket.on("connect", () => {
  console.log(`${bearId} connected with socket id ${socket.id}`);

  socket.emit("join_room", {
    roomId,
    bearId
  });

  // send fake heartbeat every 2 seconds
  setInterval(() => {
    const bpm = 72 + Math.floor(Math.random() * 10);
    socket.emit("heartbeat", { bpm });
    console.log(`${bearId} sent heartbeat: ${bpm}`);
  }, 15000);

  // send fake hug every 5 seconds
  /*
  setInterval(() => {
    const hugValue = Math.random() > 0.5 ? 1 : 0;
    socket.emit("hug", { value: hugValue });
    console.log(`${bearId} sent hug: ${hugValue}`);
  }, 5000); */
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