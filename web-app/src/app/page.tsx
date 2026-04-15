'use client';

import { useState, useEffect } from 'react';
import { useHydroponics } from '@/hooks/useHydroponics';
import { Line } from 'react-chartjs-2';
import {
  Chart as ChartJS, CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Filler,
} from 'chart.js';
import { LayoutGrid, List, Activity, TrendingUp, Droplets, Wind, Calendar, Database, DownloadCloud } from 'lucide-react';
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
    if (v === null) return { label: '–', color: 'var(--hint)' };
    if (v < 5.5) return { label: 'Terlalu Asam', color: 'var(--danger)' };
    if (v > 7.5) return { label: 'Terlalu Basa', color: 'var(--warn)' };
    return { label: 'Optimal', color: 'var(--success)' };
  };

  const status = phStatus(h.ph);

  const fetchHistoryByDate = async (date: string) => {
    setHistoryLoading(true);
    try {
      console.log(`${h.API_URL}/api/ph?days=${date}&user=${h.USER_ID}`);
      const res = await axios.get(`${h.API_URL}/api/ph?days=${date}&user=${h.USER_ID}`);
      if (res.data?.success) setHistoryData(res.data.data.reverse());
      else setHistoryData([]);

      console.log(res.data);
    } catch (e) {
      //alert('Gagal mengambil data riwayat.');
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

  const downloadData = () => {
    window.location.href = `${h.API_URL}/api/download/ph?user=${h.USER_ID}&days=7`;
  };

  const labels = h.realtimeHistory.map(d => {
    const t = new Date(d.created_at);
    return `${t.getHours()}:${String(t.getMinutes()).padStart(2, '0')}`;
  });
  
  const step = Math.max(1, Math.floor(labels.length / 5));
  
  const chartData = {
    labels: labels.length ? labels.filter((_, i) => i % step === 0) : [''],
    datasets: [
      {
        data: h.realtimeHistory.map(d => parseFloat(d.value)),
        borderColor: 'rgba(62, 122, 74, 1)',
        backgroundColor: 'rgba(62, 122, 74, 0.1)',
        tension: 0.4,
        pointRadius: h.realtimeHistory.length <= 20 ? 4 : 0,
        pointBackgroundColor: '#FFFFFF',
        pointBorderColor: 'var(--p700)',
        pointBorderWidth: 2,
        fill: true,
      },
    ],
  };

  const chartOptions = {
    responsive: true,
    plugins: { legend: { display: false }, tooltip: { mode: 'index' as const, intersect: false } },
    scales: { 
      x: { display: false }, 
      y: { display: false, min: 0, max: 14 } 
    },
    maintainAspectRatio: false
  };

  return (
    <>
      {/* ── App Bar ── */}
      <div className="app-bar">
        <div>
          <div className="app-bar-brand">IoT</div>
          <div className="app-bar-sub">Hydroponics Control</div>
        </div>
        <div className="pill">
          <div className="dot" style={{ backgroundColor: h.isOnline ? 'var(--success)' : 'var(--danger)' }}></div>
          <div className="pill-text" style={{ color: h.isOnline ? 'var(--success)' : 'var(--danger)' }}>
            {h.isOnline ? 'Online' : 'Offline'}
          </div>
        </div>
      </div>

      {/* ── Tab Bar ── */}
      <div className="tab-bar">
        <button className={`tab-btn ${activeTab === 'dashboard' ? 'active' : ''}`} onClick={() => setActiveTab('dashboard')}>
          <LayoutGrid size={16} /> Dashboard
        </button>
        <button className={`tab-btn ${activeTab === 'history' ? 'active' : ''}`} onClick={() => setActiveTab('history')}>
          <List size={16} /> History
        </button>
      </div>

      <div className="scroll">
        {activeTab === 'dashboard' ? (
          <>
            <div className="desktop-grid">
              
              {/* Row 1 */}
              <div className="col-span-4">
                <div className="card" style={{ height: '100%' }}>
                  <div className="card-row">
                    <div className="icon-box"><Activity size={18} /></div>
                    <div className="card-title">Live pH Sensor</div>
                    <div className="badge" style={{ backgroundColor: `${status.color}22`, color: status.color }}>
                      {status.label}
                    </div>
                  </div>
                  <div className="ph-box">
                    <div className="ph-num">{h.ph !== null ? h.ph.toFixed(2) : '--'}</div>
                    <div className="ph-unit">Potential of Hydrogen</div>
                  </div>
                </div>
              </div>

              <div className="col-span-8">
                <div className="card" style={{ height: '100%' }}>
                  <div className="card-row">
                    <div className="icon-box"><TrendingUp size={18} /></div>
                    <div className="card-title">Realtime Tracker</div>
                  </div>
                  <div style={{ height: 200, width: '100%', flex: 1 }}>
                    {h.realtimeHistory.length > 0 ? (
                      <Line data={chartData} options={chartOptions as any} />
                    ) : (
                      <div className="empty-state">
                        <div className="empty-text">Loading chart...</div>
                      </div>
                    )}
                  </div>
                </div>
              </div>
            </div>

            <div className="section-label">PENGATURAN & KONTROL SISTEM</div>

            <div className="desktop-grid" style={{ alignItems: 'normal' }}>
              
              {/* Auto / Manual Mode */}
              <div className="col-span-6">
                <div className="card" style={{ height: '100%' }}>
                  <div className="card-row" style={{ marginBottom: 0 }}>
                    <div style={{ flex: 1 }}>
                      <div className="setting-name">Mode Otomatis</div>
                      <div className="setting-desc">Sistem mengelola nutrisi & pH secara cerdas</div>
                    </div>
                    <label className="switch-wrapper">
                      <input type="checkbox" checked={h.mode === 'otomatis'} onChange={h.toggleMode} />
                      <span className="switch-slider"></span>
                    </label>
                  </div>

                  {h.mode === 'otomatis' && (
                    <div className="threshold-section">
                      <div className="threshold-label">Target pH Threshold</div>
                      <div className="threshold-row">
                        <input 
                          type="number" 
                          step="0.1"
                          className="input-field" 
                          value={h.threshold} 
                          onChange={(e) => h.setThreshold(e.target.value)}
                          placeholder="mis. 6.5" 
                        />
                        <button className="btn-primary" onClick={h.updateThreshold}>Update</button>
                      </div>
                    </div>
                  )}
                </div>
              </div>

              {/* Relay Grid */}
              <div className="col-span-6">
                <div className="relay-grid">
                  <button 
                    className={`relay-card ${h.relay1 ? 'active' : ''}`} 
                    onClick={() => h.toggleRelay(1)}
                  >
                    <div className="relay-icon-circle">
                      <Droplets size={24} color={h.relay1 ? '#FFFFFF' : 'var(--p700)'} />
                    </div>
                    <div className="relay-title">Pompa Asam</div>
                    <div className="relay-status-row">
                      <div className="dot" style={{ backgroundColor: h.relay1 ? 'var(--success)' : 'var(--hint)' }}></div>
                      <div className="relay-status" style={{ color: h.relay1 ? 'var(--success)' : 'var(--hint)' }}>
                        {h.relay1 ? 'Berjalan' : 'Berhenti'}
                      </div>
                    </div>
                  </button>

                  <button 
                    className={`relay-card ${h.relay2 ? 'active' : ''}`} 
                    onClick={() => h.toggleRelay(2)}
                  >
                    <div className="relay-icon-circle">
                      <Wind size={24} color={h.relay2 ? '#FFFFFF' : 'var(--p700)'} />
                    </div>
                    <div className="relay-title">Pompa Basa</div>
                    <div className="relay-status-row">
                      <div className="dot" style={{ backgroundColor: h.relay2 ? 'var(--success)' : 'var(--hint)' }}></div>
                      <div className="relay-status" style={{ color: h.relay2 ? 'var(--success)' : 'var(--hint)' }}>
                        {h.relay2 ? 'Berjalan' : 'Berhenti'}
                      </div>
                    </div>
                  </button>
                </div>
              </div>
            </div>
          </>
        ) : (
          <>
            <div className="history-controls">
              <div className="filter-row">
                <button className={`filter-chip ${selectedDate === new Date(new Date().getTime() - new Date().getTimezoneOffset() * 60000).toISOString().slice(0, 10) ? 'active' : ''}`} onClick={goToday}>
                  Hari Ini
                </button>
                <button className="filter-chip" onClick={goYesterday}>
                  Kemarin
                </button>
                <button className="filter-icon-btn">
                  <Calendar size={18} />
                  <input type="date" className="native-date" value={selectedDate} onChange={(e) => setSelectedDate(e.target.value)} />
                </button>
              </div>

              <div className="date-pill">
                <Calendar size={15} strokeWidth={3} />
                Data Tanggal: {selectedDate}
              </div>
            </div>

            {/* Data Table */}
            <div className="card table-card">
              <div className="table-header">
                <div className="icon-box" style={{ marginRight: 14 }}><Database size={18} /></div>
                <div className="card-title" style={{ fontSize: 18 }}>Data Records Tabel</div>
                {historyData.length > 0 && (
                  <div className="badge" style={{ backgroundColor: 'var(--p500)', color: '#fff', marginLeft: 'auto', fontSize: 13, padding: '6px 14px' }}>
                    {historyData.length} entri ditemukan
                  </div>
                )}
              </div>

              {historyLoading ? (
                <div className="empty-state" style={{ margin: '40px 0' }}>Loading data riwayat...</div>
              ) : historyData.length === 0 ? (
                <div className="empty-state">
                  <Database size={48} color="var(--line)"/>
                  <div className="empty-text">Tidak ada rekaman data pada tanggal ini</div>
                </div>
              ) : (
                <>
                  <div className="table-head">
                    <div className="th">Waktu Eksekusi</div>
                    <div className="th">Nilai pH Re-kalkulasi</div>
                    <div className="th">Status Asiditas</div>
                  </div>
                  {historyData.map((item, idx) => {
                    const d = new Date(item.created_at);
                    const time = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
                    const val = parseFloat(item.value);
                    const st = phStatus(val);
                    return (
                      <div className={`table-row ${idx % 2 === 1 ? 'table-row-alt' : ''}`} key={idx}>
                        <div className="td">{time} WIB</div>
                        <div className="td" style={{ fontWeight: 700, color: 'var(--p700)' }}>{val.toFixed(2)}</div>
                        <div style={{ flex: 1 }}>
                          <span className="badge" style={{ backgroundColor: `${st.color}22`, color: st.color }}>
                            {st.label}
                          </span>
                        </div>
                      </div>
                    );
                  })}
                </>
              )}
            </div>

            {/* Download Button */}
            <button className="btn-download" onClick={downloadData}>
              <DownloadCloud size={20} /> Unduh File Rekap Data (.csv / .xlsx)
            </button>
          </>
        )}
      </div>
    </>
  );
}
