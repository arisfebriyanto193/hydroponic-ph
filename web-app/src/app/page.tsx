'use client';

import { useState, useEffect } from 'react';
import { useHydroponics } from '@/hooks/useHydroponics';
import { Line } from 'react-chartjs-2';
import {
  Chart as ChartJS, CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Filler,
} from 'chart.js';
import { LayoutGrid, List, Plus, Activity, Droplets, Wind, CalendarDays, Download, Server } from 'lucide-react';
import axios from 'axios';

ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Filler);

export default function App() {
  const [activeTab, setActiveTab] = useState<'dashboard' | 'history'>('dashboard');
  
  const h = useHydroponics();
  
  const [selectedDate, setSelectedDate] = useState<string>(
    new Date(new Date().getTime() - new Date().getTimezoneOffset() * 60000).toISOString().slice(0, 10)
  );
  const [historyData, setHistoryData] = useState<any[]>([]);
  const [historyLoading, setHistoryLoading] = useState(false);

  const phStatus = (v: number | null) => {
    if (v === null) return { label: 'Menunggu', color: 'var(--text-muted)' };
    if (v < 5.5) return { label: 'Terlalu Asam', color: 'var(--danger)' };
    if (v > 7.5) return { label: 'Terlalu Basa', color: 'var(--warn)' };
    return { label: 'Optimal', color: 'var(--success)' };
  };
  const status = phStatus(h.ph);

  const fetchHistoryByDate = async (date: string) => {
    setHistoryLoading(true);
    try {
      console.log(`${h.API_URL}/api/ph?date=${date}&user=${h.USER_ID}`);
      const res = await axios.get(`${h.API_URL}/api/ph?date=${date}&user=${h.USER_ID}`);
      if (res.data?.success) setHistoryData(res.data.data.reverse());
      else setHistoryData([]);


      console.log(res.data);
    } catch (e) {
      alert('Gagal mengambil data riwayat.');
    } finally {
      setHistoryLoading(false);
    }
  };

  useEffect(() => {
    if (activeTab === 'history') fetchHistoryByDate(selectedDate);
  }, [activeTab, selectedDate]);

  const goYesterday = () => {
    const y = new Date();
    y.setDate(y.getDate() - 1);
    setSelectedDate(new Date(y.getTime() - y.getTimezoneOffset() * 60000).toISOString().slice(0, 10));
  };
  const goToday = () => {
    setSelectedDate(new Date(new Date().getTime() - new Date().getTimezoneOffset() * 60000).toISOString().slice(0, 10));
  };

  const chartData = {
    labels: h.realtimeHistory.map(d => {
      const t = new Date(d.created_at);
      return `${t.getHours()}:${String(t.getMinutes()).padStart(2, '0')}`;
    }).slice(-10),
    datasets: [
      {
        fill: true,
        data: h.realtimeHistory.map(d => parseFloat(d.value)).slice(-10),
        borderColor: '#3b82f6',
        backgroundColor: 'rgba(59, 130, 246, 0.1)',
        tension: 0.4,
        pointRadius: 4,
        pointBackgroundColor: '#fff',
        pointBorderColor: '#3b82f6',
        pointBorderWidth: 2,
      },
    ],
  };

  const chartOptions = {
    responsive: true,
    plugins: { legend: { display: false }, tooltip: { mode: 'index' as const, intersect: false } },
    scales: { x: { display: false }, y: { display: false } },
    maintainAspectRatio: false
  };

  return (
    <>
      <div className="sidebar">
        <div className="sidebar-header">
          <div className="brand-title">AquaLink</div>
          <div className="brand-subtitle">Hydroponics Sys</div>
        </div>
        <div className="nav-menu">
          <button className={`nav-item ${activeTab === 'dashboard' ? 'active' : ''}`} onClick={() => setActiveTab('dashboard')}>
            <LayoutGrid size={20} />
            <span>Dashboard</span>
          </button>
          <button className={`nav-item ${activeTab === 'history' ? 'active' : ''}`} onClick={() => setActiveTab('history')}>
            <List size={20} />
            <span>Riwayat Data</span>
          </button>
        </div>
      </div>

      <div className="main-content">
        <div className="topbar">
          <div className="page-title">{activeTab === 'dashboard' ? 'Overview' : 'Riwayat Data'}</div>
          <div className="status-indicator">
            <div className={`dot ${h.isOnline ? 'glow-success' : 'glow-danger'}`}></div>
            <span>{h.isOnline ? 'System Online' : 'System Offline'}</span>
          </div>
        </div>

        <div className="content-inner">
          {activeTab === 'dashboard' ? (
            <div className="bento-grid">
              
              {/* PH Monitor */}
              <div className="bento-card col-span-1">
                <div className="card-header">
                  <div className="card-title-group">
                    <div className="card-icon"><Activity size={20} /></div>
                    <div className="card-title">Live pH Sensor</div>
                  </div>
                  <div className="badge" style={{ backgroundColor: `${status.color}22`, color: status.color, border: `1px solid ${status.color}40` }}>
                    {status.label}
                  </div>
                </div>
                <div className="ph-display">
                  <div className="ph-value">{h.ph !== null ? h.ph.toFixed(2) : '--'}</div>
                  <div className="ph-label">Potential of Hydrogen</div>
                </div>
              </div>

              {/* Chart Tracker */}
              <div className="bento-card col-span-2">
                <div className="card-header">
                  <div className="card-title-group">
                    <div className="card-icon"><Server size={20} /></div>
                    <div className="card-title">Realtime Tracker</div>
                  </div>
                </div>
                <div style={{ height: 180, width: '100%', position: 'relative' }}>
                  {h.realtimeHistory.length > 0 ? (
                    <Line data={chartData} options={chartOptions} />
                  ) : (
                    <div className="empty-state">Loading chart...</div>
                  )}
                </div>
              </div>

              {/* Mode Settings */}
              <div className="bento-card col-span-1">
                <div className="card-header">
                   <div className="card-title">Sistem Kontrol</div>
                </div>
                <div className="switch-container">
                  <div className="switch-info">
                    <h3>Mode Otomatis</h3>
                    <p>Sistem regulasi nutrisi otonom</p>
                  </div>
                  <input type="checkbox" className="toggle-input" checked={h.mode === 'otomatis'} onChange={h.toggleMode} />
                </div>
                
                {h.mode === 'otomatis' && (
                  <div className="threshold-box">
                    <div style={{fontSize: '0.9rem', fontWeight: 600}}>Target Threshold pH</div>
                    <div className="input-group">
                      <input 
                        type="number" 
                        step="0.1"
                        className="text-input" 
                        value={h.threshold} 
                        onChange={(e) => h.setThreshold(e.target.value)}
                        placeholder="mis. 6.5" 
                      />
                      <button className="btn" onClick={h.updateThreshold}>Simpan</button>
                    </div>
                  </div>
                )}
              </div>

              {/* Relays */}
              <div className="bento-card col-span-2">
                <div className="card-header">
                  <div className="card-title">Aksi Pompa Manual</div>
                </div>
                <div className="relay-container">
                  <button 
                    className={`relay-btn ${h.relay1 ? 'active' : ''}`} 
                    onClick={() => h.toggleRelay(1)}
                    disabled={h.mode === 'otomatis'}
                  >
                    <div className="icon-wrap"><Droplets size={24} /></div>
                    <div style={{ textAlign: 'center' }}>
                      <div className="relay-name">Pompa Asam</div>
                      <div className="relay-status">{h.relay1 ? 'Berjalan' : 'Berhenti'}</div>
                    </div>
                  </button>

                  <button 
                    className={`relay-btn ${h.relay2 ? 'active' : ''}`} 
                    onClick={() => h.toggleRelay(2)}
                    disabled={h.mode === 'otomatis'}
                  >
                    <div className="icon-wrap"><Wind size={24} /></div>
                    <div style={{ textAlign: 'center' }}>
                      <div className="relay-name">Pompa Basa</div>
                      <div className="relay-status">{h.relay2 ? 'Berjalan' : 'Berhenti'}</div>
                    </div>
                  </button>
                </div>
              </div>

            </div>
          ) : (
            <div className="bento-card col-span-3" style={{ minHeight: '600px' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 24 }}>
                <div className="filter-bar" style={{ margin: 0 }}>
                  <button className={`chip ${selectedDate === new Date(new Date().getTime() - new Date().getTimezoneOffset() * 60000).toISOString().slice(0, 10) ? 'active' : ''}`} onClick={goToday}>
                    Hari Ini
                  </button>
                  <button className="chip" onClick={goYesterday}>Kemarin</button>
                  <input type="date" className="native-date" value={selectedDate} onChange={(e) => setSelectedDate(e.target.value)} />
                </div>
                
                <button className="btn" style={{ padding: '12px 24px', display: 'flex', alignItems: 'center', gap: 10 }} onClick={() => window.location.href = `${h.API_URL}/api/download/ph?user=${h.USER_ID}&days=7`}>
                  <Download size={18} /> Ekspor Data
                </button>
              </div>

              {historyLoading ? (
                <div className="empty-state" style={{ marginTop: 100 }}>Loading...</div>
              ) : historyData.length === 0 ? (
                <div className="empty-state" style={{ marginTop: 100 }}>
                  <CalendarDays size={48} style={{ opacity: 0.3 }} />
                  <p>Tidak ada data pada {selectedDate}</p>
                </div>
              ) : (
                <div style={{ overflowX: 'auto' }}>
                  <table className="data-table">
                    <thead>
                      <tr>
                        <th>Waktu (WIB)</th>
                        <th>Nilai pH</th>
                        <th>Status Asiditas</th>
                      </tr>
                    </thead>
                    <tbody>
                      {historyData.map((item, idx) => {
                        const d = new Date(item.created_at);
                        const time = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
                        const val = parseFloat(item.value);
                        const st = phStatus(val);
                        return (
                          <tr key={idx}>
                            <td>{time}</td>
                            <td style={{ color: 'var(--accent-light)', fontWeight: 700 }}>{val.toFixed(2)}</td>
                            <td>
                              <span className="badge" style={{ backgroundColor: `${st.color}22`, color: st.color, border: `1px solid ${st.color}40`, display: 'inline-block' }}>
                                {st.label}
                              </span>
                            </td>
                          </tr>
                        );
                      })}
                    </tbody>
                  </table>
                </div>
              )}
            </div>
          )}
        </div>
      </div>
    </>
  );
}
