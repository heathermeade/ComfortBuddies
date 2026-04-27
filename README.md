# Comfort Buddies
 
## Overview
 
**Comfort Buddies** is an interactive, connected plush system designed to simulate emotional presence across distance. Each "buddy" is embedded with sensors, lights, and haptics that allow users to send simple, meaningful signals, like a hug or heartbeat, to another person in real time.
 
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
 
```
Computer A (Child Bear):
Arduino A → serial-child.js (childA) → Heroku Server
 
Computer B (Parent Bear):
Arduino B → serial-parent.js (parentB) → Heroku Server
```
 
Both devices connect to a **shared deployed Heroku server**, allowing communication across different networks.
 
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
```
 
### 2. Use Deployed Server URL
 
Both `serial-parent.js` and `serial-child.js` are already pointed at the deployed Heroku server:
 
```js
const socket = io("https://comfort-buddies-dd5cf7328898.herokuapp.com/", {
  transports: ["websocket"]
});
```
 
Make sure:
- You include `https://`
- The URL matches your deployed app exactly
- **Do not change this to `localhost`** — both bears must connect to the same Heroku server
---
 
### 3. Connect Arduinos
 
Plug each Arduino into its respective computer.
 
Find available serial ports:
 
```bash
ls /dev/cu.*
```
 
---
 
### 4. Run Each Bear
 
On the **Child Bear** computer:
 
```bash
node serial-child.js childA /dev/cu.usbmodemXXXX
```
 
On the **Parent Bear** computer:
 
```bash
node serial-parent.js parentB /dev/cu.usbmodemYYYY
```
 
---
 
### 5. Verify Communication
 
You should see logs like:
 
```text
childA connected to server as ...
parentB connected to server as ...
 
childA sent heartbeat color: red
parentB received heartbeat: { type: 'heartbeat', bearId: 'childA', bpm: 'red', ts: ... }
Sent BUZZ command to Arduino
 
parentB sent hug: 1
childA received hug: { type: 'hug', bearId: 'parentB', value: 1, ts: ... }
Sent HUG:1 to Arduino — child bear purring/glowing
```
 
---
 
## Interaction Flow
 
### Heartbeat Interaction (Child → Parent)
 
1. Child bear's heart rate sensor reads BPM
2. Arduino encodes BPM as a color (`BPM:red`, `BPM:blue`, `BPM:purple`)
3. `serial-child.js` emits `heartbeat` event with color to server
4. Server relays to parent bear
5. `serial-parent.js` receives heartbeat → sends `BUZZ` to Arduino
6. Parent bear vibrates + glows in received color *(glow coming soon)*
### Hug Interaction (Parent → Child)
 
1. Parent bear user presses FSR pressure sensor
2. Arduino sends `HUG:1`
3. `serial-parent.js` emits `hug` event to server
4. Server relays to child bear
5. `serial-child.js` receives hug → sends `HUG:1` to Arduino
6. Child bear purrs (vibration motor) + glows
---
 
## Serial Message Protocol
 
### Arduino → Node
```text
HUG:1       → pressure sensor pressed (parent bear)
HUG:0       → pressure sensor released (parent bear)
BPM:red     → high heart rate (child bear)
BPM:blue    → below resting heart rate (child bear)
BPM:purple  → resting heart rate (child bear)
```
 
### Node → Arduino
```text
BUZZ        → trigger vibration motor
HUG:1       → start purr/glow (child bear)
HUG:0       → stop purr/glow (child bear)
COLOR:red   → trigger LED color (coming soon)
```
 
---
 
## File Structure
 
```text
server.js         → Heroku server, routes events between bears
serial-parent.js    → Node bridge for parent bear
serial-child.js   → Node bridge for child bear
fakebear.js       → Testing script, simulates a bear without hardware
```
 
### What Each File Does
 
**`serial-parent.js`**
- Reads pressure sensor → sends `hug` event to server
- Receives `heartbeat` from child → vibrates + glows in received color
**`serial-child.js`**
- Reads heart rate sensor → sends `BPM:color` to server as `heartbeat`
- Receives `hug` from parent → purrs and glows
**`server.js`** *(runs on Heroku — do not run locally)*
- Routes `heartbeat` and `hug` events between bears in the same room
---
 
## Important Notes
 
- Devices can be on different networks
- Both must connect to the same Heroku server URL
- **Never change the server URL to `localhost`** in `serial-parent.js` or `serial-child.js`
- Close Arduino Serial Monitor before running Node scripts
- Ensure stable internet connection for real-time communication
---
 
## Hardware Overview
 
Each Comfort Buddy includes:
 
- Arduino Uno R4 WiFi
- Force Sensitive Resistor (FSR) — parent bear only
- Heart Rate Sensor — child bear only
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
- Mobile pairing interface
- Improved enclosure + manufacturability
---
 
## Author
 
Heather Meade  
USC Iovine and Young Academy
 
---
 
## Acknowledgments
 
Built as part of a physical computing + networked interaction exploration.