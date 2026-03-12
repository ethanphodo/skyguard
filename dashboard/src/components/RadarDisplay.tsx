/**
 * RadarDisplay - Canvas-Based Flight Visualization
 *
 * Renders a radar-style display showing:
 * - Aircraft positions as triangular icons
 * - Heading indicators
 * - Flight ID labels
 * - Conflict highlights
 * - Range rings
 *
 * For interview: "I used Canvas for the radar display because it's more
 * performant than SVG for frequent updates. With 100+ aircraft updating
 * at 2Hz, Canvas avoids the DOM manipulation overhead."
 */

import { useEffect, useRef, useCallback } from 'react';
import { useFlightStore } from '../stores/flightStore';
import type { Flight } from '../types';

// Display configuration
const RADAR_BG = '#0a1628';
const RADAR_RING = '#1a3a5c';
const RADAR_TEXT = '#4a90d9';
const AIRCRAFT_NORMAL = '#00ff88';
const AIRCRAFT_SELECTED = '#ffff00';
const AIRCRAFT_CONFLICT = '#ff4444';
const TRAIL_COLOR = 'rgba(0, 255, 136, 0.3)';

// Geographic bounds (should match simulator)
const LAT_MIN = 33.5;
const LAT_MAX = 34.5;
const LON_MIN = -118.8;
const LON_MAX = -117.5;

interface Props {
  width?: number;
  height?: number;
}

export function RadarDisplay({ width = 600, height = 600 }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const flights = useFlightStore((state) => state.getFlightsArray());
  const alerts = useFlightStore((state) => state.getAlertsArray());
  const selectedFlightId = useFlightStore((state) => state.selectedFlightId);
  const selectFlight = useFlightStore((state) => state.selectFlight);
  const isFlightInConflict = useFlightStore((state) => state.isFlightInConflict);

  // Convert lat/lon to canvas coordinates
  const toCanvas = useCallback(
    (lat: number, lon: number): [number, number] => {
      const x = ((lon - LON_MIN) / (LON_MAX - LON_MIN)) * width;
      const y = height - ((lat - LAT_MIN) / (LAT_MAX - LAT_MIN)) * height;
      return [x, y];
    },
    [width, height]
  );

  // Draw the radar display
  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Clear background
    ctx.fillStyle = RADAR_BG;
    ctx.fillRect(0, 0, width, height);

    // Draw range rings
    const centerX = width / 2;
    const centerY = height / 2;
    const maxRadius = Math.min(width, height) / 2 - 20;

    ctx.strokeStyle = RADAR_RING;
    ctx.lineWidth = 1;

    for (let i = 1; i <= 4; i++) {
      const radius = (maxRadius / 4) * i;
      ctx.beginPath();
      ctx.arc(centerX, centerY, radius, 0, Math.PI * 2);
      ctx.stroke();
    }

    // Draw crosshairs
    ctx.beginPath();
    ctx.moveTo(centerX, 20);
    ctx.lineTo(centerX, height - 20);
    ctx.moveTo(20, centerY);
    ctx.lineTo(width - 20, centerY);
    ctx.stroke();

    // Draw range labels
    ctx.fillStyle = RADAR_TEXT;
    ctx.font = '10px monospace';
    ctx.textAlign = 'center';
    for (let i = 1; i <= 4; i++) {
      const nm = i * 10; // Approximate nautical miles
      ctx.fillText(`${nm}nm`, centerX + (maxRadius / 4) * i + 5, centerY - 5);
    }

    // Draw conflict lines
    ctx.strokeStyle = AIRCRAFT_CONFLICT;
    ctx.lineWidth = 2;
    ctx.setLineDash([5, 5]);

    alerts.forEach((alert) => {
      const f1 = flights.find((f) => f.id === alert.flight1);
      const f2 = flights.find((f) => f.id === alert.flight2);

      if (f1 && f2) {
        const [x1, y1] = toCanvas(f1.lat, f1.lon);
        const [x2, y2] = toCanvas(f2.lat, f2.lon);

        ctx.beginPath();
        ctx.moveTo(x1, y1);
        ctx.lineTo(x2, y2);
        ctx.stroke();
      }
    });

    ctx.setLineDash([]);

    // Draw aircraft
    flights.forEach((flight) => {
      const [x, y] = toCanvas(flight.lat, flight.lon);
      const isSelected = flight.id === selectedFlightId;
      const inConflict = isFlightInConflict(flight.id);

      // Determine color
      let color = AIRCRAFT_NORMAL;
      if (isSelected) color = AIRCRAFT_SELECTED;
      else if (inConflict) color = AIRCRAFT_CONFLICT;

      // Draw aircraft icon (triangle pointing in heading direction)
      drawAircraft(ctx, x, y, flight.hdg, color, isSelected);

      // Draw label
      ctx.fillStyle = color;
      ctx.font = isSelected ? 'bold 11px monospace' : '10px monospace';
      ctx.textAlign = 'left';
      ctx.fillText(
        `${flight.id}`,
        x + 12,
        y - 5
      );
      ctx.fillText(
        `FL${Math.round(flight.alt / 100)}`,
        x + 12,
        y + 7
      );
    });

    // Draw scale
    ctx.fillStyle = RADAR_TEXT;
    ctx.font = '11px monospace';
    ctx.textAlign = 'left';
    ctx.fillText('Los Angeles TRACON', 10, height - 10);
  }, [flights, alerts, selectedFlightId, isFlightInConflict, toCanvas, width, height]);

  // Draw aircraft triangle
  const drawAircraft = (
    ctx: CanvasRenderingContext2D,
    x: number,
    y: number,
    heading: number,
    color: string,
    selected: boolean
  ) => {
    const size = selected ? 10 : 8;
    const rad = (heading - 90) * (Math.PI / 180);

    ctx.save();
    ctx.translate(x, y);
    ctx.rotate(rad);

    // Draw triangle
    ctx.beginPath();
    ctx.moveTo(size, 0);
    ctx.lineTo(-size / 2, -size / 2);
    ctx.lineTo(-size / 2, size / 2);
    ctx.closePath();

    ctx.fillStyle = color;
    ctx.fill();

    if (selected) {
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 1;
      ctx.stroke();
    }

    ctx.restore();
  };

  // Handle click to select aircraft
  const handleClick = useCallback(
    (event: React.MouseEvent<HTMLCanvasElement>) => {
      const canvas = canvasRef.current;
      if (!canvas) return;

      const rect = canvas.getBoundingClientRect();
      const clickX = event.clientX - rect.left;
      const clickY = event.clientY - rect.top;

      // Find closest aircraft within 20px
      let closest: Flight | null = null;
      let minDist = 20;

      flights.forEach((flight) => {
        const [x, y] = toCanvas(flight.lat, flight.lon);
        const dist = Math.sqrt((x - clickX) ** 2 + (y - clickY) ** 2);
        if (dist < minDist) {
          minDist = dist;
          closest = flight;
        }
      });

      selectFlight(closest?.id ?? null);
    },
    [flights, toCanvas, selectFlight]
  );

  // Redraw on data change
  useEffect(() => {
    draw();
  }, [draw]);

  return (
    <canvas
      ref={canvasRef}
      width={width}
      height={height}
      onClick={handleClick}
      style={{
        borderRadius: '8px',
        cursor: 'crosshair',
      }}
    />
  );
}
