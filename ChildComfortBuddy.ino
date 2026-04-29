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
#include <Arduino_LED_Matrix.h>
#include <Adafruit_NeoPixel.h>

// Must be declared before any function that uses it as a parameter;
// Arduino IDE inserts auto-prototypes right after the #includes.
enum MatrixAnim { ANIM_NONE, ANIM_HEARTBEAT, ANIM_CHECKMARK, ANIM_INTERRUPTED };
enum NeoAnim   { NEO_NONE, NEO_PULSE_WHITE, NEO_SWEEP_GREEN, NEO_SWEEP_RED };

// ════════════════════════════════════════════════════════════════════════════
// Pins
// ════════════════════════════════════════════════════════════════════════════
const int HEART_PIN  = A0;
const int MOTOR_PIN  = 9;    // PWM-capable
const int NEO_PIN    = 13;
const int NEO_COUNT  = 16;
const int NEO_DIM    = 40;   // brightness for white pulse (0-255)

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
void setLedRed()   {}
void setLedGreen() {}
void setLedBlue()  {}

// ════════════════════════════════════════════════════════════════════════════
// NeoPixel
// ════════════════════════════════════════════════════════════════════════════
Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

NeoAnim       neoAnim     = NEO_NONE;
int           neoStep     = 0;
unsigned long neoLastStep = 0;

void startNeoAnim(NeoAnim anim) {
  neoAnim     = anim;
  neoStep     = 0;
  neoLastStep = millis();
  strip.clear();
  strip.show();
}

void stopNeo() {
  neoAnim = NEO_NONE;
  strip.clear();
  strip.show();
}

void updateNeo() {
  if (neoAnim == NEO_NONE) return;
  unsigned long now = millis();

  if (neoAnim == NEO_PULSE_WHITE) {
    // Sine-wave pulse: ~2 s period, all LEDs white
    float t = (now % 2000) / 2000.0f;
    float brightness = (sin(t * 2.0f * 3.14159f - 1.5708f) + 1.0f) * 0.5f;
    uint8_t val = (uint8_t)(brightness * NEO_DIM);
    for (int i = 0; i < NEO_COUNT; i++)
      strip.setPixelColor(i, strip.Color(val, val, val));
    strip.show();
    return;
  }

  // Sweep animations: one LED per 50 ms
  if (now - neoLastStep < 50) return;
  neoLastStep = now;

  if (neoStep < NEO_COUNT) {
    uint32_t color = (neoAnim == NEO_SWEEP_GREEN)
                       ? strip.Color(0, NEO_DIM, 0)
                       : strip.Color(NEO_DIM, 0, 0);
    strip.setPixelColor(neoStep, color);
    strip.show();
  } else if (neoStep >= NEO_COUNT + 20) {
    stopNeo();
    return;
  }
  neoStep++;
}

// ════════════════════════════════════════════════════════════════════════════
// LED matrix
// ════════════════════════════════════════════════════════════════════════════
ArduinoLEDMatrix matrix;

static uint8_t FRAME_HEART_BIG[8][12] = {
  {0,0,1,1,0,0,0,1,1,0,0,0},
  {0,1,1,1,1,0,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,1,0,0},
  {0,1,1,1,1,1,1,1,1,1,0,0},
  {0,0,1,1,1,1,1,1,1,0,0,0},
  {0,0,0,1,1,1,1,1,0,0,0,0},
  {0,0,0,0,1,1,1,0,0,0,0,0},
  {0,0,0,0,0,1,0,0,0,0,0,0}
};
static uint8_t FRAME_HEART_SMALL[8][12] = {
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,1,0,0,1,0,0,0,0,0},
  {0,0,1,1,1,1,1,1,0,0,0,0},
  {0,0,1,1,1,1,1,1,0,0,0,0},
  {0,0,0,1,1,1,1,0,0,0,0,0},
  {0,0,0,0,1,1,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0}
};
static uint8_t FRAME_CHECK[8][12] = {
  {0,0,0,0,0,0,0,0,0,0,1,1},
  {0,0,0,0,0,0,0,0,0,1,1,0},
  {0,0,0,0,0,0,0,0,1,1,0,0},
  {0,0,0,0,0,0,0,1,1,0,0,0},
  {1,0,0,0,0,0,1,1,0,0,0,0},
  {1,1,0,0,0,1,1,0,0,0,0,0},
  {0,1,1,0,1,1,0,0,0,0,0,0},
  {0,0,1,1,1,0,0,0,0,0,0,0}
};
static uint8_t FRAME_X[8][12] = {
  {1,1,0,0,0,0,0,0,0,1,1,0},
  {0,1,1,0,0,0,0,0,1,1,0,0},
  {0,0,1,1,0,0,0,1,1,0,0,0},
  {0,0,0,1,1,0,1,1,0,0,0,0},
  {0,0,0,0,1,1,1,0,0,0,0,0},
  {0,0,0,1,1,0,1,1,0,0,0,0},
  {0,0,1,1,0,0,0,1,1,0,0,0},
  {0,1,1,0,0,0,0,0,1,1,0,0}
};
static uint8_t FRAME_BLANK[8][12] = {
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0}
};

// 3x6 mini font: 0-9 digits, 10=B, 11=P, 12=R
static const uint8_t MINI_FONT[13][6] = {
  {0b111,0b101,0b101,0b101,0b101,0b111}, // 0
  {0b010,0b110,0b010,0b010,0b010,0b111}, // 1
  {0b111,0b001,0b011,0b110,0b100,0b111}, // 2
  {0b111,0b001,0b011,0b001,0b001,0b111}, // 3
  {0b101,0b101,0b111,0b001,0b001,0b001}, // 4
  {0b111,0b100,0b111,0b001,0b001,0b111}, // 5
  {0b011,0b100,0b111,0b101,0b101,0b111}, // 6
  {0b111,0b001,0b001,0b010,0b010,0b010}, // 7
  {0b111,0b101,0b111,0b101,0b101,0b111}, // 8
  {0b111,0b101,0b111,0b001,0b001,0b111}, // 9
  {0b110,0b101,0b110,0b101,0b101,0b110}, // B
  {0b110,0b101,0b110,0b100,0b100,0b100}, // P
  {0b110,0b101,0b110,0b110,0b101,0b101}, // R
};

void drawMiniChar(uint8_t frame[8][12], int fontIdx, int colStart) {
  for (int r = 0; r < 6; r++) {
    uint8_t bits = MINI_FONT[fontIdx][r];
    frame[r+1][colStart]   = (bits >> 2) & 1;
    frame[r+1][colStart+1] = (bits >> 1) & 1;
    frame[r+1][colStart+2] =  bits       & 1;
  }
}

void showBpmColor(int bpm, const char* color) {
  uint8_t frame[8][12] = {};
  int d1 = (bpm >= 100) ? (bpm / 100)     : (bpm / 10);
  int d2 = (bpm >= 100) ? (bpm / 10) % 10 : (bpm % 10);
  int li = (color[0] == 'b') ? 10 : (color[0] == 'p') ? 11 : 12;
  drawMiniChar(frame, d1, 0);
  drawMiniChar(frame, d2, 4);
  drawMiniChar(frame, li, 8);
  matrix.renderBitmap(frame, 8, 12);
}

MatrixAnim    currentAnim  = ANIM_NONE;
unsigned long animLastStep = 0;
int           animStep     = 0;
int           pendingBpm   = 0;
const char*   pendingColor = "";

void startMatrixAnim(MatrixAnim anim) {
  currentAnim  = anim;
  animLastStep = millis();
  animStep     = 0;
  switch (anim) {
    case ANIM_HEARTBEAT:   matrix.renderBitmap(FRAME_HEART_BIG, 8, 12); break;
    case ANIM_CHECKMARK:   matrix.renderBitmap(FRAME_CHECK,     8, 12); break;
    case ANIM_INTERRUPTED: matrix.renderBitmap(FRAME_X,         8, 12); break;
    default:               matrix.renderBitmap(FRAME_BLANK,     8, 12); break;
  }
}

void updateMatrix() {
  if (currentAnim == ANIM_NONE) return;
  unsigned long now = millis();
  switch (currentAnim) {
    case ANIM_HEARTBEAT:
      if (now - animLastStep >= 500) {
        animStep     = (animStep + 1) % 2;
        animLastStep = now;
        if (animStep == 0) matrix.renderBitmap(FRAME_HEART_BIG,   8, 12);
        else               matrix.renderBitmap(FRAME_HEART_SMALL, 8, 12);
      }
      break;
    case ANIM_CHECKMARK:
      if (now - animLastStep >= 300) {
        animStep++;
        animLastStep = now;
        if (animStep >= 4) {
          showBpmColor(pendingBpm, pendingColor);
          currentAnim = ANIM_NONE;
        } else {
          if (animStep % 2 == 0) matrix.renderBitmap(FRAME_CHECK, 8, 12);
          else                   matrix.renderBitmap(FRAME_BLANK, 8, 12);
        }
      }
      break;
    case ANIM_INTERRUPTED:
      if (now - animLastStep >= 300) {
        animStep++;
        animLastStep = now;
        if (animStep >= 12) {
          matrix.renderBitmap(FRAME_BLANK, 8, 12);
          currentAnim = ANIM_NONE;
        } else {
          if (animStep % 2 == 0) matrix.renderBitmap(FRAME_X,     8, 12);
          else                   matrix.renderBitmap(FRAME_BLANK, 8, 12);
        }
      }
      break;
    default: break;
  }
}

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
    startNeoAnim(NEO_SWEEP_RED);
    startMatrixAnim(ANIM_INTERRUPTED);
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
  matrix.begin();
  strip.begin();
  strip.setBrightness(255);
  strip.clear();
  strip.show();

  pinMode(MOTOR_PIN, OUTPUT);
  analogWrite(MOTOR_PIN, 0);

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

  // Always service the purr motor, matrix, NeoPixel, and serial first so they stay responsive.
  readSerial();
  updatePurr();
  updateMatrix();
  updateNeo();

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
    startNeoAnim(NEO_SWEEP_GREEN);
    pendingBpm   = (recordingBpmCount > 0) ? (int)round((float)recordingBpmSum / recordingBpmCount) : 0;
    pendingColor = (pendingBpm < 70) ? "blue" : (pendingBpm < 90) ? "purple" : "red";
    startMatrixAnim(ANIM_CHECKMARK);

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
              startNeoAnim(NEO_PULSE_WHITE);
              startMatrixAnim(ANIM_HEARTBEAT);
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
