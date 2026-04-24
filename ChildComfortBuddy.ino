/*
 * ChildComfortBuddy.ino — combined Pulse Sensor + Purr motor sketch
 *
 * Behavior:
 *   - Reads heart rate from a PulseSensor on A0
 *   - When a 10-second recording window completes, prints:
 *         BPM:<avg>\n        (machine-readable line for the Node bridge)
 *         Average BPM: <n>   (human-readable)
 *   - Listens on serial for one-line commands:
 *         HUG:1\n   → start purring (vibration motor on PIN 9)
 *         HUG:0\n   → stop purring
 *
 * Serial baud: 115200 (matches serial-bear.js)
 *
 * The purr is non-blocking: a 25 Hz on/off flutter modulated by a 3-second
 * breathing envelope. When inactive, the motor is held at 0.
 */

#define USE_ARDUINO_INTERRUPTS false
#include <PulseSensorPlayground.h>

// ════════════════════════════════════════════════════════════════════════════
// Pins
// ════════════════════════════════════════════════════════════════════════════
const int HEART_PIN = A0;
const int MOTOR_PIN = 9;     // PWM-capable

// TODO: wire up RGB LED and uncomment
// const int LED_PIN_R = 5;
// const int LED_PIN_G = 6;
// const int LED_PIN_B = 3;

// ════════════════════════════════════════════════════════════════════════════
// Pulse sensor config
// ════════════════════════════════════════════════════════════════════════════
const int   THRESHOLD              = 600;
const float MIN_BPM                = 40.0f;
const float MAX_BPM                = 180.0f;
const int   BEATS_TO_CONFIRM       = 3;
const unsigned long TIMEOUT_MS         = 4000;
const unsigned long STATUS_PRINT_MS    = 2000;
const float MAX_INTERVAL_VARIANCE      = 0.40f;
const unsigned long RECORDING_DURATION_MS = 10000;

enum SensorState { NO_SIGNAL, DETECTING, RECORDING };
SensorState state = NO_SIGNAL;

unsigned long lastBeatTime      = 0;
unsigned long lastValidBeatTime = 0;
unsigned long beatIntervals[BEATS_TO_CONFIRM];
int           validBeatCount    = 0;
unsigned long lastPrintTime     = 0;

unsigned long recordingStartTime = 0;
long          recordingBpmSum    = 0;
int           recordingBpmCount  = 0;

PulseSensorPlayground pulseSensor;

// ════════════════════════════════════════════════════════════════════════════
// Purr config
// ════════════════════════════════════════════════════════════════════════════
const int           PURR_ON_MS          = 22;
const int           PURR_OFF_MS         = 18;
const int           PURR_JITTER         = 3;
const unsigned long BREATH_PERIOD_MS    = 3000;
const unsigned long EXHALE_DURATION_MS  = 1800;
const int           EXHALE_PEAK_PWM     = 215;
const int           INHALE_PEAK_PWM     = 165;
const int           PURR_REST_PWM       = 25;
const unsigned long BLEND_MS            = 100;

// Non-blocking fade-in when purr starts
const unsigned long FADE_DURATION_MS    = 1500;

// ── Purr runtime state ──────────────────────────────────────────────────────
bool          purrActive    = false;
unsigned long purrStartTime = 0;     // when this purr session began (for fade)
unsigned long purrPhaseStart = 0;
bool          purrOnPhase   = true;
int           currentOnMs   = PURR_ON_MS;
int           currentOffMs  = PURR_OFF_MS;

// ════════════════════════════════════════════════════════════════════════════
// Serial command buffer
// ════════════════════════════════════════════════════════════════════════════
const int SERIAL_BUF_LEN = 32;
char  serialBuf[SERIAL_BUF_LEN];
int   serialBufIdx = 0;

// ════════════════════════════════════════════════════════════════════════════
// LED helpers (no-ops until pins are wired)
// ════════════════════════════════════════════════════════════════════════════
void setLedOff()   {}
void setLedBlue()  {}
void setLedRed()   {}
void setLedGreen() {}

// ════════════════════════════════════════════════════════════════════════════
// Pulse sensor helpers
// ════════════════════════════════════════════════════════════════════════════
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
    setLedRed();
  }
  validBeatCount     = 0;
  lastBeatTime       = 0;
  lastValidBeatTime  = 0;
  recordingStartTime = 0;
  recordingBpmSum    = 0;
  recordingBpmCount  = 0;
  state              = NO_SIGNAL;
}

// ════════════════════════════════════════════════════════════════════════════
// Purr control
// ════════════════════════════════════════════════════════════════════════════
void startPurr() {
  if (purrActive) return;
  purrActive     = true;
  purrStartTime  = millis();
  purrPhaseStart = purrStartTime;
  purrOnPhase    = true;
  currentOnMs    = PURR_ON_MS  + random(-PURR_JITTER, PURR_JITTER);
  currentOffMs   = PURR_OFF_MS + random(-PURR_JITTER, PURR_JITTER);
  Serial.println("PURR:on");
}

void stopPurr() {
  if (!purrActive) return;
  purrActive = false;
  analogWrite(MOTOR_PIN, 0);
  Serial.println("PURR:off");
}

// Called every loop iteration; writes the appropriate PWM if purrActive.
void updatePurr() {
  if (!purrActive) return;
  unsigned long now = millis();

  // ── Fade-in envelope (multiplier 0.0 → 1.0 over FADE_DURATION_MS) ────────
  unsigned long sinceStart = now - purrStartTime;
  float fadeMul = (sinceStart >= FADE_DURATION_MS)
                    ? 1.0f
                    : (float)sinceStart / (float)FADE_DURATION_MS;

  // ── Breathing envelope ──────────────────────────────────────────────────
  unsigned long breathPhase = now % BREATH_PERIOD_MS;
  bool isExhale = (breathPhase < EXHALE_DURATION_MS);
  int  peakPWM  = isExhale ? EXHALE_PEAK_PWM : INHALE_PEAK_PWM;

  // Smooth crossover blends
  if (breathPhase < BLEND_MS) {
    peakPWM = map(breathPhase, 0, BLEND_MS, INHALE_PEAK_PWM, EXHALE_PEAK_PWM);
  } else if (breathPhase >= EXHALE_DURATION_MS - BLEND_MS &&
             breathPhase <  EXHALE_DURATION_MS) {
    peakPWM = map(breathPhase,
                  EXHALE_DURATION_MS - BLEND_MS, EXHALE_DURATION_MS,
                  EXHALE_PEAK_PWM, INHALE_PEAK_PWM);
  }

  // ── 25 Hz purr flutter ──────────────────────────────────────────────────
  unsigned long phaseElapsed = now - purrPhaseStart;
  if (purrOnPhase && phaseElapsed >= (unsigned long)currentOnMs) {
    purrOnPhase    = false;
    purrPhaseStart = now;
    currentOffMs   = max(PURR_OFF_MS + random(-PURR_JITTER, PURR_JITTER), 5);
  } else if (!purrOnPhase && phaseElapsed >= (unsigned long)currentOffMs) {
    purrOnPhase    = true;
    purrPhaseStart = now;
    currentOnMs    = max(PURR_ON_MS + random(-PURR_JITTER, PURR_JITTER), 5);
  }

  int basePWM = purrOnPhase ? peakPWM : PURR_REST_PWM;
  int motorPWM = (int)(basePWM * fadeMul);
  analogWrite(MOTOR_PIN, motorPWM);
}

// ════════════════════════════════════════════════════════════════════════════
// Serial command parser  (HUG:1 / HUG:0)
// ════════════════════════════════════════════════════════════════════════════
void handleSerialLine(const char* line) {
  if (strcmp(line, "HUG:1") == 0) {
    startPurr();
  } else if (strcmp(line, "HUG:0") == 0) {
    stopPurr();
  }
  // (ignore anything else)
}

void readSerial() {
  while (Serial.available() > 0) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (serialBufIdx > 0) {
        serialBuf[serialBufIdx] = '\0';
        handleSerialLine(serialBuf);
        serialBufIdx = 0;
      }
    } else if (serialBufIdx < SERIAL_BUF_LEN - 1) {
      serialBuf[serialBufIdx++] = ch;
    } else {
      // overflow -- discard
      serialBufIdx = 0;
    }
  }
}

// ════════════════════════════════════════════════════════════════════════════
// Setup
// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(MOTOR_PIN, OUTPUT);
  analogWrite(MOTOR_PIN, 0);

  // pinMode(LED_PIN_R, OUTPUT);
  // pinMode(LED_PIN_G, OUTPUT);
  // pinMode(LED_PIN_B, OUTPUT);
  setLedOff();

  pulseSensor.analogInput(HEART_PIN);
  pulseSensor.setThreshold(THRESHOLD);
  if (!pulseSensor.begin()) {
    Serial.println("Pulse sensor failed to start");
    while (true) {}
  }
}

// ════════════════════════════════════════════════════════════════════════════
// Main loop
// ════════════════════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // Always service the purr motor and serial first so they stay responsive.
  readSerial();
  updatePurr();

  // ── Recording window complete ────────────────────────────────────────────
  if (state == RECORDING && (now - recordingStartTime >= RECORDING_DURATION_MS)) {
    Serial.println("Recording over");
    if (recordingBpmCount > 0) {
      int avgBPM = (int)round((float)recordingBpmSum / recordingBpmCount);

      // Map BPM → color (server only forwards a single string field, so we
      // send the color directly as the "BPM" payload).
      const char* color;
      if (avgBPM < 70)       color = "blue";    // calm
      else if (avgBPM < 90)  color = "purple";  // normal
      else                   color = "red";     // elevated

      // Machine-readable line for serial-bear.js to forward to the server
      Serial.print("BPM:");
      Serial.println(color);

      // Human-readable
      Serial.print("Average BPM: ");
      Serial.print(avgBPM);
      Serial.print("  -> color: ");
      Serial.println(color);
    } else {
      Serial.println("(no BPM samples collected)");
    }
    Serial.println("---");
    setLedGreen();

    resetToNoSignal(false);
    lastPrintTime = now;
    return;
  }

  // ── Library beat detection ──────────────────────────────────────────────
  if (pulseSensor.sawStartOfBeat()) {
    int libraryBPM = pulseSensor.getBeatsPerMinute();

    if (libraryBPM >= (int)MIN_BPM && libraryBPM <= (int)MAX_BPM) {
      if (lastBeatTime > 0) {
        unsigned long interval = now - lastBeatTime;

        if (isValidInterval(interval)) {
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
              state              = RECORDING;
              recordingStartTime = now;
              recordingBpmSum    = 0;
              recordingBpmCount  = 0;
              Serial.println("Recording started");
              setLedBlue();
            }
            if (state == RECORDING) {
              Serial.print("BPM_SAMPLE:");
              Serial.println(libraryBPM);
              recordingBpmSum   += libraryBPM;
              recordingBpmCount++;
            }
          } else if (state == NO_SIGNAL) {
            state = DETECTING;
          }
        } else {
          validBeatCount = 0;
        }
      }
      lastBeatTime = now;
    } else {
      validBeatCount = 0;
    }
  }

  // ── Timeout: no valid beat for too long ─────────────────────────────────
  if (lastValidBeatTime > 0 && (now - lastValidBeatTime > TIMEOUT_MS)) {
    resetToNoSignal(true);
    lastPrintTime = now;
  }
  if (lastBeatTime > 0 && (now - lastBeatTime > TIMEOUT_MS)) {
    lastBeatTime = 0;
  }

  // ── Periodic "no signal" message ────────────────────────────────────────
  if (state == NO_SIGNAL && (now - lastPrintTime >= STATUS_PRINT_MS)) {
    Serial.println("No reliable heart rate detected");
    lastPrintTime = now;
  }

  // No delay() — purr timing must stay tight.
}
