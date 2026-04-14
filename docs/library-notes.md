Adafruit NeoPixel
  include: <Adafruit_NeoPixel.h>
  constructor: Adafruit_NeoPixel(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800)

DFRobot_C4001
  include: <DFRobot_C4001.h>
  class used: DFRobot_C4001_I2C
  methods used in current project path: begin(), update(), getTargetNumber(), getTargetRange(), getTargetSpeed(), getTargetEnergy()
  investigation note (ANG-015): no angle/azimuth/lateral/XY/beam-index accessor is currently used or documented in this repo's active C4001 integration path.

BH1750
  include: <Wire.h>
  class used: none (direct I²C / Wire path)
  methods used: Wire.begin(), Wire.beginTransmission(), Wire.write(), Wire.endTransmission(), Wire.requestFrom(), Wire.available(), Wire.read()
