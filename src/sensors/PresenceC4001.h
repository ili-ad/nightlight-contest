#pragma once

#include "PresenceTypes.h"
#include "../mapping/C4001TrackFilter.h"

class PresenceC4001 {
public:
  enum class LinkState : uint8_t {
    Online,
    DegradedHold,
    Offline
  };

  enum class SampleKind : uint8_t {
    Unknown,
    Target,
    NoTarget,
    ReadFailure
  };
  enum class RejectReason : uint8_t {
    None = 0,
    SpeedCap,
    RangeDelta,
    NearFieldCoherence,
    NoTarget
  };

  struct LinkStatus {
    LinkState state = LinkState::Offline;
    bool online = false;
    bool holding = false;
    uint16_t consecutiveFailures = 0;
    uint16_t consecutiveNoTargetSamples = 0;
    uint32_t lastSuccessMs = 0;
    uint32_t lastFailureMs = 0;
    uint32_t lastTargetMs = 0;
    uint32_t noTargetSinceMs = 0;
    uint32_t lastSampleMs = 0;
    bool noTargetHolding = false;
    bool noTargetCommitted = false;
    bool nearField = false;
    uint8_t nearFieldPendingCount = 0;
    SampleKind sampleKind = SampleKind::Unknown;
    RejectReason rejectReason = RejectReason::None;
    bool rejected = false;
  };

  struct Snapshot {
    CorePresence core;
    C4001PresenceRich rich;
  };

  void begin();
  Snapshot read();

  const C4001PresenceRich& lastRich() const;
  const LinkStatus& linkStatus() const;

private:
  bool initSensor();
  bool readSensorRich(C4001PresenceRich& outRich);
  bool shouldAttemptInit(uint32_t nowMs) const;
  bool acceptTargetSample(const C4001PresenceRich& rawRich,
                          uint32_t nowMs,
                          float& acceptedRangeM,
                          float& acceptedSpeedMps,
                          RejectReason& reason);
  void clearNearFieldCoherence();

  static float clamp01(float value);
  CorePresence buildCoreFromStableTrack(const C4001PresenceRich& rich, uint32_t nowMs) const;
  C4001TrackFilter::Sample buildStableSample(const C4001PresenceRich& rich);
  void applyStableTrack(C4001PresenceRich& rich,
                        C4001TrackFilter::InputClass inputClass,
                        uint32_t nowMs,
                        const C4001TrackFilter::Sample* acceptedSample);
  Snapshot applyFailure(uint32_t nowMs, C4001PresenceRich* richForTrack = nullptr);
  bool shouldPoll(uint32_t nowMs) const;

  bool initialized_ = false;
  LinkStatus linkStatus_{};
  uint32_t lastPollMs_ = 0;
  uint32_t lastInitAttemptMs_ = 0;
  bool hasPolled_ = false;
  bool sensorReady_ = false;

  CorePresence lastCore_{};
  C4001PresenceRich lastRich_{};

  bool hasAcceptedTarget_ = false;
  uint32_t lastAcceptedTargetMs_ = 0;
  float acceptedRangeM_ = 0.0f;
  float acceptedSpeedMps_ = 0.0f;
  bool hasNearFieldPending_ = false;
  uint8_t nearFieldPendingCount_ = 0;
  float nearFieldPendingRangeM_ = 0.0f;
  float nearFieldPendingSpeedMps_ = 0.0f;
  C4001TrackFilter stableTrackFilter_{};
  bool stableHasSmoothedRange_ = false;
  bool stableHasChargeTarget_ = false;
  float stableSmoothedRangeM_ = 0.0f;
  float stableLastChargeTarget_ = 0.0f;
};
