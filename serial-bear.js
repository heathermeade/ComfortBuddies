const { SerialPort, ReadlineParser } = require("serialport");
const { io } = require("socket.io-client");

const bearId = process.argv[2] || "bearA";
const roomId = "room-1";
const serialPath =
  process.argv[3] || "/dev/cu.usbmodemE072A1E2CFE42";

const port = new SerialPort({
  path: serialPath,
  baudRate: 115200,
});

const parser = port.pipe(new ReadlineParser({ delimiter: "\n" }));


const socket = io("https://comfort-buddies-dd5cf7328898.herokuapp.com/", {
  transports: ["websocket"],
});

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

port.on("open", () => {
  console.log(`Serial port open: ${serialPath}`);
});

port.on("error", (err) => {
  console.error("Serial error:", err.message);
});

parser.on("data", (line) => {
  const msg = line.trim();
  //console.log("Arduino says:", msg);

  if (msg === "HUG:1") {
    socket.emit("hug", { value: 1 });
    console.log(`${bearId} sent hug: 1`);
  } else if (msg === "HUG:0") {
    socket.emit("hug", { value: 0 });
    console.log(`${bearId} sent hug: 0`);
  }
});

socket.on("heartbeat", (msg) => {
  console.log(`${bearId} received heartbeat:`, msg);

  port.write("BUZZ\n", (err) => {
    if (err) {
      console.error("Error writing BUZZ to Arduino:", err.message);
    } else {
      console.log("Sent BUZZ command to Arduino");
    }
  });
});

socket.on("hug", (msg) => {
  console.log(`${bearId} received hug:`, msg);
});

socket.on("bear_status", (msg) => {
  console.log(`${bearId} received bear_status:`, msg);
});


/*
const serialPath =
  process.argv[2] || "/dev/cu.usbmodemE072A1E2CFE42";

const port = new SerialPort({
  path: serialPath,
  baudRate: 115200,
});

const parser = port.pipe(new ReadlineParser({ delimiter: "\n" }));

port.on("open", () => {
  console.log(`Serial port open: ${serialPath}`);

  setInterval(() => {
    port.write("BUZZ\n", (err) => {
      if (err) {
        console.error("Write error:", err.message);
      } else {
        console.log("Sent: BUZZ");
      }
    });
  }, 5000);
});

port.on("error", (err) => {
  console.error("Serial error:", err.message);
});

parser.on("data", (line) => {
  console.log("Arduino says:", line.trim());
});
*/