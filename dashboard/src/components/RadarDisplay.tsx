/**
 * RadarDisplay - Professional ATC-Style Radar Visualization
 *
 * Features:
 * - Circular radar with degree markings
 * - Rotating sweep line effect
 * - Aircraft icons with heading indicators
 * - Range rings with distance labels
 * - Green phosphor color scheme
 * - Conflict highlighting
 */

import { useEffect, useRef, useCallback, useState } from 'react';
import { useFlightStore } from '../stores/flightStore';
import type { Flight } from '../types';

// Classic radar green phosphor colors
const COLORS = {
  background: '#001a00',
  grid: '#003300',
  gridBright: '#004400',
  sweep: 'rgba(0, 255, 0, 0.15)',
  sweepLine: 'rgba(0, 255, 0, 0.8)',
  text: '#00ff00',
  textDim: '#008800',
  aircraft: '#00ff00',
  aircraftSelected: '#ffff00',
  aircraftConflict: '#ff0000',
  conflictLine: '#ff0000',
  rangeRing: '#004400',
  tickMark: '#006600',
  center: '#00ff00',
};

// Geographic bounds (LA area)
const LAT_MIN = 33.5;
const LAT_MAX = 34.5;
const LON_MIN = -118.8;
const LON_MAX = -117.5;
const CENTER_LAT = (LAT_MIN + LAT_MAX) / 2;
const CENTER_LON = (LON_MIN + LON_MAX) / 2;

interface Props {
  size?: number;
}

export function RadarDisplay({ size = 600 }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const sweepAngleRef = useRef(0);
  const animationRef = useRef<number>();
  const [hoveredFlight, setHoveredFlight] = useState<number | null>(null);

  const flights = useFlightStore((state) => state.getFlightsArray());
  const alerts = useFlightStore((state) => state.getAlertsArray());
  const selectedFlightId = useFlightStore((state) => state.selectedFlightId);
  const selectFlight = useFlightStore((state) => state.selectFlight);
  const isFlightInConflict = useFlightStore((state) => state.isFlightInConflict);

  const center = size / 2;
  const radius = (size / 2) - 40;

  // Convert lat/lon to radar coordinates (centered, circular)
  const toRadar = useCallback(
    (lat: number, lon: number): [number, number] => {
      const latNorm = (lat - CENTER_LAT) / ((LAT_MAX - LAT_MIN) / 2);
      const lonNorm = (lon - CENTER_LON) / ((LON_MAX - LON_MIN) / 2);

      // Scale to fit in radar circle
      const scale = radius * 0.85;
      const x = center + lonNorm * scale;
      const y = center - latNorm * scale;

      return [x, y];
    },
    [center, radius]
  );

  // Draw the radar display
  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Clear with dark background
    ctx.fillStyle = COLORS.background;
    ctx.fillRect(0, 0, size, size);

    // Draw outer ring with tick marks
    drawOuterRing(ctx, center, center, radius);

    // Draw range rings
    drawRangeRings(ctx, center, center, radius);

    // Draw crosshairs
    drawCrosshairs(ctx, center, center, radius);

    // Draw sweep effect
    drawSweep(ctx, center, center, radius, sweepAngleRef.current);

    // Draw conflict lines
    drawConflictLines(ctx);

    // Draw aircraft
    drawAircraft(ctx);

    // Draw center point
    ctx.fillStyle = COLORS.center;
    ctx.beginPath();
    ctx.arc(center, center, 4, 0, Math.PI * 2);
    ctx.fill();

    // Update sweep angle
    sweepAngleRef.current = (sweepAngleRef.current + 1.5) % 360;

    // Schedule next frame
    animationRef.current = requestAnimationFrame(draw);
  }, [flights, alerts, selectedFlightId, hoveredFlight, center, radius, size, toRadar, isFlightInConflict]);

  const drawOuterRing = (ctx: CanvasRenderingContext2D, cx: number, cy: number, r: number) => {
    // Outer circle
    ctx.strokeStyle = COLORS.gridBright;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.stroke();

    // Degree markings
    ctx.fillStyle = COLORS.text;
    ctx.font = '10px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';

    for (let deg = 0; deg < 360; deg += 10) {
      const rad = (deg - 90) * (Math.PI / 180);
      const outerR = r + 2;

      // Tick marks
      const isMajor = deg % 30 === 0;
      const tickInner = isMajor ? r - 15 : r - 8;

      ctx.strokeStyle = isMajor ? COLORS.text : COLORS.tickMark;
      ctx.lineWidth = isMajor ? 2 : 1;
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(rad) * tickInner, cy + Math.sin(rad) * tickInner);
      ctx.lineTo(cx + Math.cos(rad) * outerR, cy + Math.sin(rad) * outerR);
      ctx.stroke();

      // Degree labels (every 30 degrees)
      if (isMajor) {
        const labelR = r + 18;
        const x = cx + Math.cos(rad) * labelR;
        const y = cy + Math.sin(rad) * labelR;
        ctx.fillStyle = COLORS.text;
        ctx.fillText(deg.toString(), x, y);
      }
    }
  };

  const drawRangeRings = (ctx: CanvasRenderingContext2D, cx: number, cy: number, r: number) => {
    const rings = [0.25, 0.5, 0.75, 1.0];
    const distances = [10, 20, 30, 40]; // nautical miles

    ctx.strokeStyle = COLORS.rangeRing;
    ctx.lineWidth = 1;
    ctx.setLineDash([2, 4]);

    rings.forEach((ring, i) => {
      const ringR = r * ring;
      ctx.beginPath();
      ctx.arc(cx, cy, ringR, 0, Math.PI * 2);
      ctx.stroke();

      // Distance label
      if (ring < 1.0) {
        ctx.fillStyle = COLORS.textDim;
        ctx.font = '9px monospace';
        ctx.textAlign = 'left';
        ctx.fillText(`${distances[i]}nm`, cx + ringR + 5, cy - 3);
      }
    });

    ctx.setLineDash([]);
  };

  const drawCrosshairs = (ctx: CanvasRenderingContext2D, cx: number, cy: number, r: number) => {
    ctx.strokeStyle = COLORS.grid;
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);

    // Vertical line
    ctx.beginPath();
    ctx.moveTo(cx, cy - r);
    ctx.lineTo(cx, cy + r);
    ctx.stroke();

    // Horizontal line
    ctx.beginPath();
    ctx.moveTo(cx - r, cy);
    ctx.lineTo(cx + r, cy);
    ctx.stroke();

    // Diagonal lines
    const diag = r * 0.707;
    ctx.beginPath();
    ctx.moveTo(cx - diag, cy - diag);
    ctx.lineTo(cx + diag, cy + diag);
    ctx.stroke();

    ctx.beginPath();
    ctx.moveTo(cx + diag, cy - diag);
    ctx.lineTo(cx - diag, cy + diag);
    ctx.stroke();

    ctx.setLineDash([]);
  };

  const drawSweep = (ctx: CanvasRenderingContext2D, cx: number, cy: number, r: number, angle: number) => {
    const rad = (angle - 90) * (Math.PI / 180);

    // Sweep gradient (fade trail)
    const gradient = ctx.createConicGradient(rad, cx, cy);
    gradient.addColorStop(0, 'rgba(0, 255, 0, 0.3)');
    gradient.addColorStop(0.1, 'rgba(0, 255, 0, 0.1)');
    gradient.addColorStop(0.2, 'rgba(0, 255, 0, 0)');
    gradient.addColorStop(1, 'rgba(0, 255, 0, 0)');

    ctx.fillStyle = gradient;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.arc(cx, cy, r, rad - 0.5, rad);
    ctx.closePath();
    ctx.fill();

    // Sweep line
    ctx.strokeStyle = COLORS.sweepLine;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + Math.cos(rad) * r, cy + Math.sin(rad) * r);
    ctx.stroke();
  };

  const drawConflictLines = (ctx: CanvasRenderingContext2D) => {
    ctx.strokeStyle = COLORS.conflictLine;
    ctx.lineWidth = 2;
    ctx.setLineDash([5, 5]);

    alerts.forEach((alert) => {
      const f1 = flights.find((f) => f.id === alert.flight1);
      const f2 = flights.find((f) => f.id === alert.flight2);

      if (f1 && f2) {
        const [x1, y1] = toRadar(f1.lat, f1.lon);
        const [x2, y2] = toRadar(f2.lat, f2.lon);

        ctx.beginPath();
        ctx.moveTo(x1, y1);
        ctx.lineTo(x2, y2);
        ctx.stroke();
      }
    });

    ctx.setLineDash([]);
  };

  const drawAircraft = (ctx: CanvasRenderingContext2D) => {
    flights.forEach((flight) => {
      const [x, y] = toRadar(flight.lat, flight.lon);
      const isSelected = flight.id === selectedFlightId;
      const isHovered = flight.id === hoveredFlight;
      const inConflict = isFlightInConflict(flight.id);

      // Check if within radar circle
      const dist = Math.sqrt((x - center) ** 2 + (y - center) ** 2);
      if (dist > radius) return;

      // Determine color
      let color = COLORS.aircraft;
      if (inConflict) color = COLORS.aircraftConflict;
      if (isSelected || isHovered) color = COLORS.aircraftSelected;

      // Draw aircraft icon
      drawAircraftIcon(ctx, x, y, flight.hdg, color, isSelected || isHovered);

      // Draw data tag
      drawDataTag(ctx, x, y, flight, color, isSelected || isHovered);
    });
  };

  const drawAircraftIcon = (
    ctx: CanvasRenderingContext2D,
    x: number,
    y: number,
    heading: number,
    color: string,
    enlarged: boolean
  ) => {
    const scale = enlarged ? 1.3 : 1;
    const rad = (heading - 90) * (Math.PI / 180);

    ctx.save();
    ctx.translate(x, y);
    ctx.rotate(rad);
    ctx.scale(scale, scale);

    // Aircraft shape (simplified jet)
    ctx.fillStyle = color;
    ctx.beginPath();
    // Fuselage
    ctx.moveTo(8, 0);
    ctx.lineTo(-4, -2);
    ctx.lineTo(-6, 0);
    ctx.lineTo(-4, 2);
    ctx.closePath();
    ctx.fill();

    // Wings
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(-3, -6);
    ctx.lineTo(-4, -6);
    ctx.lineTo(-2, 0);
    ctx.lineTo(-4, 6);
    ctx.lineTo(-3, 6);
    ctx.closePath();
    ctx.fill();

    // Tail
    ctx.beginPath();
    ctx.moveTo(-5, 0);
    ctx.lineTo(-7, -3);
    ctx.lineTo(-8, -3);
    ctx.lineTo(-6, 0);
    ctx.lineTo(-8, 3);
    ctx.lineTo(-7, 3);
    ctx.closePath();
    ctx.fill();

    ctx.restore();

    // Glow effect for selected/conflict
    if (enlarged) {
      ctx.shadowColor = color;
      ctx.shadowBlur = 10;
      ctx.beginPath();
      ctx.arc(x, y, 3, 0, Math.PI * 2);
      ctx.fillStyle = color;
      ctx.fill();
      ctx.shadowBlur = 0;
    }
  };

  const drawDataTag = (
    ctx: CanvasRenderingContext2D,
    x: number,
    y: number,
    flight: Flight,
    color: string,
    showFull: boolean
  ) => {
    const flightLevel = Math.round(flight.alt / 100);
    const groundSpeed = Math.round(flight.spd);

    ctx.font = '10px monospace';
    ctx.fillStyle = color;
    ctx.textAlign = 'left';

    if (showFull) {
      // Full data tag
      ctx.fillText(`${flight.id}`, x + 14, y - 8);
      ctx.fillText(`FL${flightLevel.toString().padStart(3, '0')}`, x + 14, y + 2);
      ctx.fillText(`${groundSpeed}kt`, x + 14, y + 12);
    } else {
      // Minimal tag
      ctx.fillText(`${flight.id}`, x + 12, y + 4);
    }
  };

  // Handle mouse interaction
  const handleMouseMove = useCallback(
    (event: React.MouseEvent<HTMLCanvasElement>) => {
      const canvas = canvasRef.current;
      if (!canvas) return;

      const rect = canvas.getBoundingClientRect();
      const mouseX = event.clientX - rect.left;
      const mouseY = event.clientY - rect.top;

      // Find closest aircraft within 20px
      let closest: number | null = null;
      let minDist = 20;

      flights.forEach((flight) => {
        const [x, y] = toRadar(flight.lat, flight.lon);
        const dist = Math.sqrt((x - mouseX) ** 2 + (y - mouseY) ** 2);
        if (dist < minDist) {
          minDist = dist;
          closest = flight.id;
        }
      });

      setHoveredFlight(closest);
    },
    [flights, toRadar]
  );

  const handleClick = useCallback(
    (event: React.MouseEvent<HTMLCanvasElement>) => {
      const canvas = canvasRef.current;
      if (!canvas) return;

      const rect = canvas.getBoundingClientRect();
      const clickX = event.clientX - rect.left;
      const clickY = event.clientY - rect.top;

      // Find closest aircraft within 20px
      let closestId: number | null = null;
      let minDist = 20;

      flights.forEach((flight) => {
        const [x, y] = toRadar(flight.lat, flight.lon);
        const dist = Math.sqrt((x - clickX) ** 2 + (y - clickY) ** 2);
        if (dist < minDist) {
          minDist = dist;
          closestId = flight.id;
        }
      });

      selectFlight(closestId);
    },
    [flights, toRadar, selectFlight]
  );

  // Animation loop
  useEffect(() => {
    animationRef.current = requestAnimationFrame(draw);
    return () => {
      if (animationRef.current) {
        cancelAnimationFrame(animationRef.current);
      }
    };
  }, [draw]);

  return (
    <div className="radar-container">
      <canvas
        ref={canvasRef}
        width={size}
        height={size}
        onClick={handleClick}
        onMouseMove={handleMouseMove}
        onMouseLeave={() => setHoveredFlight(null)}
        style={{ cursor: 'crosshair' }}
      />
      <div className="radar-label">
        <span className="location">LOS ANGELES TRACON</span>
        <span className="coords">{CENTER_LAT.toFixed(4)}°N {Math.abs(CENTER_LON).toFixed(4)}°W</span>
      </div>
    </div>
  );
}
