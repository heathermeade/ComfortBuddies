# AGENTS.md

Notes for AI coding agents working in this repo. Read this **before** modifying anything.

## Hard constraints

- **Do not modify `server.js`.** The Heroku deployment is fixed and cannot be redeployed in this workflow. Treat the server as a read-only black box that only knows three events: `join_room`, `heartbeat`, `hug`.
- **Do not modify `serial-child.js` or `serial-parent.js`** (the Node serial bridge files) without explicit permission from the user. These files are considered stable. Ask before making any changes.
- **The server forwards a fixed payload shape only.** Extra fields you add to a payload are silently dropped:
  - `heartbeat` → only forwards `msg.bpm`
  - `hug` → only forwards `msg.value`
  - There is **no** `color` event. Do not invent one.
- The deployed server URL is `https://comfort-buddies-dd5cf7328898.herokuapp.com`. The other URL (`comfort-buddies.herokuapp.com`) shown in `serial-bear.js` is broken — do not "fix" it without asking.

## System overview

Three bears in one shared Socket.IO room (`room-1`):

| Bear | Hardware | Sends | Receives & acts on |
|---|---|---|---|
| 2 × **child** | Pulse sensor + vibration motor | `heartbeat { bpm: "<color>" }` (color name derived from avg BPM) | `hug` → starts/stops purr motor |
| 1 × **parent** | FSR + LED (sketch TBD) | `hug { value: 1\|0 }` from FSR press/release | `heartbeat` → drive LED color |

Because the server only forwards `msg.bpm`, the **color name is smuggled through the `bpm` field as a string** (e.g. `"red"`, `"blue"`, `"purple"`). This is intentional and is the only way to add new data without changing the server.

The server's `socket.to(roomId).emit(...)` already excludes the sender, so a bear cannot trigger itself.

## Files

| File | Purpose |
|---|---|
| [server.js](server.js) | Socket.IO relay deployed to Heroku. **Do not edit.** |
| [serial-bear.js](serial-bear.js) | Node bridge: Arduino serial ⇄ Socket.IO. One process per Arduino. |
| [ChildComfortBuddy.ino](ChildComfortBuddy.ino) | Combined pulse-sensor + purr sketch for child bears. Baud `115200`. Replaces the original `PulseSensor.ino` + `Purr.ino` (removed). |
| [test-pulse.js](test-pulse.js) | Stand-in for a child Arduino. Cycles red → blue → purple, sends them as `heartbeat { bpm: "<color>" }`. |
| [test-listener.js](test-listener.js) | Passive observer. Joins `room-1` and prints every relayed event. |
| [fakeBear.js](fakeBear.js) | Older local-server test client (sends numeric BPM). Not used in current flow. |

## Serial protocol (Arduino ↔ bridge)

Lines are newline-terminated, baud `115200`.

| Direction | Line | Meaning |
|---|---|---|
| Arduino → bridge | `BPM:<color>` | Average heart rate window completed; color is `red`/`blue`/`purple` |
| Arduino → bridge | `HUG:1` / `HUG:0` | (Parent only) FSR pressed / released |
| Bridge → Arduino | `HUG:1` / `HUG:0` | (Child only) Start / stop purr motor |
| Bridge → Arduino | `COLOR:<name>` | (Parent only, future) Set LED color from a child's heartbeat |

The bridge currently only writes `HUG:` lines back to the Arduino. `COLOR:` is reserved for the parent sketch when written.

## BPM → color mapping (in `ChildComfortBuddy.ino`)

```
avgBPM <  70  → "blue"   (calm)
avgBPM <  90  → "purple" (normal)
avgBPM >= 90  → "red"    (elevated)
```

## Running

### Real hardware

```bash
# Per child laptop, find the serial port:
ls /dev/cu.*

# Then start the bridge for each child bear:
node serial-bear.js childA /dev/cu.usbmodemAAAA
node serial-bear.js childB /dev/cu.usbmodemBBBB

# Parent bear (when sketch exists):
node serial-bear.js parent /dev/cu.usbmodemPPPP
```

The first arg is the `bearId` (any string). The second is the serial device path. Close the Arduino IDE Serial Monitor before launching, or the port will be busy.

### Test scripts (no hardware required)

Two terminals on the same machine:

```bash
# Terminal 1 — passive listener that prints everything the server relays
node test-listener.js bearB

# Terminal 2 — stand-in pulse sensor that cycles colors every 2s
node test-pulse.js bearA 2000
```

Expected listener output:
```
[8:30:12 PM] [bearB] connected as ...
[8:30:12 PM] [bearB] joining room "room-1"...
[8:30:14 PM] STATUS     bearA → online
[8:30:16 PM] COLOR      red     from=bearA
[8:30:18 PM] COLOR      blue    from=bearA
[8:30:20 PM] COLOR      purple  from=bearA
```

Override server URL for local testing:
```bash
SERVER_URL=http://localhost:3000 node test-pulse.js bearA 2000
```

To run the server locally: `node server.js` (listens on port 3000).

## Common gotchas

- **Stale clients.** If you see events tagged `from=bearA` arriving at `bearA`'s own listener, another process is connected to `room-1` impersonating that id. Find and kill it: `ps aux | grep -E "fakeBear|test-pulse|serial-bear" | grep -v grep`.
- **Wrong server URL.** `comfort-buddies.herokuapp.com` returns websocket errors. The correct URL is `comfort-buddies-dd5cf7328898.herokuapp.com`.
- **Baud mismatch.** All Arduino sketches must use `Serial.begin(115200)`. The original `PulseSensor.ino` used 9600 — `ChildComfortBuddy.ino` corrects this.
- **Don't add `socket.emit("color", …)`.** The server has no handler. Use `heartbeat { bpm: "<color>" }`.
- **Arduino IDE folder requirement.** To open `ChildComfortBuddy.ino` from the IDE, copy/move it into a folder of the same name (`ChildComfortBuddy/ChildComfortBuddy.ino`).
