#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <stdio.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// ---------------- Hardware pins ----------------

const int LDR_PINS[2] = {A0, A1};
const int IR_PINS[2] = {2, 3};
const int LED_CHANNELS[2] = {0, 1};
const int BUTTON_PIN = 6;

// Most IR obstacle modules drive OUT LOW when an object is detected.
const bool IR_ACTIVE_LOW = true;

// ---------------- Calibrated lighting settings ----------------

// Wiring: 3.3 V -> LDR -> analog input -> 10 kOhm -> GND.
// With this arrangement, a lower ADC reading means darker conditions.
// Replace these values with your already-tested thresholds if they differ.
const int DARK_THRESHOLD[2] = {400, 400};
const int BRIGHT_THRESHOLD[2] = {500, 500};

const int LOCAL_DIM_BRIGHTNESS = 25;
const int FULL_BRIGHTNESS = 100;

// If commands stop, the Nano returns to safe standalone control.
const unsigned long QNX_COMMAND_TIMEOUT_MS = 2500;
const unsigned long DEBOUNCE_TIME_MS = 50;
const unsigned long TELEMETRY_INTERVAL_MS = 500;

// ---------------- Runtime state ----------------

bool zoneDark[2] = {false, false};
bool emergencyMode = false;

bool lastRawButtonState = HIGH;
bool stableButtonState = HIGH;
unsigned long lastButtonChangeMs = 0;
unsigned long lastTelemetryMs = 0;

int qnxBrightness[2] = {0, 0};
bool hasQnxCommand = false;
unsigned long lastQnxCommandMs = 0;

char commandBuffer[64];
size_t commandLength = 0;
int lastAppliedBrightness[2] = {-1, -1};

// LEDs use sink wiring:
// PCA V+ -> resistor -> LED anode; LED cathode -> PCA PWM pin.
// A LOW PCA output lights the LED, so the PWM direction is inverted.
void setLedBrightness(int channel, int percentage) {
  percentage = constrain(percentage, 0, 100);

  if (percentage == 0) {
    pwm.setPWM(channel, 4096, 0);  // Output always HIGH: LED off
    return;
  }

  if (percentage == 100) {
    pwm.setPWM(channel, 0, 4096);  // Output always LOW: LED fully on
    return;
  }

  uint16_t ledOnTicks = (uint32_t)percentage * 4095 / 100;
  uint16_t outputHighTicks = 4095 - ledOnTicks;
  pwm.setPWM(channel, 0, outputHighTicks);
}

bool readObjectSensor(int pin) {
  int rawState = digitalRead(pin);
  return IR_ACTIVE_LOW ? rawState == LOW : rawState == HIGH;
}

void updateDarkState(int zone, int ldrValue) {
  // The two thresholds add hysteresis so the state does not flicker.
  if (!zoneDark[zone] && ldrValue < DARK_THRESHOLD[zone]) {
    zoneDark[zone] = true;
  } else if (zoneDark[zone] && ldrValue > BRIGHT_THRESHOLD[zone]) {
    zoneDark[zone] = false;
  }
}

void updateEmergencyButton() {
  bool rawState = digitalRead(BUTTON_PIN);

  if (rawState != lastRawButtonState) {
    lastRawButtonState = rawState;
    lastButtonChangeMs = millis();
  }

  if (millis() - lastButtonChangeMs >= DEBOUNCE_TIME_MS &&
      rawState != stableButtonState) {
    stableButtonState = rawState;

    if (stableButtonState == LOW) {
      emergencyMode = !emergencyMode;
    }
  }
}

void acceptQnxCommand(const char *line) {
  int zone1 = 0;
  int zone2 = 0;
  char trailing = '\0';

  int fields = sscanf(line, "QNX_PWM:Z1=%d,Z2=%d%c",
                      &zone1, &zone2, &trailing);

  if (fields == 2 && zone1 >= 0 && zone1 <= 100 &&
      zone2 >= 0 && zone2 <= 100) {
    qnxBrightness[0] = zone1;
    qnxBrightness[1] = zone2;
    hasQnxCommand = true;
    lastQnxCommandMs = millis();
  }
}

void readQnxCommands() {
  while (Serial.available() > 0) {
    char value = (char)Serial.read();

    if (value == '\n') {
      commandBuffer[commandLength] = '\0';
      acceptQnxCommand(commandBuffer);
      commandLength = 0;
    } else if (value != '\r') {
      if (commandLength < sizeof(commandBuffer) - 1) {
        commandBuffer[commandLength++] = value;
      } else {
        // Discard an overlong or damaged command.
        commandLength = 0;
      }
    }
  }
}

int calculateLocalBrightness(bool dark, bool objectDetected) {
  if (!dark) {
    return 0;
  }
  return objectDetected ? FULL_BRIGHTNESS : LOCAL_DIM_BRIGHTNESS;
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(10);

  pinMode(IR_PINS[0], INPUT);
  pinMode(IR_PINS[1], INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin();
  pwm.begin();
  pwm.setPWMFreq(1000);

  setLedBrightness(LED_CHANNELS[0], 0);
  setLedBrightness(LED_CHANNELS[1], 0);

  Serial.println("LumiGrid two-zone QNX controller ready");
}

void loop() {
  readQnxCommands();
  updateEmergencyButton();

  int ldrValues[2];
  bool objectDetected[2];
  int appliedBrightness[2];

  bool qnxOnline = hasQnxCommand &&
                   millis() - lastQnxCommandMs <= QNX_COMMAND_TIMEOUT_MS;

  for (int zone = 0; zone < 2; zone++) {
    ldrValues[zone] = analogRead(LDR_PINS[zone]);
    objectDetected[zone] = readObjectSensor(IR_PINS[zone]);
    updateDarkState(zone, ldrValues[zone]);

    int localBrightness =
        calculateLocalBrightness(zoneDark[zone], objectDetected[zone]);

    // Emergency always wins immediately, even during a network failure.
    if (emergencyMode) {
      appliedBrightness[zone] = FULL_BRIGHTNESS;
    } else if (qnxOnline) {
      appliedBrightness[zone] = qnxBrightness[zone];
    } else {
      appliedBrightness[zone] = localBrightness;
    }

    if (appliedBrightness[zone] != lastAppliedBrightness[zone]) {
      setLedBrightness(LED_CHANNELS[zone], appliedBrightness[zone]);
      lastAppliedBrightness[zone] = appliedBrightness[zone];
    }
  }

  // Keep this record format exact: the QNX hardware parser consumes it.
  if (millis() - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = millis();

    Serial.print("Z1_LDR=");
    Serial.print(ldrValues[0]);
    Serial.print(",Z1_DARK=");
    Serial.print(zoneDark[0] ? 1 : 0);
    Serial.print(",Z1_OBJECT=");
    Serial.print(objectDetected[0] ? 1 : 0);
    Serial.print(",Z1_LED=");
    Serial.print(appliedBrightness[0]);

    Serial.print(",Z2_LDR=");
    Serial.print(ldrValues[1]);
    Serial.print(",Z2_DARK=");
    Serial.print(zoneDark[1] ? 1 : 0);
    Serial.print(",Z2_OBJECT=");
    Serial.print(objectDetected[1] ? 1 : 0);
    Serial.print(",Z2_LED=");
    Serial.print(appliedBrightness[1]);

    Serial.print(",EMERGENCY=");
    Serial.println(emergencyMode ? 1 : 0);
  }

  delay(20);
}
