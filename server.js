const express = require("express");
const http = require("http");
const { Server } = require("socket.io");

const app = express();
const server = http.createServer(app);

const io = new Server(server, {
  cors: { origin: "*" }
});

app.get("/", (_req, res) => {
  res.send("Bear server is running");
});

io.on("connection", (socket) => {
  console.log("connected:", socket.id);

  socket.on("join_room", ({ roomId, bearId }) => {
    socket.join(roomId);
    socket.data.roomId = roomId;
    socket.data.bearId = bearId;
    console.log(`${bearId} joined ${roomId}`);

    socket.to(roomId).emit("bear_status", {
      type: "bear_status",
      bearId,
      status: "online",
      ts: Date.now()
    });
  });

  socket.on("heartbeat", (msg) => {
    const roomId = socket.data.roomId;
    if (!roomId) return;

    socket.to(roomId).emit("heartbeat", {
      type: "heartbeat",
      bearId: socket.data.bearId,
      bpm: msg.bpm,
      ts: Date.now()
    });
  });

  socket.on("hug", (msg) => {
    const roomId = socket.data.roomId;
    if (!roomId) return;

    socket.to(roomId).emit("hug", {
      type: "hug",
      bearId: socket.data.bearId,
      value: msg.value ?? 1,
      ts: Date.now()
    });
  });

  socket.on("disconnect", () => {
    const roomId = socket.data.roomId;
    const bearId = socket.data.bearId;
    if (roomId && bearId) {
      socket.to(roomId).emit("bear_status", {
        type: "bear_status",
        bearId,
        status: "offline",
        ts: Date.now()
      });
    }
  });
});

// heartbeat ping at app level if you want extra visibility
setInterval(() => {
  io.emit("server_ping", { ts: Date.now() });
}, 25000);

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`listening on ${PORT}`);
});