# Unit1-Unit2 Communication Issues and Improvement Plan

This document records the identified issues and future improvement plans regarding the communication between Winch, Unit1, and Unit2, discussed on February 18, 2026.

## 1. Current Implementation Issues (v2.3.23)

### A. Blocking Reception (`readStringUntil`)
- **Location**: `src/main.cpp` (Line ~819) inside `loop()`
- **Code**: `handleWinchCommand(Serial2.readStringUntil('\n'));`
- **Issue**:
  - `readStringUntil` blocks the entire system execution until a newline character is received or a timeout occurs (default 1000ms).
  - If a packet is fragmented or noise corrupts the newline character, the main loop can freeze for up to 1 second.
- **Impact**:
  - **High Risk**: During this freeze, **no sensor updates (IMU) are sent to Unit2**. Unit2 loses orientation data, potentially causing instability or uncontrolled motor behavior.
  - LED animations also freeze.

### B. Exclusive Transmission Logic (`processTubeQueue`)
- **Location**: `Unit2/src/main.cpp` (Sending back to Winch) or `Unit1` logic for forwarding commands.
- **Code Logic**:
  ```cpp
  void processTubeQueue() {
    if (posDirty) {
      sendPOS();
      return; // EARLY RETURN!
    }
    if (rgbDirty) {
      sendRGB();
    }
  }
  ```
- **Issue**:
  - The `return` statement forces an exclusive choice: either POS or RGB is sent per cycle (`TUBE_SEND_INTERVAL`).
  - If `posDirty` is frequently true (high-speed motion), `rgbDirty` checks may be starved, causing significant LED latency.
  - Conversely, if priority is swapped to RGB, motor commands (POS) may be delayed or dropped, which is risky for control stability.

### C. Text-Based Protocol Overhead
- **Format**: `POS,123.45,67.89\n`
- **Issue**:
  - **Inefficiency**: Converting floats to strings (`snprintf`) and parsing them back (`atof`) is CPU-intensive and increases data volume (6 bytes for "123.45" vs 4 bytes for binary float).
  - **Parsing complexity**: String manipulation is prone to buffer overflows and harder to validate than fixed-structure binary data.

---

## 2. Improvement Plan (Future Work)

### Phase 1: Non-Blocking Reception (Immediate Stability Fix)
- **Action**: Replace `readStringUntil` with a non-blocking character buffer approach.
- **Logic**:
  1. Read available bytes one by one into a static buffer.
  2. If `\n` is detected, process the buffer as a complete command.
  3. If buffer is full without `\n`, discard/reset to prevent overflow.
  4. **Crucial**: The main loop **must continue running** between individual byte receptions.

### Phase 2: Transmission Logic Optimization
- **Action**: Remove the exclusive `return` in `processTubeQueue`.
- **Logic**:
  - Allow sending **both** POS and RGB in the same cycle if both flags are dirty.
  - Alternatively, combine them into a single status packet if the protocol allows.
  - Ensure `TUBE_SEND_INTERVAL` throttles the *frequency* of sending, not the *number* of packets sent per slot.

### Phase 3: Binary Protocol (Long-Term Efficiency)
- **Action**: Switch to a binary packet structure (e.g., COBS encoding or simple Header/Length/Data/Checksum).
- **Benefits**:
  - Reduces data size by ~50%.
  - Eliminates `snprintf`/`atof` overhead.
  - constant-time parsing.
- **Trade-off**: Harder to debug with standard Serial Monitor (requires custom tools or hex view).

---

## 3. Decision Log
- **2026-02-18**: Identified the blocking risk of `readStringUntil` as a critical flaw. Decided to prioritize documentation over immediate code refactoring to maintain current development velocity, but marked for future refactoring.
- **2026-02-18**: Analyzing `processTubeQueue`, confirmed that the current `return` logic causes mutual exclusion between POS and RGB updates, leading to latency in whichever is lower priority. Recommendation is to remove the `return` to enable multi-packet transmission per interval.
