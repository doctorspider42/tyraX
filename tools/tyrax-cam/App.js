// TyraX Cam - the phone half of the editor's phone camera link.
//
// Two screens: connect (address + pairing code), then the viewfinder - the live
// JPEG stream from the editor with REC / STOP / RECENTRE over it. The phone's
// ARKit pose is what moves the camera producing that image, so the loop is
// closed on screen: turn the phone, the picture turns.
import AsyncStorage from '@react-native-async-storage/async-storage';
import { activateKeepAwakeAsync, deactivateKeepAwake } from 'expo-keep-awake';
import { StatusBar } from 'expo-status-bar';
import React from 'react';
import {
  ActivityIndicator,
  Image,
  KeyboardAvoidingView,
  Platform,
  Pressable,
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  useWindowDimensions,
  View,
} from 'react-native';

import * as ARKit from './modules/tyrax-arkit';
import { EditorLink } from './src/link';

const STORE_KEY = 'tyrax-cam.settings.v1';
const QUALITY_PRESETS = [
  { label: 'Low', maxw: 256, maxh: 256, fps: 10, quality: 45 },
  { label: 'Medium', maxw: 480, maxh: 480, fps: 15, quality: 60 },
  { label: 'High', maxw: 640, maxh: 640, fps: 20, quality: 75 },
];

export default function App() {
  const { width, height } = useWindowDimensions();
  const [host, setHost] = React.useState('');
  const [port, setPort] = React.useState('7798');
  const [code, setCode] = React.useState('');
  const [presetIdx, setPresetIdx] = React.useState(1);
  const [matchLens, setMatchLens] = React.useState(false);
  const [state, setState] = React.useState('idle');
  const [detail, setDetail] = React.useState('');
  const [tracking, setTracking] = React.useState('');
  const [frame, setFrame] = React.useState(null);
  const [status, setStatus] = React.useState({});

  // The link and the pose plumbing live in refs: a pose arrives up to 30 times a
  // second and must not re-render anything.
  const linkRef = React.useRef(null);
  const matchLensRef = React.useRef(matchLens);
  matchLensRef.current = matchLens;

  React.useEffect(() => {
    AsyncStorage.getItem(STORE_KEY)
      .then((raw) => {
        if (!raw) return;
        const s = JSON.parse(raw);
        if (s.host) setHost(s.host);
        if (s.port) setPort(String(s.port));
        if (s.code) setCode(s.code);
        if (typeof s.presetIdx === 'number') setPresetIdx(s.presetIdx);
        if (typeof s.matchLens === 'boolean') setMatchLens(s.matchLens);
      })
      .catch(() => {});
  }, []);

  const link = React.useMemo(() => {
    const l = new EditorLink({
      onState: (s, d) => {
        setState(s);
        setDetail(d || '');
      },
      onFrame: (uri) => setFrame(uri),
      onStatus: (s) => setStatus(s),
    });
    linkRef.current = l;
    return l;
  }, []);

  // ARKit -> the wire. Subscribed once for the app's lifetime; sendPose is a
  // no-op until the handshake completes.
  React.useEffect(() => {
    const pose = ARKit.addPoseListener((p) => {
      linkRef.current?.sendPose(p, matchLensRef.current);
    });
    const track = ARKit.addTrackingListener((e) => setTracking(e.state || ''));
    return () => {
      pose.remove();
      track.remove();
      ARKit.stop();
    };
  }, []);

  const connected = state === 'connected';

  React.useEffect(() => {
    if (connected) {
      ARKit.start(30);
      activateKeepAwakeAsync().catch(() => {});
    } else {
      ARKit.stop();
      deactivateKeepAwake().catch(() => {});
      setFrame(null);
    }
  }, [connected]);

  const doConnect = () => {
    const preset = QUALITY_PRESETS[presetIdx];
    AsyncStorage.setItem(
      STORE_KEY,
      JSON.stringify({ host, port, code, presetIdx, matchLens })
    ).catch(() => {});
    link.connect({
      host: host.trim(),
      port: parseInt(port, 10) || 7798,
      code: code.trim(),
      deviceName: 'iPhone',
      deviceModel: `${Platform.OS} ${Platform.Version}`,
      sixDof: ARKit.isSupported(),
      preview: {
        maxw: Math.min(preset.maxw, Math.round(Math.max(width, height))),
        maxh: preset.maxh,
        fps: preset.fps,
        quality: preset.quality,
      },
    });
  };

  // --- viewfinder ---------------------------------------------------------
  if (connected) {
    return (
      <View style={styles.stage}>
        <StatusBar hidden />
        {frame ? (
          <Image source={{ uri: frame }} style={styles.view} resizeMode="contain" />
        ) : (
          <View style={styles.waiting}>
            <ActivityIndicator color="#7a8290" />
            <Text style={styles.dim}>Waiting for the editor's picture...</Text>
          </View>
        )}

        {status.rec ? (
          <View style={styles.recBadge}>
            <View style={styles.recDot} />
            <Text style={styles.recText}>REC {(status.time || 0).toFixed(1)}s</Text>
          </View>
        ) : null}

        <View style={styles.topBar}>
          <Text style={styles.dim} numberOfLines={1}>
            {(status.seq ? status.seq + '  ' : '') +
              (status.target ? 'camera ' + status.target : 'free shots') +
              (status.keys ? `  ${status.keys} keys` : '') +
              (status.dens ? `  @${status.dens}/s` : '')}
          </Text>
          {tracking && tracking !== 'normal' ? (
            <Text style={styles.warn} numberOfLines={1}>
              {tracking}
            </Text>
          ) : null}
        </View>

        <View style={styles.controls}>
          <Btn label="Recentre" onPress={() => link.command('recenter')} />
          {status.rec ? (
            <Btn label="Stop" tone="rec" onPress={() => link.command('stop')} />
          ) : (
            <Btn label="Record" tone="rec" onPress={() => link.command('record')} />
          )}
          <Btn label="Leave" onPress={() => link.disconnect()} />
        </View>
      </View>
    );
  }

  // --- connect ------------------------------------------------------------
  return (
    <KeyboardAvoidingView
      style={styles.screen}
      behavior={Platform.OS === 'ios' ? 'padding' : undefined}
    >
      <StatusBar style="light" />
      <ScrollView contentContainerStyle={styles.form} keyboardShouldPersistTaps="handled">
        <Text style={styles.title}>TyraX Cam</Text>
        <Text style={styles.dim}>
          Start the link in the editor (Tools &gt; Phone Camera) and type the address it
          shows.
        </Text>

        <Text style={styles.label}>Editor address</Text>
        <TextInput
          style={styles.input}
          value={host}
          onChangeText={setHost}
          placeholder="192.168.1.20"
          placeholderTextColor="#5a6270"
          autoCapitalize="none"
          autoCorrect={false}
          keyboardType="numbers-and-punctuation"
        />

        <View style={styles.row}>
          <View style={styles.half}>
            <Text style={styles.label}>Port</Text>
            <TextInput
              style={styles.input}
              value={port}
              onChangeText={setPort}
              keyboardType="number-pad"
              placeholderTextColor="#5a6270"
            />
          </View>
          <View style={styles.half}>
            <Text style={styles.label}>Pairing code</Text>
            <TextInput
              style={styles.input}
              value={code}
              onChangeText={setCode}
              placeholder="123456"
              placeholderTextColor="#5a6270"
              keyboardType="number-pad"
            />
          </View>
        </View>

        <Text style={styles.label}>Stream quality</Text>
        <View style={styles.row}>
          {QUALITY_PRESETS.map((p, i) => (
            <Pressable
              key={p.label}
              onPress={() => setPresetIdx(i)}
              style={[styles.chip, i === presetIdx && styles.chipOn]}
            >
              <Text style={styles.chipText}>{p.label}</Text>
              <Text style={styles.chipSub}>
                {p.maxw}px {p.fps}fps
              </Text>
            </Pressable>
          ))}
        </View>

        <View style={styles.switchRow}>
          <Switch value={matchLens} onValueChange={setMatchLens} />
          <View style={styles.switchText}>
            <Text style={styles.labelInline}>Match the phone's lens</Text>
            <Text style={styles.dim}>
              Sends this camera's field of view (a narrow ~39 deg) instead of leaving the
              editor's own. Off is usually what a cutscene wants.
            </Text>
          </View>
        </View>

        <Pressable
          onPress={doConnect}
          disabled={!host.trim() || state === 'connecting'}
          style={[styles.connect, (!host.trim() || state === 'connecting') && styles.dimBtn]}
        >
          <Text style={styles.connectText}>
            {state === 'connecting' ? 'Connecting...' : 'Connect'}
          </Text>
        </Pressable>

        {state !== 'idle' && state !== 'connecting' ? (
          <Text style={state === 'denied' || state === 'error' ? styles.warn : styles.dim}>
            {stateText(state, detail)}
          </Text>
        ) : null}

        {!ARKit.available ? (
          <Text style={styles.warn}>
            The ARKit module is missing from this build - the app can show the stream but
            cannot move the camera. Run a native build (see the README), not Expo Go.
          </Text>
        ) : !ARKit.isSupported() ? (
          <Text style={styles.warn}>
            This device has no ARKit world tracking, so the camera cannot be moved from
            here. The stream still works.
          </Text>
        ) : null}
      </ScrollView>
    </KeyboardAvoidingView>
  );
}

function stateText(state, detail) {
  switch (state) {
    case 'denied':
      return `The editor refused the connection: ${detail}`;
    case 'error':
      return `${detail}. Same Wi-Fi as the editor? Right address and port?`;
    case 'closed':
      return `The editor closed the link: ${detail}`;
    case 'disconnected':
      return 'Disconnected.';
    default:
      return state;
  }
}

function Btn({ label, onPress, tone }) {
  return (
    <Pressable onPress={onPress} style={[styles.btn, tone === 'rec' && styles.btnRec]}>
      <Text style={styles.btnText}>{label}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: '#14161a' },
  form: { padding: 22, paddingTop: 64, gap: 10 },
  title: { color: '#f0f2f5', fontSize: 30, fontWeight: '700', marginBottom: 2 },
  label: { color: '#aab2c0', fontSize: 13, marginTop: 10 },
  labelInline: { color: '#e6e6e6', fontSize: 15, fontWeight: '600' },
  dim: { color: '#7a8290', fontSize: 13, lineHeight: 18 },
  warn: { color: '#ffa0a8', fontSize: 13, lineHeight: 18 },
  input: {
    backgroundColor: '#0e1013',
    borderColor: '#333a45',
    borderWidth: 1,
    borderRadius: 8,
    color: '#f0f2f5',
    fontSize: 17,
    paddingHorizontal: 12,
    paddingVertical: 11,
  },
  row: { flexDirection: 'row', gap: 10 },
  half: { flex: 1 },
  chip: {
    flex: 1,
    alignItems: 'center',
    backgroundColor: '#0e1013',
    borderColor: '#333a45',
    borderWidth: 1,
    borderRadius: 8,
    paddingVertical: 9,
  },
  chipOn: { borderColor: '#5580d0', backgroundColor: '#1d2a40' },
  chipText: { color: '#e6e6e6', fontSize: 15, fontWeight: '600' },
  chipSub: { color: '#6f7885', fontSize: 11 },
  switchRow: { flexDirection: 'row', gap: 12, alignItems: 'flex-start', marginTop: 14 },
  switchText: { flex: 1 },
  connect: {
    backgroundColor: '#3b5c94',
    borderRadius: 10,
    paddingVertical: 15,
    alignItems: 'center',
    marginTop: 22,
  },
  dimBtn: { opacity: 0.45 },
  connectText: { color: '#fff', fontSize: 17, fontWeight: '700' },

  stage: { flex: 1, backgroundColor: '#000', justifyContent: 'center' },
  view: { width: '100%', height: '100%' },
  waiting: { alignItems: 'center', gap: 12 },
  topBar: { position: 'absolute', top: 44, left: 18, right: 18, gap: 3 },
  recBadge: {
    position: 'absolute',
    top: 44,
    right: 18,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 7,
  },
  recDot: { width: 11, height: 11, borderRadius: 6, backgroundColor: '#ff3040' },
  recText: { color: '#ff5a6a', fontWeight: '700', letterSpacing: 0.5 },
  controls: {
    position: 'absolute',
    bottom: 38,
    left: 16,
    right: 16,
    flexDirection: 'row',
    justifyContent: 'space-between',
    gap: 10,
  },
  btn: {
    flex: 1,
    alignItems: 'center',
    backgroundColor: 'rgba(30,38,52,0.86)',
    borderColor: '#3d5480',
    borderWidth: 1,
    borderRadius: 10,
    paddingVertical: 15,
  },
  btnRec: { backgroundColor: 'rgba(110,28,38,0.9)', borderColor: '#8a3542' },
  btnText: { color: '#f0f2f5', fontSize: 16, fontWeight: '700' },
});
