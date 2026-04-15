#pragma once

namespace Pins {
  // Bench-proven Nano Every wiring.
  constexpr int kLedData = 6;  // D6

  // I2C uses the board's fixed hardware pins on Nano Every: A4=SDA, A5=SCL.
  // Keep strip power external at 5V; do not source strip current through Nano USB.
  // Strip and Nano must share a common ground reference.
}
