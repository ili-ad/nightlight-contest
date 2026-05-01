# C4001StableSource alignment notes

- Production `C4001StableSource` previously attempted sensor init only when a manual request flag was set, while the bench sketch retries automatically every 1000 ms when offline.
- Production now mirrors the bench retry behavior by attempting explicit re-init on a timed cadence (`initRetryMs`) while offline, without blocking render flow.
- Read acceptance now also matches the bench's key filter gate for unrealistic speed outliers (`abs(speedMps) <= 2.60`), while preserving production `StableTrack` smoothing/continuity output fields.
