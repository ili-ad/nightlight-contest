Adafruit NeoPixel
  include: <Adafruit_NeoPixel.h>
  constructor: Adafruit_NeoPixel(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800)

DFRobot_C4001
  include: <DFRobot_C4001.h>
  class used: DFRobot_C4001_I2C
  methods used: begin(), update(), getTargetNumber(), getTargetRange(), getTargetSpeed(), getTargetEnergy()

BH1750
  include: <Wire.h>
  class used: none (direct I²C / Wire path)
  methods used: Wire.begin(), Wire.beginTransmission(), Wire.write(), Wire.endTransmission(), Wire.requestFrom(), Wire.available(), Wire.read()