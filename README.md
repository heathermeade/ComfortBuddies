# Comfort Buddies

## Overview

**Comfort Buddies** is an interactive, connected plush system designed to simulate emotional presence across distance. Each “buddy” is embedded with sensors, lights, and haptics that allow users to send simple, meaningful signals, like a hug or heartbeat, to another person in real time.

The goal of the project is to create a **tangible, ambient communication layer** that feels more human than a notification. Instead of text or sound, Comfort Buddies communicate through:
- Light (NeoPixel glow)
- Touch (vibration motor)
- Interaction (pressure sensor / hug)
- Presence (heartbeat simulation)

This project combines:
- Arduino hardware
- Node.js networking
- Socket.IO real-time communication
- Physical computing + soft product design

---

## System Architecture
Computer A:
Arduino A → serial-bear.js (bearA) → server

Computer B:
Arduino B → serial-bear.js (bearB) → same server

---

Both devices connect to a **shared deployed server**, allowing communication across different networks.

---

## Tech Stack

- Arduino Uno R4 WiFi
- Node.js
- Express
- Socket.IO
- SerialPort (Node)
- NeoPixel (WS2812B)
- LiPo Battery + TP4056 + Boost Converter
- Heroku (server hosting)

---

## Setup Instructions

### 1. Install Dependencies

```bash
npm install
### 2. Use Deployed Server URL

Replace all local server references with your Heroku URL.

In `serial-bear.js`:

```js
const socket = io("https://your-heroku-app-name.herokuapp.com", {
  transports: ["websocket"]
});
```

Make sure:
- You include `https://`
- The URL matches your deployed app exactly

---

### 3. Connect Arduinos

Plug each Arduino into its respective computer.

Find available serial ports:

```bash
ls /dev/cu.*
```

---

### 4. Run Each Bear

On Computer A:

```bash
node serial-bear.js bearA /dev/cu.usbmodemXXXX
```

On Computer B:

```bash
node serial-bear.js bearB /dev/cu.usbmodemYYYY
```

---

### 5. Verify Communication

You should see logs like:

```text
bearA connected
bearB connected

bearA sent hug: 1
bearB received hug
```

---

## Interaction Flow

### Hug Interaction

1. User presses FSR on Bear A
2. Arduino sends `HUG:1`
3. Node emits `hug` event
4. Server relays event
5. Bear B receives event
6. Arduino B triggers motor or light

---

### Heartbeat Interaction

1. Bear A sends heartbeat every 15 seconds
2. Server relays heartbeat
3. Bear B receives it
4. Motor buzzes once

---

## Serial Message Protocol

```text
HUG:1   → pressure detected
HUG:0   → released
BUZZ    → trigger vibration
LIGHT   → trigger LED
```

---

## Node Bridge Behavior

Each `serial-bear.js` acts as a translator between:

```text
Arduino Serial ↔ Socket.IO Server
```

---

### Example: Sending Hug

```js
if (msg === "HUG:1") {
  socket.emit("hug", { value: 1 });
}
```

---

### Example: Receiving Hug

```js
socket.on("hug", (msg) => {
  if (msg.value === 1) {
    port.write("BUZZ\n");
  }
});
```

---

## Custom Behaviors

Each bear can run different logic:

- One bear vibrates
- One bear lights up
- One bear simulates heartbeat

Optional structure:

```text
serial-bear-a.js
serial-bear-b.js
```

---

## Important Notes

- Devices can be on different networks
- Both must connect to the same Heroku server URL
- Close Arduino Serial Monitor before running Node scripts
- Ensure stable internet connection for real-time communication

---

## Hardware Overview

Each Comfort Buddy includes:

- Arduino Uno R4 WiFi
- Force Sensitive Resistor (FSR)
- NeoPixel Ring (WS2812B)
- Coin Vibration Motor
- Transistor + diode (motor driver)
- LiPo battery + charging module
- Boost converter (3.7V → 5V)

---

## Project Vision

Comfort Buddies explores how physical objects can become emotionally expressive interfaces.

Rather than replacing communication, it augments it with:

- ambient presence
- physical feedback
- non-verbal signals

This creates a new category of interaction:

> subtle, continuous, and human-centered connection through objects

---

## Future Improvements

- Fully standalone WiFi (no laptop required)
- Direct Arduino → server communication (no Node bridge)
- Real heart rate sensor integration
- Mobile pairing interface
- Improved enclosure + manufacturability

---

## Author

Heather Meade  
USC Iovine and Young Academy

---

## Acknowledgments

Built as part of a physical computing + networked interaction exploration.
