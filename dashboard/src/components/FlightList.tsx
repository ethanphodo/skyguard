/**
 * FlightList - Tabular Flight Information
 *
 * Shows all tracked flights in a sortable table with:
 * - Flight ID, altitude, heading, speed
 * - Conflict status highlighting
 * - Click to select and highlight on radar
 * - Auto-scroll to selected flight
 */

import { useRef, useEffect } from 'react';
import { useFlightStore } from '../stores/flightStore';
import type { Flight } from '../types';
import './FlightList.css';

export function FlightList() {
  const flights = useFlightStore((state) => state.getFlightsArray());
  const selectedFlightId = useFlightStore((state) => state.selectedFlightId);
  const selectFlight = useFlightStore((state) => state.selectFlight);
  const isFlightInConflict = useFlightStore((state) => state.isFlightInConflict);
  const containerRef = useRef<HTMLDivElement>(null);

  // Sort flights by ID
  const sortedFlights = [...flights].sort((a, b) => a.id - b.id);

  return (
    <div className="flight-list">
      <h3>Tracked Flights ({flights.length})</h3>
      <div className="flight-table-container" ref={containerRef}>
        <table className="flight-table">
          <thead>
            <tr>
              <th>ID</th>
              <th>FL</th>
              <th>HDG</th>
              <th>GS</th>
              <th>Status</th>
            </tr>
          </thead>
          <tbody>
            {sortedFlights.map((flight) => (
              <FlightRow
                key={flight.id}
                flight={flight}
                selected={flight.id === selectedFlightId}
                inConflict={isFlightInConflict(flight.id)}
                onClick={() => selectFlight(flight.id)}
                containerRef={containerRef}
              />
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}

interface FlightRowProps {
  flight: Flight;
  selected: boolean;
  inConflict: boolean;
  onClick: () => void;
  containerRef: React.RefObject<HTMLDivElement>;
}

function FlightRow({ flight, selected, inConflict, onClick, containerRef }: FlightRowProps) {
  const rowRef = useRef<HTMLTableRowElement>(null);
  const flightLevel = Math.round(flight.alt / 100);
  const heading = Math.round(flight.hdg);
  const groundSpeed = Math.round(flight.spd);

  // Auto-scroll to this row when selected
  useEffect(() => {
    if (selected && rowRef.current && containerRef.current) {
      const row = rowRef.current;
      const container = containerRef.current;

      // Check if row is visible in the container
      const rowTop = row.offsetTop;
      const rowBottom = rowTop + row.offsetHeight;
      const containerTop = container.scrollTop;
      const containerBottom = containerTop + container.clientHeight;

      // Scroll if row is not fully visible
      if (rowTop < containerTop || rowBottom > containerBottom) {
        row.scrollIntoView({
          behavior: 'smooth',
          block: 'center',
        });
      }

      // Add highlight animation class
      row.classList.add('scroll-target');
      const timeout = setTimeout(() => {
        row.classList.remove('scroll-target');
      }, 500);

      return () => clearTimeout(timeout);
    }
  }, [selected, containerRef]);

  let className = 'flight-row';
  if (selected) className += ' selected';
  if (inConflict) className += ' conflict';

  return (
    <tr ref={rowRef} className={className} onClick={onClick} data-flight-id={flight.id}>
      <td className="flight-id">{flight.id}</td>
      <td>FL{flightLevel.toString().padStart(3, '0')}</td>
      <td>{heading.toString().padStart(3, '0')}°</td>
      <td>{groundSpeed}kt</td>
      <td>
        {inConflict ? (
          <span className="status-conflict">CONFLICT</span>
        ) : (
          <span className="status-normal">Normal</span>
        )}
      </td>
    </tr>
  );
}
