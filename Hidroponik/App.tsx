import React, { useEffect, useState, useRef } from 'react';
import {
  StyleSheet, Text, View, Switch, ScrollView,
  TouchableOpacity, TextInput, ActivityIndicator,
  Alert, Linking, Platform
} from 'react-native';
import { StatusBar } from 'expo-status-bar';
import { LineChart } from 'react-native-chart-kit';
import { Dimensions } from 'react-native';
import axios from 'axios';
import { Feather } from '@expo/vector-icons';
import DateTimePicker from '@react-native-community/datetimepicker';

const screenWidth = Dimensions.get('window').width;

const WS_URL  = process.env.EXPO_PUBLIC_WS_URL   || 'wss://server-iot-qbyte.qbyte.web.id/ws';
const API_URL = process.env.EXPO_PUBLIC_API_BASE  || 'http://192.168.6.236:3000';
const USER_ID = '9911';

// ─── Color Palette ────────────────────────────────────────────────────────────
const C = {
  // backgrounds
  bg:          '#E8E4DC',   // warm parchment
  surface:     '#FFFFFF',
  surfaceAlt:  '#F4F1EA',   // slightly tinted white for alternating rows

  // primary green (rich, not neon)
  p900:  '#1A3526',
  p700:  '#2B5935',
  p500:  '#3E7A4A',
  p300:  '#6EA87A',
  p100:  '#C4DFC9',
  p50:   '#EAF2EB',

  // neutral
  ink:   '#1A2920',   // almost-black text
  muted: '#5A7060',   // secondary text
  hint:  '#8FA897',   // placeholder / caption
  line:  '#D0DDD4',   // border

  // semantic
  danger:  '#C94040',
  success: '#2D8F70',
  warn:    '#C07A2A',
};

// ─── Typography scale ─────────────────────────────────────────────────────────
// Using a single readable font stack. On iOS SF Pro is used, on Android Roboto.
// If you want a custom font, load it via expo-font and set fontFamily below.
const T = {
  xs:   { fontSize: 11, letterSpacing: 0.3 },
  sm:   { fontSize: 13 },
  base: { fontSize: 15 },
  lg:   { fontSize: 17 },
  xl:   { fontSize: 22 },
  h1:   { fontSize: 36, letterSpacing: -1 },
};

// ─── Small helpers ─────────────────────────────────────────────────────────────
function Dot({ color, size = 8 }: { color: string; size?: number }) {
  return <View style={{ width: size, height: size, borderRadius: size / 2, backgroundColor: color }} />;
}

function Badge({ label, color }: { label: string; color: string }) {
  return (
    <View style={{ backgroundColor: color + '22', borderRadius: 6, paddingHorizontal: 8, paddingVertical: 3 }}>
      <Text style={{ color, fontWeight: '700', fontSize: 11, letterSpacing: 0.4 }}>{label}</Text>
    </View>
  );
}

// ─── Main App ──────────────────────────────────────────────────────────────────
export default function App() {
  const [activeTab, setActiveTab] = useState<'dashboard' | 'history'>('dashboard');
  const [isOnline, setIsOnline]   = useState(false);

  // Dashboard
  const [ph,               setPh]               = useState<number | null>(null);
  const [mode,             setMode]             = useState<'otomatis' | 'manual'>('manual');
  const [threshold,        setThreshold]        = useState<string>('6.5');
  const [relay1,           setRelay1]           = useState(false);
  const [relay2,           setRelay2]           = useState(false);
  const [wsConnected,      setWsConnected]      = useState(false);
  const [realtimeHistory,  setRealtimeHistory]  = useState<any[]>([]);

  // History
  const [selectedDate,  setSelectedDate]  = useState<Date>(new Date());
  const [showPicker,    setShowPicker]    = useState(false);
  const [historyData,   setHistoryData]   = useState<any[]>([]);
  const [historyLoading,setHistoryLoading]= useState(false);

  const ws          = useRef<WebSocket | null>(null);
  const timeoutRef  = useRef<NodeJS.Timeout | null>(null);

  // ── WebSocket & initial fetch ───────────────────────────────────────────────
  useEffect(() => {
    fetchInitialRealtimeHistory();
    connectWebSocket();
    const id = setInterval(fetchInitialRealtimeHistory, 60_000);
    return () => {
      clearInterval(id);
      ws.current?.close();
      if (timeoutRef.current) clearTimeout(timeoutRef.current);
    };
  }, []);

  const fetchInitialRealtimeHistory = async () => {
    try {
      const res = await axios.get(`${API_URL}/api/ph?days=1&user=${USER_ID}`);
      if (res.data?.success && res.data.data.length > 0) {
        const hist = res.data.data.reverse();
        setRealtimeHistory(hist.slice(-10));
        setPh(prev => prev === null ? hist[hist.length - 1].value : prev);
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
      e.data.split('\n').forEach((raw: string) => {
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
    switch (topic) {
      case `data/ph/user/${USER_ID}`: {
        const val = typeof payload === 'object'
          ? (payload.pH ?? payload.sensor1 ?? parseFloat(payload))
          : parseFloat(payload);
        if (!isNaN(val)) {
          setPh(val);
          setIsOnline(true);
          setRealtimeHistory(prev => [...prev, { created_at: new Date().toISOString(), value: val }].slice(-10));
          if (timeoutRef.current) clearTimeout(timeoutRef.current);
          timeoutRef.current = setTimeout(() => setIsOnline(false), 10_000);
        }
        break;
      }
      case `data/mode/user/${USER_ID}`:
        setMode(payload === 'otomatis' ? 'otomatis' : 'manual');
        break;
      case `data/relay1/user/${USER_ID}`:
        setRelay1(payload === true || payload === 'true' || payload === '1' || payload === 1);
        break;
      case `data/relay2/user/${USER_ID}`:
        setRelay2(payload === true || payload === 'true' || payload === '1' || payload === 1);
        break;
      case `data/treshold/user/${USER_ID}`:
        setThreshold(typeof payload === 'object' ? (payload.threshold ?? '6.5') : String(payload));
        break;
    }
  };

  const publish = (topic: string, payload: any) => {
    if (ws.current && wsConnected)
      ws.current.send(JSON.stringify({ action: 'publish', topic, payload }));
    else
      Alert.alert('Koneksi Terputus', 'WebSocket offline — instruksi tidak dapat dikirim.');
  };

  const toggleMode = () => {
    const next = mode === 'otomatis' ? 'manual' : 'otomatis';
    setMode(next);
    publish(`data/mode/user/${USER_ID}`, next);
  };

  const toggleRelay = (id: 1 | 2) => {
    if (mode === 'otomatis') {
      Alert.alert('Mode Otomatis Aktif', 'Nonaktifkan mode otomatis untuk mengontrol relay secara manual.');
      return;
    }
    const next = id === 1 ? !relay1 : !relay2;
    publish(`data/relay${id}/user/${USER_ID}`, next ? '1' : '0');
    id === 1 ? setRelay1(next) : setRelay2(next);
  };

  // ── History fetch ────────────────────────────────────────────────────────────
  const fetchHistoryByDate = async (date: Date) => {
    setHistoryLoading(true);
    const iso = new Date(date.getTime() - date.getTimezoneOffset() * 60_000).toISOString().slice(0, 10);
    try {
      const res = await axios.get(`${API_URL}/api/ph?date=${iso}&user=${USER_ID}`);
      if (res.data?.success) setHistoryData(res.data.data.reverse());
    } catch (e) {
      Alert.alert('Gagal', 'Tidak dapat mengambil data riwayat.');
    } finally {
      setHistoryLoading(false);
    }
  };

  useEffect(() => {
    if (activeTab === 'history') fetchHistoryByDate(selectedDate);
  }, [activeTab, selectedDate]);

  const onDateChange = (_: any, d?: Date) => {
    setShowPicker(false);
    if (d) setSelectedDate(d);
  };

  const goYesterday = () => {
    const y = new Date();
    y.setDate(y.getDate() - 1);
    setSelectedDate(y);
  };

  const downloadData = () =>
    Linking.openURL(`${API_URL}/api/download/ph?user=${USER_ID}&days=7`)
      .catch(err => console.error(err));

  // ── Chart ────────────────────────────────────────────────────────────────────
  const renderChart = (data: any[]) => {
    if (data.length === 0) return <ActivityIndicator color={C.p500} style={{ marginVertical: 24 }} />;

    const labels = data.map(d => {
      const t = new Date(d.created_at);
      return `${t.getHours()}:${String(t.getMinutes()).padStart(2, '0')}`;
    });
    const values = data.map(d => d.value);
    const step    = Math.max(1, Math.floor(labels.length / 5));
    const display = labels.filter((_, i) => i % step === 0);

    return (
      <LineChart
        data={{ labels: display.length ? display : [''], datasets: [{ data: values }] }}
        width={screenWidth - 64}
        height={200}
        withDots={data.length <= 20}
        withInnerLines={false}
        withShadow={false}
        chartConfig={{
          backgroundColor:         C.surface,
          backgroundGradientFrom:  C.surface,
          backgroundGradientTo:    C.surface,
          decimalPlaces:           2,
          color:   (o = 1) => `rgba(62, 122, 74, ${o})`,   // C.p500
          labelColor: (o = 1) => `rgba(90, 112, 96, ${o})`, // C.muted
          propsForDots: { r: '4', strokeWidth: '2', stroke: C.p700 },
          propsForBackgroundLines: { strokeWidth: 0 },
        }}
        bezier
        style={{ borderRadius: 14, marginTop: 8 }}
      />
    );
  };

  // ── pH status helper ─────────────────────────────────────────────────────────
  const phStatus = (v: number | null) => {
    if (v === null) return { label: '–', color: C.hint };
    if (v < 5.5)   return { label: 'Terlalu Asam', color: C.danger };
    if (v > 7.5)   return { label: 'Terlalu Basa',  color: C.warn  };
    return          { label: 'Optimal',              color: C.success };
  };
  const status = phStatus(ph);

  // ─────────────────────────────────────────────────────────────────────────────
  return (
    <View style={S.root}>
      <StatusBar style="dark" />

      {/* ── App Bar ── */}
      <View style={S.appBar}>
        <View>
          <Text style={S.appBarBrand}>GreenDrop</Text>
          <Text style={S.appBarSub}>Hydroponics Control</Text>
        </View>
        <View style={S.pill}>
          <Dot color={isOnline ? C.success : C.danger} size={7} />
          <Text style={[S.pillText, { color: isOnline ? C.success : C.danger }]}>
            {isOnline ? 'Online' : 'Offline'}
          </Text>
        </View>
      </View>

      {/* ── Tab Bar ── */}
      <View style={S.tabBar}>
        {(['dashboard', 'history'] as const).map(tab => {
          const active = activeTab === tab;
          return (
            <TouchableOpacity
              key={tab}
              style={[S.tab, active && S.tabActive]}
              onPress={() => setActiveTab(tab)}
              activeOpacity={0.7}
            >
              <Feather
                name={tab === 'dashboard' ? 'grid' : 'list'}
                size={15}
                color={active ? C.p700 : C.muted}
              />
              <Text style={[S.tabLabel, active && S.tabLabelActive]}>
                {tab === 'dashboard' ? 'Dashboard' : 'History'}
              </Text>
            </TouchableOpacity>
          );
        })}
      </View>

      <ScrollView
        contentContainerStyle={S.scroll}
        showsVerticalScrollIndicator={false}
        keyboardShouldPersistTaps="handled"
      >
        {/* ════════════════════════════════════════════════════════════
            DASHBOARD TAB
            ════════════════════════════════════════════════════════ */}
        {activeTab === 'dashboard' ? (
          <>
            {/* Live pH Card */}
            <View style={S.card}>
              <View style={S.cardRow}>
                <View style={S.iconBox}>
                  <Feather name="activity" size={16} color={C.p500} />
                </View>
                <Text style={S.cardTitle}>Live pH Sensor</Text>
                <View style={{ flex: 1 }} />
                <Badge label={status.label} color={status.color} />
              </View>
              <View style={S.phBox}>
                <Text style={S.phNum}>{ph !== null ? ph.toFixed(2) : '--'}</Text>
                <Text style={S.phUnit}>Potential of Hydrogen</Text>
              </View>
            </View>

            {/* Realtime Chart */}
            <View style={S.card}>
              <View style={S.cardRow}>
                <View style={S.iconBox}>
                  <Feather name="trending-up" size={16} color={C.p500} />
                </View>
                <Text style={S.cardTitle}>Realtime Tracker</Text>
              </View>
              {renderChart(realtimeHistory)}
            </View>

            <Text style={S.sectionLabel}>KONFIGURASI SISTEM</Text>

            {/* Auto / Manual Mode */}
            <View style={S.card}>
              <View style={[S.cardRow, { marginBottom: 0 }]}>
                <View style={{ flex: 1 }}>
                  <Text style={S.settingName}>Mode Otomatis</Text>
                  <Text style={S.settingDesc}>Sistem mengelola nutrisi & pH secara cerdas</Text>
                </View>
                <Switch
                  value={mode === 'otomatis'}
                  onValueChange={toggleMode}
                  trackColor={{ false: C.line, true: C.p300 }}
                  thumbColor={mode === 'otomatis' ? C.p700 : '#FFFFFF'}
                  ios_backgroundColor={C.line}
                />
              </View>

              {mode === 'otomatis' && (
                <View style={S.thresholdSection}>
                  <Text style={S.thresholdLabel}>Target pH Threshold</Text>
                  <View style={S.thresholdRow}>
                    <TextInput
                      style={S.input}
                      keyboardType="decimal-pad"
                      value={threshold}
                      onChangeText={setThreshold}
                      placeholder="mis. 6.5"
                      placeholderTextColor={C.hint}
                    />
                    <TouchableOpacity
                      style={S.btnUpdate}
                      onPress={() => publish(`data/treshold/user/${USER_ID}`, threshold)}
                      activeOpacity={0.8}
                    >
                      <Text style={S.btnUpdateText}>Update</Text>
                    </TouchableOpacity>
                  </View>
                </View>
              )}
            </View>

            <Text style={S.sectionLabel}>KONTROL RELAY</Text>

            {/* Relay Grid */}
            <View style={S.relayGrid}>
              {[
                { id: 1 as const, icon: 'droplet' as const, title: 'Nutrition Pump',     active: relay1 },
                { id: 2 as const, icon: 'wind'    as const, title: 'Circulation Fan',    active: relay2 },
              ].map(({ id, icon, title, active }) => (
                <TouchableOpacity
                  key={id}
                  style={[S.relayCard, active && S.relayCardActive]}
                  onPress={() => toggleRelay(id)}
                  activeOpacity={mode === 'otomatis' ? 1 : 0.7}
                >
                  <View style={[S.relayIconCircle, active && S.relayIconCircleActive]}>
                    <Feather name={icon} size={20} color={active ? '#FFFFFF' : C.p700} />
                  </View>
                  <Text style={S.relayTitle}>{title}</Text>
                  <View style={S.relayStatusRow}>
                    <Dot color={active ? C.success : C.hint} size={6} />
                    <Text style={[S.relayStatus, { color: active ? C.success : C.hint }]}>
                      {active ? 'Berjalan' : 'Berhenti'}
                    </Text>
                  </View>
                </TouchableOpacity>
              ))}
            </View>
          </>
        ) : (
          /* ════════════════════════════════════════════════════════════
             HISTORY TAB
             ════════════════════════════════════════════════════════ */
          <>
            {/* Date Filter Card */}
            <View style={S.card}>
              <View style={S.cardRow}>
                <View style={S.iconBox}>
                  <Feather name="calendar" size={16} color={C.p500} />
                </View>
                <Text style={S.cardTitle}>Filter Tanggal</Text>
              </View>

              <View style={S.filterRow}>
                {[
                  { label: 'Hari Ini', action: () => setSelectedDate(new Date()) },
                  { label: 'Kemarin',  action: goYesterday },
                ].map(({ label, action }) => (
                  <TouchableOpacity key={label} style={S.filterChip} onPress={action} activeOpacity={0.7}>
                    <Text style={S.filterChipText}>{label}</Text>
                  </TouchableOpacity>
                ))}
                <TouchableOpacity style={S.filterIconBtn} onPress={() => setShowPicker(true)} activeOpacity={0.8}>
                  <Feather name="calendar" size={16} color="#FFFFFF" />
                </TouchableOpacity>
              </View>

              {showPicker && (
                <DateTimePicker
                  value={selectedDate}
                  mode="date"
                  display="default"
                  onChange={onDateChange}
                />
              )}

              <View style={S.datePill}>
                <Feather name="clock" size={13} color={C.p500} />
                <Text style={S.datePillText}>
                  Data:{' '}
                  {new Date(selectedDate.getTime() - selectedDate.getTimezoneOffset() * 60_000)
                    .toISOString()
                    .slice(0, 10)}
                </Text>
              </View>
            </View>

            {/* Data Table */}
            <View style={[S.card, { paddingHorizontal: 0, paddingVertical: 0, overflow: 'hidden' }]}>
              <View style={{ paddingHorizontal: 20, paddingVertical: 18, flexDirection: 'row', alignItems: 'center' }}>
                <View style={S.iconBox}>
                  <Feather name="database" size={16} color={C.p500} />
                </View>
                <Text style={S.cardTitle}>Data Records</Text>
                {historyData.length > 0 && (
                  <View style={{ marginLeft: 'auto' }}>
                    <Badge label={`${historyData.length} entri`} color={C.p500} />
                  </View>
                )}
              </View>

              {historyLoading ? (
                <ActivityIndicator color={C.p500} style={{ marginVertical: 32 }} />
              ) : historyData.length === 0 ? (
                <View style={S.emptyState}>
                  <Feather name="inbox" size={32} color={C.line} />
                  <Text style={S.emptyText}>Tidak ada data pada tanggal ini</Text>
                </View>
              ) : (
                <>
                  <View style={S.tableHead}>
                    {['Waktu', 'Nilai pH', 'Status'].map(h => (
                      <Text key={h} style={S.th}>{h}</Text>
                    ))}
                  </View>
                  {historyData.map((item, idx) => {
                    const d    = new Date(item.created_at);
                    const time = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
                    const val  = parseFloat(item.value);
                    const st   = phStatus(val);
                    return (
                      <View key={idx} style={[S.tableRow, idx % 2 === 1 && S.tableRowAlt]}>
                        <Text style={S.td}>{time} WIB</Text>
                        <Text style={[S.td, { fontWeight: '700', color: C.p700 }]}>{val.toFixed(2)}</Text>
                        <View style={{ flex: 1 }}>
                          <Badge label={st.label} color={st.color} />
                        </View>
                      </View>
                    );
                  })}
                </>
              )}
            </View>

            {/* Download Button */}
            <TouchableOpacity style={S.btnDownload} onPress={downloadData} activeOpacity={0.85}>
              <Feather name="download-cloud" size={18} color="#FFFFFF" />
              <Text style={S.btnDownloadText}>Unduh Riwayat 7 Hari</Text>
            </TouchableOpacity>
          </>
        )}
      </ScrollView>
    </View>
  );
}

// ─── Styles ────────────────────────────────────────────────────────────────────
const S = StyleSheet.create({
  root: { flex: 1, backgroundColor: C.bg },

  // App Bar
  appBar: {
    backgroundColor: C.surface,
    paddingTop: Platform.OS === 'ios' ? 54 : 40,
    paddingBottom: 14,
    paddingHorizontal: 22,
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    borderBottomWidth: 1,
    borderBottomColor: C.line,
  },
  appBarBrand: { fontSize: 19, fontWeight: '800', color: C.ink, letterSpacing: -0.6 },
  appBarSub:   { fontSize: 12, color: C.muted, marginTop: 2, fontWeight: '500' },
  pill: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: C.bg,
    paddingHorizontal: 11,
    paddingVertical: 6,
    borderRadius: 20,
    borderWidth: 1,
    borderColor: C.line,
    gap: 6,
  },
  pillText: { fontSize: 12, fontWeight: '700' },

  // Tab Bar
  tabBar: {
    flexDirection: 'row',
    backgroundColor: C.surface,
    marginHorizontal: 20,
    marginTop: 16,
    marginBottom: 4,
    borderRadius: 14,
    padding: 4,
    borderWidth: 1,
    borderColor: C.line,
  },
  tab: {
    flex: 1,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    paddingVertical: 10,
    borderRadius: 10,
    gap: 6,
  },
  tabActive:      { backgroundColor: C.p50, borderWidth: 1, borderColor: C.p100 },
  tabLabel:       { fontSize: 14, fontWeight: '600', color: C.muted },
  tabLabelActive: { color: C.p700, fontWeight: '700' },

  scroll: { paddingHorizontal: 20, paddingBottom: 60, paddingTop: 16 },

  sectionLabel: {
    fontSize: 11,
    fontWeight: '800',
    color: C.hint,
    letterSpacing: 1,
    marginBottom: 12,
    marginTop: 4,
  },

  // Card
  card: {
    backgroundColor: C.surface,
    borderRadius: 18,
    padding: 20,
    marginBottom: 16,
    borderWidth: 1,
    borderColor: C.line,
    shadowColor: C.p900,
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.06,
    shadowRadius: 8,
    elevation: 3,
  },
  cardRow:   { flexDirection: 'row', alignItems: 'center', marginBottom: 16, gap: 10 },
  cardTitle: { fontSize: 15, fontWeight: '700', color: C.ink },
  iconBox: {
    width: 34,
    height: 34,
    borderRadius: 10,
    backgroundColor: C.p50,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 1,
    borderColor: C.p100,
  },

  // pH display
  phBox: { alignItems: 'center', paddingVertical: 8 },
  phNum:  { fontSize: 64, fontWeight: '900', color: C.p700, letterSpacing: -2, lineHeight: 72 },
  phUnit: { fontSize: 13, color: C.muted, fontWeight: '600', marginTop: 2 },

  // Settings
  settingName: { fontSize: 15, fontWeight: '700', color: C.ink, marginBottom: 3 },
  settingDesc: { fontSize: 12, color: C.muted },
  thresholdSection: {
    marginTop: 18,
    paddingTop: 18,
    borderTopWidth: 1,
    borderTopColor: C.line,
  },
  thresholdLabel: { fontSize: 13, fontWeight: '600', color: C.ink, marginBottom: 10 },
  thresholdRow:   { flexDirection: 'row', gap: 10 },
  input: {
    flex: 1,
    height: 46,
    backgroundColor: C.bg,
    borderWidth: 1,
    borderColor: C.line,
    borderRadius: 12,
    paddingHorizontal: 14,
    fontSize: 16,
    fontWeight: '700',
    color: C.ink,
  },
  btnUpdate: {
    backgroundColor: C.p700,
    paddingHorizontal: 20,
    borderRadius: 12,
    justifyContent: 'center',
  },
  btnUpdateText: { color: '#FFFFFF', fontWeight: '700', fontSize: 14 },

  // Relay Cards
  relayGrid: { flexDirection: 'row', gap: 14, marginBottom: 10 },
  relayCard: {
    flex: 1,
    backgroundColor: C.surface,
    borderRadius: 18,
    padding: 18,
    borderWidth: 1,
    borderColor: C.line,
    shadowColor: C.p900,
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.04,
    shadowRadius: 6,
    elevation: 2,
  },
  relayCardActive:         { borderColor: C.p300, backgroundColor: C.p50 },
  relayIconCircle: {
    width: 46,
    height: 46,
    borderRadius: 23,
    backgroundColor: C.surfaceAlt,
    borderWidth: 1,
    borderColor: C.line,
    alignItems: 'center',
    justifyContent: 'center',
    marginBottom: 14,
  },
  relayIconCircleActive:   { backgroundColor: C.p700, borderColor: C.p700 },
  relayTitle:              { fontSize: 13, fontWeight: '700', color: C.ink, marginBottom: 6 },
  relayStatusRow:          { flexDirection: 'row', alignItems: 'center', gap: 5 },
  relayStatus:             { fontSize: 12, fontWeight: '600' },

  // History - Filter
  filterRow:               { flexDirection: 'row', gap: 10, marginBottom: 0 },
  filterChip: {
    flex: 1,
    paddingVertical: 11,
    borderRadius: 12,
    alignItems: 'center',
    backgroundColor: C.bg,
    borderWidth: 1,
    borderColor: C.line,
  },
  filterChipText:          { fontWeight: '700', color: C.ink, fontSize: 13 },
  filterIconBtn: {
    width: 46,
    borderRadius: 12,
    backgroundColor: C.p700,
    alignItems: 'center',
    justifyContent: 'center',
  },
  datePill: {
    marginTop: 14,
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: C.p50,
    paddingVertical: 9,
    paddingHorizontal: 14,
    borderRadius: 10,
    borderWidth: 1,
    borderColor: C.p100,
    gap: 7,
  },
  datePillText: { color: C.p700, fontWeight: '700', fontSize: 13 },

  // Table
  tableHead: {
    flexDirection: 'row',
    backgroundColor: C.bg,
    paddingVertical: 12,
    paddingHorizontal: 20,
    borderTopWidth: 1,
    borderTopColor: C.line,
  },
  th: { flex: 1, fontSize: 11, fontWeight: '800', color: C.muted, letterSpacing: 0.5, textTransform: 'uppercase' },
  tableRow: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 14,
    paddingHorizontal: 20,
    borderTopWidth: 1,
    borderTopColor: C.line,
  },
  tableRowAlt: { backgroundColor: C.surfaceAlt },
  td: { flex: 1, fontSize: 14, color: C.ink, fontWeight: '500' },

  emptyState: { alignItems: 'center', paddingVertical: 40, gap: 10 },
  emptyText:  { color: C.muted, fontWeight: '500', fontSize: 14 },

  // Download Button
  btnDownload: {
    backgroundColor: C.p700,
    borderRadius: 16,
    paddingVertical: 16,
    flexDirection: 'row',
    justifyContent: 'center',
    alignItems: 'center',
    gap: 10,
    marginTop: 4,
    shadowColor: C.p900,
    shadowOffset: { width: 0, height: 4 },
    shadowOpacity: 0.18,
    shadowRadius: 10,
    elevation: 4,
  },
  btnDownloadText: { color: '#FFFFFF', fontWeight: '800', fontSize: 15 },
});