/*
 * Purr.ino — Vibration motor cat-purr simulator
 *
 * Based on acoustic research (Eklund, Peters et al.):
 *  - Domestic cat fundamental purr frequency: 20–30 Hz (~25 Hz average)
 *  - Egressive (exhale) phase: longer, slightly louder, ~22 Hz
 *  - Ingressive (inhale) phase: shorter, slightly quieter, ~23 Hz
 *  - Resting breath rate: ~20 breaths/min = 3000ms cycle
 *
 * How it works:
 *  1. A 25 Hz on/off pulse creates the motor flutter that mimics the laryngeal
 *     glottis opening and closing (the actual purr mechanism).
 *  2. A slow 3-second breathing envelope modulates the peak motor power,
 *     making exhale louder and longer, inhale quieter and shorter.
 *  3. Small random jitter on the purr period keeps it feeling organic.
 *  4. Fades in on startup so the motor doesn't slam on.
 *
 * Wiring:
 *  - Vibration motor + → MOTOR_PIN (PWM) via NPN transistor or motor driver
 *  - Vibration motor − → GND
 *  - Use a flyback diode across the motor terminals if it's a DC motor
 */

// ── Pin config ────────────────────────────────────────────────────────────────
const int MOTOR_PIN = 9;  // Must be a PWM-capable pin

// ── Purr pulse (25 Hz flutter) ────────────────────────────────────────────────
// Full cycle = 40ms (1000ms / 25Hz)
// ON portion slightly longer than OFF, matching egressive-dominant real purrs.
const int PURR_ON_MS  = 22;  // motor active per purr cycle
const int PURR_OFF_MS = 18;  // motor resting per purr cycle
// Max random jitter added to each phase (ms) — keeps it from feeling mechanical
const int PURR_JITTER = 3;

// ── Breathing envelope ────────────────────────────────────────────────────────
// Exhale (egressive): 60% of breath cycle, higher motor power
// Inhale (ingressive): 40% of breath cycle, lower motor power
const unsigned long BREATH_PERIOD_MS   = 3000;  // full breath cycle
const unsigned long EXHALE_DURATION_MS = 1800;  // 60% exhale

// Motor PWM levels (0–255)
const int EXHALE_PEAK_PWM = 215;  // loud during exhale
const int INHALE_PEAK_PWM = 165;  // quieter during inhale
const int PURR_REST_PWM   = 25;   // very low during OFF part of purr pulse
                                   // (keeps motor barely spinning — more natural)
// ── Startup fade ──────────────────────────────────────────────────────────────
const int           FADE_START_PWM  = 0;
const int           FADE_END_PWM    = EXHALE_PEAK_PWM;
const unsigned long FADE_DURATION_MS = 1500;  // ramp up over 1.5 seconds

// ── State ─────────────────────────────────────────────────────────────────────
unsigned long purrPhaseStart = 0;
bool          purrOnPhase    = true;
int           currentOnMs    = PURR_ON_MS;
int           currentOffMs   = PURR_OFF_MS;

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  pinMode(MOTOR_PIN, OUTPUT);
  analogWrite(MOTOR_PIN, 0);

  // Fade in over FADE_DURATION_MS
  unsigned long fadeStart = millis();
  while (true) {
    unsigned long elapsed = millis() - fadeStart;
    if (elapsed >= FADE_DURATION_MS) break;
    int pwm = map(elapsed, 0, FADE_DURATION_MS, FADE_START_PWM, FADE_END_PWM);
    analogWrite(MOTOR_PIN, pwm);
    delay(10);
  }

  purrPhaseStart = millis();
  purrOnPhase    = true;
  currentOnMs    = PURR_ON_MS + random(-PURR_JITTER, PURR_JITTER);
  currentOffMs   = PURR_OFF_MS + random(-PURR_JITTER, PURR_JITTER);
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // ── Breathing envelope ──────────────────────────────────────────────────────
  unsigned long breathPhase = now % BREATH_PERIOD_MS;
  bool     isExhale  = (breathPhase < EXHALE_DURATION_MS);
  int      peakPWM   = isExhale ? EXHALE_PEAK_PWM : INHALE_PEAK_PWM;

  // Optional: smooth the exhale→inhale transition by linearly blending near
  // the crossover points (±100ms around transition) to avoid a hard click.
  const unsigned long BLEND_MS = 100;
  if (breathPhase < BLEND_MS) {
    // Just entered exhale — blend up from inhale peak
    peakPWM = map(breathPhase, 0, BLEND_MS, INHALE_PEAK_PWM, EXHALE_PEAK_PWM);
  } else if (breathPhase >= EXHALE_DURATION_MS - BLEND_MS &&
             breathPhase <  EXHALE_DURATION_MS) {
    // About to leave exhale — blend down to inhale peak
    peakPWM = map(breathPhase,
                  EXHALE_DURATION_MS - BLEND_MS, EXHALE_DURATION_MS,
                  EXHALE_PEAK_PWM, INHALE_PEAK_PWM);
  } else if (breathPhase >= EXHALE_DURATION_MS &&
             breathPhase <  EXHALE_DURATION_MS + BLEND_MS) {
    // Just entered inhale — blend up from exhale end
    peakPWM = map(breathPhase,
                  EXHALE_DURATION_MS, EXHALE_DURATION_MS + BLEND_MS,
                  INHALE_PEAK_PWM, INHALE_PEAK_PWM);  // already inhale
  }

  // ── Purr pulse ──────────────────────────────────────────────────────────────
  unsigned long phaseElapsed = now - purrPhaseStart;

  if (purrOnPhase && phaseElapsed >= (unsigned long)currentOnMs) {
    // Switch to OFF phase
    purrOnPhase    = false;
    purrPhaseStart = now;
    currentOffMs   = PURR_OFF_MS + random(-PURR_JITTER, PURR_JITTER);
    currentOffMs   = max(currentOffMs, 5);  // never less than 5ms
  } else if (!purrOnPhase && phaseElapsed >= (unsigned long)currentOffMs) {
    // Switch to ON phase
    purrOnPhase    = true;
    purrPhaseStart = now;
    currentOnMs    = PURR_ON_MS + random(-PURR_JITTER, PURR_JITTER);
    currentOnMs    = max(currentOnMs, 5);  // never less than 5ms
  }

  int motorPWM = purrOnPhase ? peakPWM : PURR_REST_PWM;
  analogWrite(MOTOR_PIN, motorPWM);

  // Tight loop — no delay() so purr timing stays accurate
}
