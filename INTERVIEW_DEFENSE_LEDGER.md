# SkyGuard Defense Ledger
## Interview Talking Points for Leidos Air Traffic Control Systems Position

---

## Executive Summary

SkyGuard is a real-time flight monitoring and conflict detection system demonstrating:
- Multi-threaded C++ backend with real-time scheduling
- Safety-critical design patterns
- Systems engineering (CMake, systemd, deployment)
- AI-assisted development with human validation

---

## Component Defense Matrix

| Component | Technical Defense | Safety-Critical Angle | Leidos Job Mapping |
|-----------|-------------------|----------------------|-------------------|
| **UDP Receiver** | Non-blocking sockets with poll() prevent thread stalls | System remains responsive during network bursts | "Design real-time systems" |
| **Worker Pool** | Fixed-size pool based on CPU cores | Prevents resource exhaustion during high traffic | "Develop mission-critical software" |
| **Bounded Queue** | Lock-free patterns where possible | Backpressure prevents memory exhaustion | "Performance optimization" |
| **AI Validator** | Two-layer design: AI math + human safety gates | Defense in depth for calculation errors | "Use AI responsibly" |
| **WebSocket Server** | Decoupled from core processing | UI crash doesn't affect radar processing | "System resilience" |
| **CMake Build** | Target-based configuration, sanitizers in debug | Reproducible builds across environments | "Automation scripts" |
| **systemd Service** | Security hardening, restart policies | Production-grade deployment | "Linux/Red Hat experience" |

---

## Deep Dive: Key Components

### 1. RadarReceiver (radar_receiver.cpp)

**Problem:** Need to receive high-frequency UDP packets (1000+/sec) without dropping data.

**Action:**
```cpp
// Set real-time thread priority (SCHED_FIFO)
struct sched_param param;
param.sched_priority = thread_priority_;
pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
```

**Safety Trade-off:**
- SCHED_FIFO ensures receiver always gets CPU time
- Risk: Could starve other processes if misconfigured
- Mitigation: Cap priority at 50 (not max 99), run in cgroup

**Interview Quote:**
> "I used SCHED_FIFO real-time scheduling for the receiver thread. In air traffic systems, packet loss means lost radar returns - unacceptable. The bounded queue provides backpressure if processing falls behind, dropping old packets rather than consuming unlimited memory."

---

### 2. PacketQueue (packet_queue.hpp)

**Problem:** Multiple producers (receiver) and consumers (workers) accessing shared queue.

**Action:**
```cpp
// Bounded queue with condition variable
std::mutex mutex_;
std::condition_variable cv_;
std::queue<T> queue_;
static constexpr size_t MAX_SIZE = 10000;

bool push(T item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= MAX_SIZE) {
        return false;  // Drop packet rather than block
    }
    queue_.push(std::move(item));
    cv_.notify_one();
    return true;
}
```

**Safety Trade-off:**
- Bounded size prevents memory exhaustion
- Risk: Packets dropped during sustained overload
- Mitigation: Queue depth monitored in metrics, alert on high water mark

**Interview Quote:**
> "The queue is bounded at 10,000 packets. If processing falls behind, we drop old packets rather than consuming unlimited memory. This is a conscious trade-off: in radar systems, a 5-second-old position is useless anyway."

---

### 3. AIValidator (ai_validator.cpp)

**Problem:** Need accurate Closest Point of Approach (CPA) calculation with edge case handling.

**Action:** Two-layer design:
```cpp
// LAYER 1: AI-Generated CPA Math
// t_cpa = -(Pr · Vr) / |Vr|²
double dot_product = rel_pos.dot(rel_vel);
time_to_cpa = -dot_product / rel_vel_sq;

// LAYER 2: Human-Written Safety Gate
bool validate_inputs(const FlightState& f1, const FlightState& f2) {
    // Check for NaN/Inf (sensor errors)
    if (!std::isfinite(f1.position.x)) return false;
    // Check for unrealistic speeds (> Mach 3)
    if (speed_sq > MAX_AIRCRAFT_SPEED * MAX_AIRCRAFT_SPEED) return false;
    // Reject stale data
    if (f1.is_stale(10.0)) return false;
    return true;
}
```

**Safety Trade-off:**
- AI generates well-documented math (faster development)
- Human validates edge cases AI might miss
- Risk: Over-reliance on AI-generated code
- Mitigation: Explicit documentation, reference to FAA standards

**Interview Quote:**
> "I used AI to generate the CPA geometry - it's well-documented calculus that I validated against FAA Order 7110.65. But I wrote the safety gates myself: input validation for NaN/Inf from sensor errors, divide-by-zero prevention when aircraft have matching velocities, and output clamping to physical limits. The AI handles the math; humans handle the edge cases."

---

### 4. ProcessingEngine (processing_engine.cpp)

**Problem:** Need concurrent access to flight state table from multiple workers.

**Action:**
```cpp
// Read-write lock for flight table
std::shared_mutex table_mutex_;
std::unordered_map<FlightId, FlightState> flight_table_;

// Multiple readers (conflict detection)
std::shared_lock<std::shared_mutex> lock(table_mutex_);
for (const auto& [id, state] : flight_table_) { ... }

// Single writer (packet update)
std::unique_lock<std::shared_mutex> lock(table_mutex_);
flight_table_[id].update(packet);
```

**Safety Trade-off:**
- shared_mutex allows concurrent reads (better throughput)
- Risk: Writer starvation under heavy read load
- Mitigation: Writes are fast (single field update), conflict detection runs periodically

**Interview Quote:**
> "I used std::shared_mutex for the flight table - multiple workers can read concurrently during conflict detection, while packet updates take an exclusive lock. This is the same pattern I used in StashDeck for audio analysis - optimizing for read-heavy workloads."

---

### 5. systemd Service (skyguard.service)

**Problem:** Need production-grade deployment with proper security.

**Action:**
```ini
# Security hardening
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
NoNewPrivileges=yes
CapabilityBoundingSet=CAP_NET_BIND_SERVICE CAP_SYS_NICE

# Resource limits
MemoryMax=512M
CPUQuota=320%
LimitRTPRIO=50

# Restart policy
Restart=on-failure
RestartSec=5
```

**Safety Trade-off:**
- Sandboxing limits blast radius of vulnerabilities
- Risk: Too restrictive might break functionality
- Mitigation: Tested each restriction, whitelisted minimum capabilities

**Interview Quote:**
> "I didn't just write a script; I deployed it as a Linux daemon with proper security hardening. Even if the application has a vulnerability, systemd sandboxing limits what an attacker can do. The service runs as a dedicated user with minimal capabilities - just enough for network access and real-time scheduling."

---

### 6. CMake Build System (CMakeLists.txt)

**Problem:** Need reproducible, cross-platform builds with proper tooling.

**Action:**
```cmake
# Modern CMake: Target-based configuration
target_compile_options(skyguard PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# Export compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform-specific handling
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(skyguard PRIVATE rt)
    target_compile_definitions(skyguard PRIVATE SKYGUARD_LINUX_RT)
endif()
```

**Safety Trade-off:**
- Sanitizers in debug catch memory bugs early
- Risk: Sanitizers add runtime overhead
- Mitigation: Only in debug builds, not production

**Interview Quote:**
> "I use modern CMake practices - target-based configuration, generator expressions, and exported compile commands for IDE integration. Debug builds enable AddressSanitizer and UBSan to catch buffer overflows and undefined behavior before they reach production. This is the same rigor I'd apply to FAA-certified code."

---

## Quick Reference: Key Technical Decisions

| Decision | Rationale | Alternative Considered |
|----------|-----------|----------------------|
| UDP over TCP | Low latency, multicast support | TCP would add connection overhead |
| Fixed worker pool | Predictable resource usage | Dynamic scaling could cause instability |
| Bounded queue | Memory safety | Unbounded could cause OOM |
| shared_mutex | Read-heavy workload | mutex would serialize all access |
| Canvas (dashboard) | Performance with many aircraft | SVG would thrash DOM |
| Zustand (dashboard) | Simple, selective updates | Redux would be overkill |

---

## Matching to Leidos Job Requirements

### "Design, develop, and test real-time systems"
- RadarReceiver with SCHED_FIFO priority
- Worker pool with lock-free patterns
- Sub-millisecond packet processing

### "Develop and maintain automation scripts"
- CMakeLists.txt with cross-platform support
- systemd service file
- install.sh deployment script

### "Use AI responsibly"
- AI-generated CPA math with documented formulas
- Human-written safety validation layer
- Clear separation and documentation

### "Linux/Red Hat experience"
- systemd service with security hardening
- POSIX threading (pthread)
- Real-time scheduling APIs

### "Collaborate across functions"
- React dashboard for operators
- WebSocket for real-time updates
- Clear API contracts between components

---

## Demo Script

1. **Start Dashboard in Demo Mode**
   ```bash
   cd dashboard && npm run dev
   # Open http://localhost:3000?demo=true
   ```

2. **Show Backend Build**
   ```bash
   cd backend/build
   cmake .. -DCMAKE_BUILD_TYPE=Debug
   cmake --build .
   ```

3. **Run Full System**
   ```bash
   # Terminal 1: Backend
   ./skyguard --port 5000 --workers 4

   # Terminal 2: Simulator with conflict
   python3 ../simulator/scenarios.py head-on

   # Terminal 3: Dashboard
   npm run dev
   ```

4. **Key Points to Highlight**
   - Watch conflict alert appear in dashboard
   - Show CPA countdown in real-time
   - Explain two-layer AI validation
   - Discuss why UDP, why bounded queue

---

## Questions to Prepare For

1. **"Why UDP instead of TCP?"**
   > Real radar systems use UDP - low latency, multicast support, and packet loss is acceptable since we'll get the next ping. TCP's connection setup and retransmission add unacceptable latency.

2. **"How do you handle packet loss?"**
   > The system is designed to be resilient to packet loss. Each radar return is independent. If we miss one, we'll get the next one in 1-4 seconds. The conflict detection algorithm uses current positions, not historical data.

3. **"What happens if the dashboard crashes?"**
   > Nothing to the core system. The WebSocket server and C++ backend are decoupled. The backend continues processing radar data and detecting conflicts. We'd lose visibility but not safety.

4. **"How would you scale this to 10,000 flights?"**
   > The O(n²) conflict detection would need optimization. I'd implement spatial partitioning - an octree or grid - to only check nearby aircraft pairs. Reduce from O(n²) to O(n log n). The current architecture supports this; just swap the detection algorithm.

5. **"How did you validate the AI-generated code?"**
   > Three ways: (1) Documented the math derivation in comments and verified against FAA references, (2) Wrote unit tests with known aircraft positions and expected CPA values, (3) Added human-written safety gates for edge cases the AI might miss - NaN handling, divide-by-zero, unrealistic speeds.
