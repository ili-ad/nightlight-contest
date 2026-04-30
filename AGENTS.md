AGENTS.md draft
# Nightlight Contest Agent Notes

This project is an embedded Arduino/Nano Every physical light sculpture. Treat it as a small hardware system first and a software abstraction exercise second.

## Prime directive

Prefer the simplest bench-proven implementation over clever architecture.

If a standalone bench sketch works on the physical hardware, use its construction/init/read pattern as the reference. Do not replace it with a more abstract version unless the replacement is tested on the hardware and demonstrably preserves behavior.

## Known-good behavioral reference

The current visual/radar behavior reference is:

```text
bench/anthurium_lite_smoke_v3/anthurium_lite_smoke_v3.ino

This sketch is important. Do not overwrite it, simplify it, or use it as scratch space.

It demonstrates the desired Anthurium behavior:

C4001 radar at I2C address 0x2B
direct C4001 initialization
stable target hold/fade
range-derived motion
smoothed charge
smoothed ingress
warm/cool hue response
stamen/J conveyor behavior
torus/ring reservoir with diffusion and decay

Production changes to AnthuriumScene or C4001StableSource should be checked against this reference before changing the visual model.

Hardware contract

Final LED topology:

RightJ / J1:      12 pixels
LeftJ / J2:       12 pixels
FrontRing / O1:   44 pixels
RearRing / O2:    44 pixels
Total:           112 pixels

Physical order:

RightJ -> LeftJ -> FrontRing -> RearRing

LED type:

5V SK6812 RGBW

Data pin:

D6

C4001 radar:

I2C address 0x2B

Microphone:

A0

No ambient light sensor is installed in the final build.

Startup and runtime behavior

Expected boot behavior:

Power on
-> startup progressive fill
-> Anthurium interactive mode
-> double clap cycles Anthurium -> Nightlight -> Off -> Anthurium

Startup must not depend on radar, microphone, or any sensor init.

The startup fill should always run even if every sensor is unplugged.

Embedded safety rules
1. No hardware side effects in global constructors

Do not call hardware APIs from global/static constructors.

Avoid this kind of pattern:

SomeHardwareThing gThing(Profiles::someRuntimeValue());

Especially avoid profile/function calls in global hardware object initializers.

Use boring compile-time constants for hardware addresses where possible:

namespace {
constexpr uint8_t kC4001I2cAddress = 0x2B;
DFRobot_C4001_I2C gC4001(&Wire, kC4001I2cAddress);
}

Then initialize the hardware explicitly from setup() or a clearly controlled init method.

2. Do not call blocking hardware init from render-critical paths

Functions called every frame must return quickly.

These functions must not call potentially blocking hardware initialization:

App::loop()
AnthuriumScene::render()
C4001StableSource::read()
PixelOutput::show()

C4001StableSource::read(nowMs) must not call gC4001.begin().

If radar init is attempted, it must be explicit, logged, and isolated.

3. Prefer known-good bench init for C4001

The working bench sketch initializes the C4001 plainly:

Wire.begin();
gC4001.begin();
gC4001.setSensorMode(eSpeedMode);
gC4001.setDetectThres(11, 1200, 10);
gC4001.setFrettingDetection(eON);

Production should not invent a complex radar init strategy unless the simple bench approach is proven insufficient on hardware.

4. Telemetry must distinguish app health from sensor health

After startup, production must keep printing telemetry even if radar is offline.

Healthy app with offline radar is acceptable:

telemetry mode=Anthurium online=0 hasTarget=0 ...

A bad state is:

event=c4001_init_begin

followed by no more logs. That means the app is blocked.

5. Off must be the only black mode

Anthurium must not go completely black just because radar is offline.

If radar is offline or has no target, Anthurium should render a low, stable idle field. Live radar should bloom over that idle field.

6. Preserve bench sketches

Bench sketches are evidence. Do not casually refactor them.

Especially protect:

bench/anthurium_lite_smoke_v3/anthurium_lite_smoke_v3.ino
bench/C4001NearFieldProbe_near_focus_v2/C4001NearFieldProbe_near_focus_v2.ino
bench/KY037ClapSmoke/KY037ClapSmoke.ino
bench/CommissioningTopology112/CommissioningTopology112.ino

Production can learn from them, but should not overwrite them.

Visual behavior guidance

The Anthurium scene should not behave like a state switch.

Avoid:

Approach = whole scene red
Retreat = whole scene blue
No target = black

Prefer continuous signals:

range
range delta
speed
nearness
motion magnitude
charge
ingress
hold/fade influence

J/spadix behavior should feel like a signal conveyor or delay line. Direction changes should not recolor the whole J. The J should be able to contain historical warm and cool samples at the same time.

Ring behavior should feel like a soft reservoir or damped field. Energy should diffuse and decay. Avoid clock-face chase animations unless explicitly requested.

Debugging priority

When production disagrees with a working bench sketch:

Trust the bench sketch first.
Compare hardware constants.
Compare init order.
Compare blocking calls.
Compare data path.
Only then tune visuals.

Do not solve missing radar data by adding more visual abstraction.


---

## Audit findings: fragile/static construction risks

### 1. C4001 global construction is now better, but init remains the danger zone

The current `C4001StableSource.cpp` now uses a file-local compile-time constant:

```cpp
constexpr uint8_t kC4001I2cAddress = 0x2B;
DFRobot_C4001_I2C gC4001(&Wire, kC4001I2cAddress);

That is a good correction. It avoids the earlier fragile pattern of using Profiles::c4001().i2cAddress inside a global object initializer. The current read() also returns an offline track if the sensor is not initialized or ready, rather than trying to initialize hardware from the read path.

The remaining issue is not static initialization anymore. It is that radar init/read behavior still differs from the bench sketch even though the bench sketch produces healthy live data. Production logs stay offline at rangeM=1.20 and zeros【turn112file2】, while the bench log streams live values【turn112file3】.

2. App-level globals exist, but they are less risky

App.cpp still constructs global objects:

LayoutMap gLayoutMap;
PixelOutput gPixelOutput(gLayoutMap);
App gApp(gLayoutMap, gPixelOutput);

This is less dangerous than global hardware init because App::setup() performs the actual hardware begin calls later. Still, it is worth documenting as a future caution. The safest embedded pattern would be lazy construction inside getApp() or plain static storage with explicit begin() only.

3. LayoutMap does profile lookup during construction

LayoutMap::LayoutMap() reads Profiles::topology() in its constructor.

This appears safe enough because the profile is static configuration, but agents should avoid expanding this pattern into hardware setup or dynamic runtime dependency. It should remain pure data only.

4. PixelOutput constructs the NeoPixel object before setup, but begins it later

PixelOutput constructs Adafruit_NeoPixel in its constructor using the layout’s pixel count, but actual hardware work is delayed until PixelOutput::begin(), where strip_.begin(), clear(), and show() happen.

