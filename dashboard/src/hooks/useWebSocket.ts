/**
 * WebSocket Hook - Real-Time Connection Management
 *
 * Handles WebSocket connection to the C++ backend with:
 * - Automatic reconnection with exponential backoff
 * - Message parsing and dispatch to store
 * - Connection status tracking
 *
 * For interview: "The hook manages the WebSocket lifecycle - connecting,
 * reconnecting on failure, and dispatching messages to the Zustand store.
 * I used exponential backoff for reconnection to avoid hammering the server."
 */

import { useEffect, useRef, useCallback } from 'react';
import { useFlightStore } from '../stores/flightStore';
import type { WebSocketMessage } from '../types';

const WS_URL = 'ws://localhost:8080';
const RECONNECT_BASE_DELAY = 1000;
const RECONNECT_MAX_DELAY = 30000;

export function useWebSocket() {
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimeoutRef = useRef<number | null>(null);

  const {
    setFlights,
    addAlert,
    removeAlert,
    setMetrics,
    setConnected,
    incrementReconnectAttempts,
    resetReconnectAttempts,
    connection,
  } = useFlightStore();

  const connect = useCallback(() => {
    // Clean up existing connection
    if (wsRef.current) {
      wsRef.current.close();
    }

    console.log('[WebSocket] Connecting to', WS_URL);
    const ws = new WebSocket(WS_URL);

    ws.onopen = () => {
      console.log('[WebSocket] Connected');
      setConnected(true);
      resetReconnectAttempts();
    };

    ws.onmessage = (event) => {
      try {
        const message: WebSocketMessage = JSON.parse(event.data);

        switch (message.type) {
          case 'flights':
            setFlights(message.data);
            break;

          case 'alert':
            addAlert(message.data);
            break;

          case 'resolved':
            removeAlert(message.data.flight1, message.data.flight2);
            break;

          case 'metrics':
            setMetrics(message.data);
            break;

          default:
            console.warn('[WebSocket] Unknown message type:', message);
        }
      } catch (err) {
        console.error('[WebSocket] Failed to parse message:', err);
      }
    };

    ws.onclose = (event) => {
      console.log('[WebSocket] Disconnected:', event.code, event.reason);
      setConnected(false);
      scheduleReconnect();
    };

    ws.onerror = (error) => {
      console.error('[WebSocket] Error:', error);
    };

    wsRef.current = ws;
  }, [setFlights, addAlert, removeAlert, setMetrics, setConnected, resetReconnectAttempts]);

  const scheduleReconnect = useCallback(() => {
    incrementReconnectAttempts();

    // Exponential backoff with max delay
    const attempts = useFlightStore.getState().connection.reconnectAttempts;
    const delay = Math.min(
      RECONNECT_BASE_DELAY * Math.pow(2, attempts),
      RECONNECT_MAX_DELAY
    );

    console.log(`[WebSocket] Reconnecting in ${delay}ms (attempt ${attempts + 1})`);

    reconnectTimeoutRef.current = window.setTimeout(() => {
      connect();
    }, delay);
  }, [connect, incrementReconnectAttempts]);

  // Connect on mount, cleanup on unmount
  useEffect(() => {
    connect();

    return () => {
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
      }
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, [connect]);

  return {
    connected: connection.connected,
    reconnectAttempts: connection.reconnectAttempts,
  };
}
