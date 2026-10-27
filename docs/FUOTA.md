# FUOTA Setup Guide

## 1. ChirpStack Device Profile (AU915)

In the ChirpStack web UI:

1. Device Profiles → select profile used by the test devices → **RX2 data rate** = `10`
2. Save. ChirpStack will issue `RXParamSetupReq` on the next downlink; the device's RadioLib stack auto-acks and switches RX2 to DR10 (SF10/500 kHz, 222 B max app payload).

Verification from device UART: after the next uplink, the `LORAWAN` log should show a received MAC command. RadioLib doesn't log this directly — instead verify by deploying a FUOTA session (Phase 3+) and confirming the device receives non-fragmented downlinks larger than 53 bytes.

## 2. chirpstack-fuota-server

Clone and deploy per upstream README: <https://github.com/chirpstack/chirpstack-fuota-server>
Configure it to point at the same ChirpStack instance (application server API endpoint + API key).

Sections below will be populated by Phases 1–5 of the implementation plan.

## 3. Running a FUOTA Campaign — TBD (filled in Phase 5)
## 4. Generating Delta Patches — TBD (filled in Phase 4)
## 5. Monitoring and Rollback — TBD (filled in Phase 5)
