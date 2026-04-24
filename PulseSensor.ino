#define USE_ARDUINO_INTERRUPTS false  // Use polling instead of interrupts
#include <PulseSensorPlayground.h>

// ── Pin & sensor config ───────────────────────────────────────────────────────
const int HEART_PIN = A0;
const int THRESHOLD = 600;  // Tune if needed

// ── LED pin config ────────────────────────────────────────────────────────────
// TODO: set these to the pins your RGB LED is wired to
// const int LED_PIN_R = 9;
// const int LED_PIN_G = 10;
// const int LED_PIN_B = 11;

// ── Heart rate validation ─────────────────────────────────────────────────────
const float MIN_BPM = 40.0f;
const float MAX_BPM = 180.0f;

// ── Timing ───────────────────────────────────────────────────────────────────
const int           BEATS_TO_CONFIRM      = 3;      // Consecutive valid beats to start recording
const unsigned long TIMEOUT_MS            = 4000;   // Signal loss timeout (ms)
const unsigned long STATUS_PRINT_MS       = 2000;   // How often to print "no signal"
const float         MAX_INTERVAL_VARIANCE = 0.40f;  // Max beat-to-beat deviation before rejecting
const unsigned long RECORDING_DURATION_MS = 10000;  // Recording window length (ms)

// ── State machine ─────────────────────────────────────────────────────────────
enum SensorState { NO_SIGNAL, DETECTING, RECORDING };
SensorState state = NO_SIGNAL;

// ── Beat tracking ─────────────────────────────────────────────────────────────
unsigned long lastBeatTime      = 0;
unsigned long lastValidBeatTime = 0;
unsigned long beatIntervals[BEATS_TO_CONFIRM];
int           validBeatCount    = 0;
unsigned long lastPrintTime     = 0;

// ── Recording state ───────────────────────────────────────────────────────────
unsigned long recordingStartTime = 0;
long          recordingBpmSum    = 0;
int           recordingBpmCount  = 0;

PulseSensorPlayground pulseSensor;

// ══════════════════════════════════════════════════════════════════════════════
// SECTION: LED colour control
// Controls an RGB LED to give visual feedback on recording state.
// TODO: uncomment and fill in once LED pins are wired up.
// ══════════════════════════════════════════════════════════════════════════════
void setLedOff() {
  // digitalWrite(LED_PIN_R, LOW);
  // digitalWrite(LED_PIN_G, LOW);
  // digitalWrite(LED_PIN_B, LOW);
}
void setLedBlue() {   // Recording in progress
  // digitalWrite(LED_PIN_R, LOW);
  // digitalWrite(LED_PIN_G, LOW);
  // digitalWrite(LED_PIN_B, HIGH);
}
void setLedRed() {    // Recording disrupted
  // digitalWrite(LED_PIN_R, HIGH);
  // digitalWrite(LED_PIN_G, LOW);
  // digitalWrite(LED_PIN_B, LOW);
}
void setLedGreen() {  // Recording finished successfully
  // digitalWrite(LED_PIN_R, LOW);
  // digitalWrite(LED_PIN_G, HIGH);
  // digitalWrite(LED_PIN_B, LOW);
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION: Heroku WebSocket — send average BPM
// Sends a plain integer BPM value over a WebSocket connection to Heroku.
// TODO: include your WebSocket library (e.g. ArduinoWebsockets, WebSockets by
//       Links2004) and fill in your Heroku wss:// URL and auth if needed.
// ══════════════════════════════════════════════════════════════════════════════
void sendBpmToWebSocket(int avgBpm) {
  // Example using the ArduinoWebsockets library:
  // client.send(String(avgBpm));
  //
  // Make sure the WebSocket connection is open before calling this.
  // Reconnect logic should live in setup() or a separate reconnect helper.
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool isValidInterval(unsigned long ms) {
  float bpm = 60000.0f / ms;
  return bpm >= MIN_BPM && bpm <= MAX_BPM;
}

bool intervalsAreConsistent() {
  unsigned long sum = 0;
  for (int i = 0; i < BEATS_TO_CONFIRM; i++) sum += beatIntervals[i];
  float avg = sum / (float)BEATS_TO_CONFIRM;
  for (int i = 0; i < BEATS_TO_CONFIRM; i++) {
    float deviation = abs((float)beatIntervals[i] - avg) / avg;
    if (deviation > MAX_INTERVAL_VARIANCE) return false;
  }
  return true;
}

void resetToNoSignal(bool disrupted) {
  if (disrupted && state == RECORDING) {
    Serial.println("Recording disrupted -- signal lost");
    Serial.println("---");

    // ── SECTION: LED — red briefly, then off ──────────────────────────────
    setLedRed();
    delay(2000);  // Hold red for 2 seconds
    setLedOff();
  }
  validBeatCount     = 0;
  lastBeatTime       = 0;
  lastValidBeatTime  = 0;
  recordingStartTime = 0;
  recordingBpmSum    = 0;
  recordingBpmCount  = 0;
  state              = NO_SIGNAL;
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(9600);

  // ── LED setup ──────────────────────────────────────────────────────────────
  // TODO: uncomment once LED pins are defined above
  // pinMode(LED_PIN_R, OUTPUT);
  // pinMode(LED_PIN_G, OUTPUT);
  // pinMode(LED_PIN_B, OUTPUT);
  setLedOff();

  // ── WebSocket setup ────────────────────────────────────────────────────────
  // TODO: connect to WiFi, then open the WebSocket to your Heroku app
  // WiFi.begin(SSID, PASSWORD);
  // while (WiFi.status() != WL_CONNECTED) { delay(500); }
  // client.connect("wss://your-app.herokuapp.com/ws");

  pulseSensor.analogInput(HEART_PIN);
  pulseSensor.setThreshold(THRESHOLD);
  if (!pulseSensor.begin()) {
    Serial.println("Pulse sensor failed to start");
    while (true) {}
  }
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // ── Recording window complete ─────────────────────────────────────────────
  if (state == RECORDING && (now - recordingStartTime >= RECORDING_DURATION_MS)) {
    Serial.println("Recording over");
    if (recordingBpmCount > 0) {
      int avgBPM = (int)round((float)recordingBpmSum / recordingBpmCount);
      Serial.print("Average BPM: ");
      Serial.println(avgBPM);

      // ── SECTION: send result to Heroku WebSocket ───────────────────────────
      sendBpmToWebSocket(avgBPM);

    } else {
      Serial.println("(no BPM samples collected)");
    }
    Serial.println("---");

    // ── SECTION: LED — green briefly, then off ─────────────────────────────
    setLedGreen();
    delay(2000);  // Hold green for 2 seconds so the user sees it
    setLedOff();

    resetToNoSignal(false);
    lastPrintTime = now;
    delay(20);
    return;
  }

  // ── Library beat detection ────────────────────────────────────────────────
  if (pulseSensor.sawStartOfBeat()) {
    int libraryBPM = pulseSensor.getBeatsPerMinute();

    if (libraryBPM >= (int)MIN_BPM && libraryBPM <= (int)MAX_BPM) {
      if (lastBeatTime > 0) {
        unsigned long interval = now - lastBeatTime;

        if (isValidInterval(interval)) {
          // Add to rolling interval window
          if (validBeatCount < BEATS_TO_CONFIRM) {
            beatIntervals[validBeatCount++] = interval;
          } else {
            for (int i = 0; i < BEATS_TO_CONFIRM - 1; i++) {
              beatIntervals[i] = beatIntervals[i + 1];
            }
            beatIntervals[BEATS_TO_CONFIRM - 1] = interval;
          }
          lastValidBeatTime = now;

          if (validBeatCount >= BEATS_TO_CONFIRM && intervalsAreConsistent()) {
            if (state != RECORDING) {
              // First confirmed reliable heart rate -- start recording
              state              = RECORDING;
              recordingStartTime = now;
              recordingBpmSum    = 0;
              recordingBpmCount  = 0;
              Serial.println("Recording started");

              // ── SECTION: LED — blue for duration of recording ────────────
              setLedBlue();
            }
            // Accumulate and print every BPM during the recording window
            if (state == RECORDING) {
              Serial.print("BPM: ");
              Serial.println(libraryBPM);
              recordingBpmSum   += libraryBPM;
              recordingBpmCount++;
            }

          } else if (state == NO_SIGNAL) {
            state = DETECTING;  // Silent -- accumulating toward confirmation
          }

        } else {
          // Interval out of range -- discard and reset accumulator
          validBeatCount = 0;
        }
      }
      lastBeatTime = now;

    } else {
      // Library BPM out of range -- discard
      validBeatCount = 0;
    }
  }

  // ── Timeout: no valid beat for too long ───────────────────────────────────
  if (lastValidBeatTime > 0 && (now - lastValidBeatTime > TIMEOUT_MS)) {
    resetToNoSignal(true);
    lastPrintTime = now;
  }
  if (lastBeatTime > 0 && (now - lastBeatTime > TIMEOUT_MS)) {
    lastBeatTime = 0;
  }

  // ── Periodic "no signal" message ──────────────────────────────────────────
  if (state == NO_SIGNAL && (now - lastPrintTime >= STATUS_PRINT_MS)) {
    Serial.println("No reliable heart rate detected");
    lastPrintTime = now;
  }

  delay(20);
}
