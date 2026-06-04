'use client';

import { useState, useEffect } from 'react';
import { useHydroponics } from '@/hooks/useHydroponics';
import { Line } from 'react-chartjs-2';
import {
  Chart as ChartJS, CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Filler,
} from 'chart.js';
import { LayoutGrid, List, Activity, TrendingUp, Droplets, Wind, Calendar, Database, DownloadCloud, ChevronLeft, ChevronRight, ChevronDown, Gauge } from 'lucide-react';
import axios from 'axios';

ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Filler);

function CalendarPopup({ selectedDate, availableDates, onSelect, onClose }: { selectedDate: string, availableDates: string[], onSelect: (date: string) => void, onClose: () => void }) {
  const [currentMonth, setCurrentMonth] = useState(new Date(selectedDate));
  
  const getDaysInMonth = (year: number, month: number) => new Date(year, month + 1, 0).getDate();
  const getFirstDayOfMonth = (year: number, month: number) => new Date(year, month, 1).getDay();

  const year = currentMonth.getFullYear();
  const month = currentMonth.getMonth();
  
  const daysInMonth = getDaysInMonth(year, month);
  const firstDay = getFirstDayOfMonth(year, month);
  
  const days = [];
  for (let i = 0; i < firstDay; i++) days.push(null);
  for (let i = 1; i <= daysInMonth; i++) days.push(i);

  const prevMonth = (e: any) => { e.stopPropagation(); setCurrentMonth(new Date(year, month - 1, 1)); };
  const nextMonth = (e: any) => { e.stopPropagation(); setCurrentMonth(new Date(year, month + 1, 1)); };

  const monthNames = ["Januari", "Februari", "Maret", "April", "Mei", "Juni", "Juli", "Agustus", "September", "Oktober", "November", "Desember"];

  return (
    <div style={{ position: 'absolute', top: 'calc(100% + 8px)', right: 0, zIndex: 100, backgroundColor: 'white', border: '1px solid var(--line)', borderRadius: '12px', padding: '16px', boxShadow: '0 8px 24px rgba(0,0,0,0.12)', width: '300px', color: '#333' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '16px' }}>
        <button onClick={prevMonth} style={{ background: 'var(--bg)', border: '1px solid var(--line)', borderRadius: '6px', cursor: 'pointer', padding: '4px', display: 'flex', alignItems: 'center', justifyContent: 'center' }}><ChevronLeft size={16}/></button>
        <div style={{ fontWeight: 'bold', fontSize: '15px' }}>{monthNames[month]} {year}</div>
        <button onClick={nextMonth} style={{ background: 'var(--bg)', border: '1px solid var(--line)', borderRadius: '6px', cursor: 'pointer', padding: '4px', display: 'flex', alignItems: 'center', justifyContent: 'center' }}><ChevronRight size={16}/></button>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(7, 1fr)', gap: '4px', textAlign: 'center', marginBottom: '8px', fontSize: '12px', color: 'var(--hint)', fontWeight: 600 }}>
        <div>Min</div><div>Sen</div><div>Sel</div><div>Rab</div><div>Kam</div><div>Jum</div><div>Sab</div>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(7, 1fr)', gap: '4px' }}>
        {days.map((day, idx) => {
          if (!day) return <div key={idx} />;
          
          const dateStr = `${year}-${String(month + 1).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
          const isSelected = dateStr === selectedDate;
          const hasData = availableDates.includes(dateStr);
          
          let bgColor = 'transparent';
          let color = '#333';
          let border = '1px solid transparent';
          
          if (isSelected) {
            bgColor = 'var(--p500)';
            color = 'white';
          } else if (hasData) {
            bgColor = 'rgba(62, 122, 74, 0.15)'; // Hijau terang
            color = 'var(--p700)';
            border = '1px solid var(--p500)';
          } else {
            color = '#888';
          }

          return (
            <button 
              key={idx}
              onClick={(e) => { e.stopPropagation(); onSelect(dateStr); onClose(); }}
              style={{
                background: bgColor,
                color: color,
                border: border,
                borderRadius: '6px',
                padding: '8px 0',
                cursor: 'pointer',
                fontSize: '14px',
                fontWeight: isSelected ? 'bold' : 'normal',
                transition: 'all 0.2s ease'
              }}
              onMouseOver={(e) => { if (!isSelected) e.currentTarget.style.background = hasData ? 'rgba(62, 122, 74, 0.25)' : '#f0f0f0'; }}
              onMouseOut={(e) => { if (!isSelected) e.currentTarget.style.background = bgColor; }}
            >
              {day}
            </button>
          );
        })}
      </div>
    </div>
  );
}

export default function App() {
  const [activeTab, setActiveTab] = useState<'dashboard' | 'history'>('dashboard');
  
  const h = useHydroponics();
  
  const [selectedDate, setSelectedDate] = useState<string>(
    new Date(new Date().getTime() - new Date().getTimezoneOffset() * 60000).toISOString().slice(0, 10)
  );
  const [historyData, setHistoryData] = useState<any[]>([]);
  const [historyLoading, setHistoryLoading] = useState(false);
  const [availableDates, setAvailableDates] = useState<string[]>([]);
  const [showCalendar, setShowCalendar] = useState(false);

  const phStatus = (v: number | null) => {
    if (v === null) return { label: '–', color: 'var(--hint)' };
    if (v < 5.5) return { label: 'Terlalu Asam', color: 'var(--danger)' };
    if (v > 7.5) return { label: 'Terlalu Basa', color: 'var(--warn)' };
    return { label: 'Optimal', color: 'var(--success)' };
  };

  const tdsStatus = (v: number | null) => {
    if (v === null) return { label: '–', color: 'var(--hint)', desc: 'Menunggu data sensor...' };
    if (v < 400)   return { label: 'Sangat Rendah', color: 'var(--danger)',  desc: 'Nutrisi sangat kurang' };
    if (v < 800)   return { label: 'Rendah',        color: 'var(--warn)',    desc: 'Tambahkan larutan nutrisi' };
    if (v < 1400)  return { label: 'Normal',         color: 'var(--success)', desc: 'Konsentrasi nutrisi optimal' };
    if (v < 2000)  return { label: 'Tinggi',         color: '#f59e0b',        desc: 'Pertimbangkan pengenceran' };
    return               { label: 'Sangat Tinggi',  color: 'var(--danger)',  desc: 'Segera encerkan larutan' };
  };

  const status = phStatus(h.ph);

  const fetchHistoryByDate = async (date: string) => {
    setHistoryLoading(true);
    try {
      const res = await axios.get(`${h.API_URL}/api/ph?date=${date}&user=${h.USER_ID}`);
      if (res.data?.success) setHistoryData(res.data.data.reverse());
      else setHistoryData([]);
    } catch (e) {
      //alert('Gagal mengambil data riwayat.');
    } finally {
      setHistoryLoading(false);
    }
  };

  useEffect(() => {
    if (activeTab === 'history') {
      axios.get(`${h.API_URL}/api/ph/dates?user=${h.USER_ID}`)
        .then(res => {
          if (res.data?.success) setAvailableDates(res.data.data);
        })
        .catch(console.error);
    }
  }, [activeTab, h.API_URL, h.USER_ID]);

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
        data: h.realtimeHistory.map(d => parseFloat(String(d.ph_value))),
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
              
              {/* Row 1: Sensors */}
              <div className="col-span-6">
                <div className="card sensor-card sensor-card-ph" style={{ height: '100%' }}>
                  <div className="card-row">
                    <div className="icon-box"><Activity size={20} /></div>
                    <div className="card-title">Live pH Sensor</div>
                    <div className="badge">
                      {status.label}
                    </div>
                  </div>
                  <div className="ph-box">
                    <div className="ph-num">{h.ph !== null ? h.ph.toFixed(2) : '--'}</div>
                    <div className="ph-unit">Potential of Hydrogen</div>
                  </div>
                </div>
              </div>

              {/* TDS Card */}
              <div className="col-span-6">
                {(() => {
                  const ts = tdsStatus(h.tds);
                  return (
                    <div className="card sensor-card sensor-card-tds" style={{ height: '100%' }}>
                      <div className="card-row">
                        <div className="icon-box"><Gauge size={20} /></div>
                        <div className="card-title">Live TDS Sensor</div>
                        <div className="badge">
                          {ts.label}
                        </div>
                      </div>
                      <div className="ph-box">
                        <div className="ph-num">
                          {h.tds !== null ? Math.round(h.tds) : '--'}
                        </div>
                        <div className="ph-unit">ppm &nbsp;·&nbsp; {ts.desc}</div>
                      </div>
                    </div>
                  );
                })()}
              </div>

              {/* Row 2: Tracker */}
              <div className="col-span-12">
                <div className="card" style={{ height: '100%' }}>
                  <div className="card-row">
                    <div className="icon-box"><TrendingUp size={18} /></div>
                    <div className="card-title">Realtime Tracker</div>
                  </div>
                  <div style={{ height: 260, width: '100%', flex: 1, marginTop: '10px' }}>
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
                    <div className="threshold-section" style={{ marginTop: 16 }}>
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

                  {h.mode === 'manual' && (
                    <div className="threshold-section" style={{ marginTop: 16 }}>
                      <div className="threshold-label">Notifikasi Email Peringatan</div>
                      <div className="setting-desc" style={{marginBottom: 12, fontSize: 12}}>Kirim peringatan jika pH di luar batas (hanya aktif di mode Manual).</div>
                      
                      <div className="threshold-row" style={{marginBottom: 12}}>
                        <div style={{flex: 1}}>
                          <div className="threshold-label" style={{fontSize: 11}}>Ambang Batas Peringatan (mis: 6.5 atau 6-7)</div>
                          <input 
                            type="text" 
                            className="input-field" 
                            value={h.threshold} 
                            onChange={(e) => h.setThreshold(e.target.value)}
                            placeholder="mis. 6.5" 
                          />
                        </div>
                        <div style={{display: 'flex', alignItems: 'flex-end'}}>
                          <button className="btn-primary" onClick={h.updateThreshold} style={{height: 38}}>Update Batas</button>
                        </div>
                      </div>

                      <div className="threshold-row">
                        <div style={{flex: 1}}>
                          <div className="threshold-label" style={{fontSize: 11}}>Email Penerima</div>
                          <input 
                            type="email" 
                            className="input-field" 
                            value={h.emailTujuan} 
                            onChange={(e) => h.setEmailTujuan(e.target.value)}
                            placeholder="email@contoh.com" 
                          />
                        </div>
                        <div style={{display: 'flex', alignItems: 'flex-end'}}>
                          <button className="btn-primary" onClick={h.updateEmailTujuan} style={{height: 38}}>Simpan Email</button>
                        </div>
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
            <div className="history-controls" style={{ display: 'flex', justifyContent: 'flex-start', marginBottom: '20px' }}>
              <div style={{ position: 'relative' }}>
                <button 
                  className="date-pill" 
                  onClick={() => setShowCalendar(!showCalendar)}
                  style={{ 
                    border: '1px solid var(--line)', 
                    cursor: 'pointer', 
                    display: 'flex', 
                    alignItems: 'center', 
                    gap: '10px', 
                    padding: '10px 20px', 
                    backgroundColor: '#ffffff', 
                    borderRadius: '8px', 
                    fontSize: '15px', 
                    fontWeight: 600,
                    color: 'var(--p700)',
                    boxShadow: '0 2px 8px rgba(0,0,0,0.04)',
                    transition: 'all 0.2s ease'
                  }}
                  onMouseOver={(e) => e.currentTarget.style.borderColor = 'var(--p500)'}
                  onMouseOut={(e) => e.currentTarget.style.borderColor = 'var(--line)'}
                >
                  <Calendar size={18} strokeWidth={2.5} color="var(--p500)" />
                  Pilih Tanggal: {new Date(selectedDate).toLocaleDateString('id-ID', { day: 'numeric', month: 'long', year: 'numeric' })}
                  <ChevronDown size={16} style={{ marginLeft: '4px', opacity: 0.6 }} />
                </button>
                {showCalendar && (
                  <>
                    <div style={{ position: 'fixed', top: 0, left: 0, right: 0, bottom: 0, zIndex: 99 }} onClick={() => setShowCalendar(false)}></div>
                    <CalendarPopup 
                      selectedDate={selectedDate} 
                      availableDates={availableDates} 
                      onSelect={(date) => { setSelectedDate(date); setShowCalendar(false); }} 
                      onClose={() => setShowCalendar(false)} 
                    />
                  </>
                )}
              </div>
            </div>

            <div className="card table-card">
              <div className="table-header">
                <div className="icon-box" style={{ marginRight: 14 }}><Database size={18} /></div>
                <div className="card-title" style={{ fontSize: 18 }}>Data Record Sensor</div>
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
                <div style={{ overflowX: 'auto' }}>
                  <div className="table-head">
                    <div className="th" style={{ minWidth: 100 }}>Waktu Eksekusi</div>
                    <div className="th" style={{ minWidth: 100 }}>Nilai pH</div>
                    <div className="th" style={{ minWidth: 140 }}>Status Asiditas</div>
                    <div className="th" style={{ minWidth: 100 }}>Nilai TDS</div>
                    <div className="th" style={{ minWidth: 140 }}>Status Nutrisi</div>
                  </div>
                  {historyData.map((item, idx) => {
                    const d = new Date(item.created_at);
                    const time = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
                    const phVal = item.ph_value != null ? parseFloat(String(item.ph_value)) : null;
                    const tdsVal = item.tds_value != null ? parseFloat(String(item.tds_value)) : null;
                    const stPh = phStatus(phVal);
                    const stTds = tdsStatus(tdsVal);
                    return (
                      <div className={`table-row ${idx % 2 === 1 ? 'table-row-alt' : ''}`} key={idx} style={{ flexWrap: 'nowrap', minWidth: 600 }}>
                        <div className="td" style={{ minWidth: 100 }}>{time} WIB</div>
                        <div className="td" style={{ minWidth: 100, fontWeight: 700, color: 'var(--p700)' }}>
                          {phVal !== null ? phVal.toFixed(2) : '--'}
                        </div>
                        <div className="td" style={{ minWidth: 140 }}>
                          <span className="badge" style={{ backgroundColor: `${stPh.color}22`, color: stPh.color }}>
                            {stPh.label}
                          </span>
                        </div>
                        <div className="td" style={{ minWidth: 100, fontWeight: 700, color: 'var(--p700)' }}>
                          {tdsVal !== null ? Math.round(tdsVal) + ' ppm' : '--'}
                        </div>
                        <div className="td" style={{ minWidth: 140 }}>
                          <span className="badge" style={{ backgroundColor: `${stTds.color}22`, color: stTds.color }}>
                            {stTds.label}
                          </span>
                        </div>
                      </div>
                    );
                  })}
                </div>
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
