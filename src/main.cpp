#include <Arduino.h>

#define FLOW_SENSOR_PIN 22
#define VALVE_PIN 23

// Start with something plausible, then calibrate with real water
#define PULSES_PER_LITER 450.0f

volatile uint32_t pulseCount = 0;
volatile uint32_t lastPulseMicros = 0;

// Reject pulses that are "too close" (noise/ringing). Start at 300us.
static const uint32_t MIN_PULSE_US = 300;

uint32_t lastCalcMs = 0;
float flowRateLpm = 0.0f;
float totalLiters = 0.0f;
bool valveState = false;

void IRAM_ATTR pulseCounter() {
  uint32_t now = micros();
  if ((uint32_t)(now - lastPulseMicros) >= MIN_PULSE_US) {
    pulseCount++;
    lastPulseMicros = now;
  }
}

void openValve() {
  digitalWrite(VALVE_PIN, HIGH);
  valveState = true;
  Serial.println(">>> VALVE OPENED <<<");
}

void closeValve() {
  digitalWrite(VALVE_PIN, LOW);
  valveState = false;
  Serial.println(">>> VALVE CLOSED <<<");
}

void resetCounters() {
  noInterrupts();
  pulseCount = 0;
  interrupts();
  totalLiters = 0.0f;
  flowRateLpm = 0.0f;
  Serial.println("* Counters RESET *");
}

void printStatus() {
  Serial.println("\n=== SYSTEM STATUS ===");
  Serial.print("Valve: ");
  Serial.println(valveState ? "OPEN" : "CLOSED");
  Serial.print("Flow Rate: ");
  Serial.print(flowRateLpm, 2);
  Serial.println(" L/min");
  Serial.print("Total Volume: ");
  Serial.print(totalLiters, 3);
  Serial.println(" L");
  Serial.println("====================\n");
}

void setup() {
  Serial.begin(115200);

  pinMode(FLOW_SENSOR_PIN, INPUT);     // if you have external pull-up, INPUT is fine
  pinMode(VALVE_PIN, OUTPUT);
  digitalWrite(VALVE_PIN, LOW);

  // Use ONE edge consistently
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);

  lastCalcMs = millis();

  Serial.println("=================================");
  Serial.println("ESP32 Water Flow Control System");
  Serial.println("Commands: OPEN, CLOSE, RESET, STATUS");
  Serial.println("=================================");
}

void loop() {
  // Calculate once per second
  uint32_t nowMs = millis();
  if (nowMs - lastCalcMs >= 1000) {
    uint32_t pulses;

    noInterrupts();
    pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    // pulses/sec over a 1s window
    float pulsesPerSec = (float)pulses;

    // L/min = (pulses/sec) * (60 sec/min) / (pulses/L)
    flowRateLpm = (pulsesPerSec * 60.0f) / PULSES_PER_LITER;

    // liters added in 1 second = (L/min)/60
    totalLiters += flowRateLpm / 60.0f;

    Serial.print("Pulses/s: ");
    Serial.print(pulses);
    Serial.print(" | Flow: ");
    Serial.print(flowRateLpm, 2);
    Serial.print(" L/min | Total: ");
    Serial.print(totalLiters, 3);
    Serial.println(" L");

    lastCalcMs = nowMs;
  }

  // Serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "OPEN") openValve();
    else if (cmd == "CLOSE") closeValve();
    else if (cmd == "RESET") resetCounters();
    else if (cmd == "STATUS") printStatus();
    else Serial.println("Unknown command. Use: OPEN, CLOSE, RESET, STATUS");
  }
}
