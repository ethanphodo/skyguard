# SkyGuard - Flight Data Monitoring & Alerting System

A real-time flight monitoring system demonstrating skills relevant to air traffic control software development.

## Architecture Overview

```
┌─────────────────┐     UDP      ┌─────────────────┐    WebSocket    ┌─────────────────┐
│  Radar/ADS-B    │ ──────────▶ │   C++ Backend   │ ◀─────────────▶ │ React Dashboard │
│  (Simulator)    │   Port 5000  │                 │    Port 8080    │                 │
└─────────────────┘              └─────────────────┘                 └─────────────────┘
      Python                           C++17                           TypeScript/React
```

## Components

### 1. C++ Backend (`/backend`)

Multi-threaded real-time processing engine:
- **RadarReceiver**: UDP socket with SCHED_FIFO real-time priority
- **ProcessingEngine**: Worker pool with shared_mutex for concurrent reads
- **AIValidator**: CPA calculation with human-written safety gates
- **WebSocketServer**: Real-time dashboard communication
- **Logger**: Async logging to avoid blocking

Key technical points:
- Lock-free patterns where possible
- Bounded queue with backpressure
- Signal-safe graceful shutdown

### 2. Python Simulator (`/simulator`)

Generates realistic flight data for testing:
- Configurable number of aircraft
- Predefined conflict scenarios (head-on, crossing, vertical)
- Binary protocol matching real radar format

### 3. React Dashboard (`/dashboard`)

Real-time visualization:
- Canvas-based radar display (60fps capable)
- Zustand state management
- WebSocket with auto-reconnect
- Demo mode for standalone testing

## Quick Start

### Demo Mode (No Backend Required)

```bash
cd dashboard
npm install
npm run dev
# Open http://localhost:3000?demo=true
```

### Full System

Terminal 1 - Backend:
```bash
cd backend
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./skyguard --port 5000 --workers 4
```

Terminal 2 - Simulator:
```bash
cd simulator
python3 flight_simulator.py --flights 20 --conflict
```

Terminal 3 - Dashboard:
```bash
cd dashboard
npm install
npm run dev
# Open http://localhost:3000
```

## Scenario Testing

```bash
# List available scenarios
python3 simulator/scenarios.py --list

# Run specific scenario
python3 simulator/scenarios.py head-on
python3 simulator/scenarios.py crossing-90
python3 simulator/scenarios.py multiple
```

## Interview Talking Points

### Real-Time Systems
> "The backend uses SCHED_FIFO thread priority for the receiver thread to ensure packets aren't dropped under load. The bounded queue provides backpressure - if processing falls behind, we drop old packets rather than consuming unlimited memory."

### Thread Safety
> "I used std::shared_mutex for the flight table - allows multiple workers to read concurrently while still protecting writes. The conflict detection runs periodically using compare_exchange_strong to ensure only one worker runs it at a time."

### AI + Safety Gates
> "The CPA calculation was AI-generated, but I wrote the safety gates: input validation for NaN/Inf, divide-by-zero prevention for matching velocities, and output clamping. The AI-generated math is documented with the formula derivation."

### Performance
> "Canvas for the radar display instead of SVG - with 100+ aircraft updating at 2Hz, Canvas avoids DOM manipulation overhead. The Zustand store updates trigger re-renders only in subscribed components."

## Project Structure

```
skyguard/
├── backend/
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp              # Entry point, signal handling
│       ├── types.hpp             # Common types, constants
│       ├── flight_packet.hpp     # Binary protocol, FlightState
│       ├── packet_queue.hpp      # Thread-safe bounded queue
│       ├── radar_receiver.*      # UDP socket, RT priority
│       ├── processing_engine.*   # Worker pool, conflict detection
│       ├── ai_validator.*        # CPA calculation + safety gates
│       ├── websocket_server.*    # Dashboard communication
│       └── logger.hpp            # Async logging
│
├── simulator/
│   ├── flight_simulator.py       # Main simulator
│   ├── scenarios.py              # Predefined test scenarios
│   ├── test_receiver.py          # Packet verification
│   └── requirements.txt
│
├── dashboard/
│   ├── package.json
│   ├── vite.config.ts
│   └── src/
│       ├── App.tsx
│       ├── components/           # React components
│       ├── hooks/                # WebSocket, demo mode
│       ├── stores/               # Zustand state
│       └── types/                # TypeScript types
│
└── README.md
```

## Dependencies

### Backend
- C++17 compiler (GCC 9+ or Clang 10+)
- CMake 3.16+
- OpenSSL (for WebSocket handshake)
- pthreads

### Simulator
- Python 3.8+
- No external dependencies (standard library only)

### Dashboard
- Node.js 18+
- React 18
- Zustand (state management)
- Vite (build tool)
