/**
 * SkyGuard Dashboard - Main Application
 *
 * Real-time flight monitoring dashboard featuring:
 * - Radar display with aircraft positions
 * - Flight list with status indicators
 * - Conflict alerts with severity levels
 * - System metrics
 *
 * For interview: "The dashboard uses React with Zustand for state management
 * and Canvas for the radar display. WebSocket provides real-time updates
 * from the C++ backend, and the Canvas rendering is optimized for frequent
 * updates with many aircraft."
 */

import { useWebSocket } from './hooks/useWebSocket';
import { useDemoMode } from './hooks/useDemoMode';
import {
  RadarDisplay,
  FlightList,
  AlertsPanel,
  MetricsBar,
  FlightDetail,
} from './components';
import './App.css';

// Enable demo mode via URL param: ?demo=true
const isDemoMode = new URLSearchParams(window.location.search).get('demo') === 'true';

function App() {
  const { connected, reconnectAttempts } = useWebSocket();

  // Start demo mode if explicitly enabled or after 3 failed connection attempts
  useDemoMode(isDemoMode || reconnectAttempts >= 3);

  return (
    <div className="app">
      <MetricsBar connected={connected} />

      <main className="main-content">
        <aside className="left-panel">
          <FlightList />
        </aside>

        <section className="center-panel">
          <RadarDisplay size={600} />
          <FlightDetail />
        </section>

        <aside className="right-panel">
          <AlertsPanel />
        </aside>
      </main>

      {!connected && reconnectAttempts < 3 && !isDemoMode && (
        <div className="connection-overlay">
          <div className="connection-modal">
            <div className="spinner" />
            <h2>Connecting to SkyGuard...</h2>
            <p>Attempting to establish WebSocket connection</p>
            <p className="attempt-count">Attempt {reconnectAttempts + 1} of 3</p>
            <a href="?demo=true" className="demo-link">
              Or try Demo Mode
            </a>
          </div>
        </div>
      )}
    </div>
  );
}

export default App;
