import { useState, useEffect, useRef } from 'react';
import axios from 'axios';

const USER_ID = '9911';
// In Next.js, we use NEXT_PUBLIC_ for env vars exposed to client. 
// Fallback to RN defaults for now.
const WS_URL = process.env.NEXT_PUBLIC_WS_URL || 'wss://server-iot-qbyte.qbyte.web.id/ws';
const API_URL = process.env.NEXT_PUBLIC_API_BASE || 'https://hydroponik.qbyte.web.id';

export interface PhHistoryItem {
  id: number;
  value: string;
  created_at: string;
}

export function useHydroponics() {
  const [isOnline, setIsOnline] = useState(false);
  const [wsConnected, setWsConnected] = useState(false);
  
  const [ph, setPh] = useState<number | null>(null);
  const [mode, setMode] = useState<'otomatis' | 'manual'>('manual');
  const [threshold, setThreshold] = useState<string>('6.5');
  const [relay1, setRelay1] = useState(false);
  const [relay2, setRelay2] = useState(false);
  
  const [realtimeHistory, setRealtimeHistory] = useState<PhHistoryItem[]>([]);
  
  const ws = useRef<WebSocket | null>(null);
  const timeoutRef = useRef<NodeJS.Timeout | null>(null);
  const ignoreWsStateUntil = useRef<number>(0);

  const fetchInitialState = async () => {
    try {
      const relayRes = await axios.get(`${API_URL}/api/relay?user=${USER_ID}`);
      if (relayRes.data?.success && relayRes.data.data) {
        const d = relayRes.data.data;
        setRelay1(d.relay1_status === 1 || d.relay1_status === true || d.relay1_status === '1');
        setRelay2(d.relay2_status === 1 || d.relay2_status === true || d.relay2_status === '1');
        if (d.mode) setMode(d.mode === 'otomatis' ? 'otomatis' : 'manual');
      }
    } catch (e) {
      console.error('fetchInitialState (relay):', e);
    }
    
    try {
      const trRes = await axios.get(`${API_URL}/api/tr?user=${USER_ID}`);
      if (trRes.data?.success && trRes.data.data) {
        const t = trRes.data.data.threshold;
        if (t !== undefined && t !== null) setThreshold(String(t));
      }
    } catch (e) {
      console.error('fetchInitialState (threshold):', e);
    }
    ignoreWsStateUntil.current = Date.now() + 8000;
  };

  const fetchInitialRealtimeHistory = async () => {
    try {
      const res = await axios.get(`${API_URL}/api/ph?days=1&user=${USER_ID}`);
      if (res.data?.success && res.data.data.length > 0) {
        const hist = res.data.data.reverse() as PhHistoryItem[];
        setRealtimeHistory(hist.slice(-10));
        setPh(prev => prev === null ? parseFloat(hist[hist.length - 1].value) : prev);
      }
    } catch (e) {
      console.error('fetchInitialRealtimeHistory:', e);
    }
  };

  const connectWebSocket = () => {
    ws.current = new WebSocket(WS_URL);
    ws.current.onopen = () => {
      setWsConnected(true);
      const topics = [
        `data/ph/user/${USER_ID}`,
        `data/mode/user/${USER_ID}`,
        `data/treshold/user/${USER_ID}`,
        `data/relay1/user/${USER_ID}`,
        `data/relay2/user/${USER_ID}`,
      ];
      topics.forEach(t => ws.current?.send(JSON.stringify({ action: 'subscribe', topic: t })));
    };
    ws.current.onmessage = (e) => {
      e.data.split('\\n').forEach((raw: string) => {
        if (!raw.trim()) return;
        try {
          const msg = JSON.parse(raw);
          if (msg.action === 'publish' || msg.topic) handleWsMessage(msg.topic, msg.payload);
        } catch (_) {}
      });
    };
    ws.current.onclose = () => {
      setWsConnected(false);
      setIsOnline(false);
      setTimeout(connectWebSocket, 5000);
    };
  };

  const handleWsMessage = (topic: string, payload: any) => {
    const inGracePeriod = Date.now() < ignoreWsStateUntil.current;
    switch (topic) {
      case `data/ph/user/${USER_ID}`: {
        const val = typeof payload === 'object'
          ? (payload.pH ?? payload.sensor1 ?? parseFloat(payload))
          : parseFloat(payload);
        if (!isNaN(val)) {
          setPh(val);
          setIsOnline(true);
          if (timeoutRef.current) clearTimeout(timeoutRef.current);
          timeoutRef.current = setTimeout(() => setIsOnline(false), 10_000);
        }
        break;
      }
      case `data/mode/user/${USER_ID}`:
        if (!inGracePeriod) setMode(payload === 'otomatis' ? 'otomatis' : 'manual');
        break;
      case `data/relay1/user/${USER_ID}`:
        if (!inGracePeriod)
          setRelay1(payload === true || payload === 'true' || payload === '1' || payload === 1);
        break;
      case `data/relay2/user/${USER_ID}`:
        if (!inGracePeriod)
          setRelay2(payload === true || payload === 'true' || payload === '1' || payload === 1);
        break;
      case `data/treshold/user/${USER_ID}`:
        if (!inGracePeriod)
          setThreshold(typeof payload === 'object' ? (payload.threshold ?? '6.5') : String(payload));
        break;
    }
  };

  const publish = (topic: string, payload: any) => {
    if (ws.current && wsConnected)
      ws.current.send(JSON.stringify({ action: 'publish', topic, payload }));
    else
      alert('Koneksi Terputus: WebSocket offline — instruksi tidak dapat dikirim.');
  };

  useEffect(() => {
    fetchInitialState().then(() => connectWebSocket());
    fetchInitialRealtimeHistory();
    const id = setInterval(fetchInitialRealtimeHistory, 60_000);
    return () => {
      clearInterval(id);
      ws.current?.close();
      if (timeoutRef.current) clearTimeout(timeoutRef.current);
    };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const toggleMode = async () => {
    const next = mode === 'otomatis' ? 'manual' : 'otomatis';
    setMode(next);
    if (next === 'otomatis') {
      try {
        const res = await axios.get(`${API_URL}/api/tr?user=${USER_ID}`);
        if (res.data?.success && res.data.data) {
          const t = res.data.data.threshold;
          if (t !== undefined && t !== null && String(t).trim() !== '') {
            setThreshold(String(t));
          }
        }
      } catch (e) {
        console.error('toggleMode fetch threshold:', e);
      }
    }
    publish(`data/mode/user/${USER_ID}`, next);
  };

  const toggleRelay = (id: 1 | 2) => {
    if (mode === 'otomatis') {
      alert('Mode Otomatis Aktif: Nonaktifkan mode otomatis untuk mengontrol relay secara manual.');
      return;
    }
    const next = id === 1 ? !relay1 : !relay2;
    publish(`data/relay${id}/user/${USER_ID}`, next ? '1' : '0');
    id === 1 ? setRelay1(next) : setRelay2(next);
  };

  const updateThreshold = () => {
    publish(`data/treshold/user/${USER_ID}`, threshold);
  };

  return {
    isOnline,
    ph,
    mode,
    threshold,
    setThreshold,
    relay1,
    relay2,
    realtimeHistory,
    toggleMode,
    toggleRelay,
    updateThreshold,
    USER_ID,
    API_URL
  };
}
