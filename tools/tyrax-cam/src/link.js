// The connection to the editor: one WebSocket, the handshake, the pose stream
// out and the preview/status stream in.
import { PROTO_VERSION, decodeFrame, encodeFrame, toBase64 } from './protocol';

export class EditorLink {
  // handlers: { onState(state, detail), onFrame(dataUri, w, h), onStatus(status) }
  constructor(handlers) {
    this.handlers = handlers;
    this.ws = null;
    this.ready = false;
    this.closedByUs = false;
  }

  connect({ host, port, code, deviceName, deviceModel, sixDof, preview }) {
    this.disconnect();
    this.closedByUs = false;
    const url = `ws://${host}:${port}/`;
    this.handlers.onState?.('connecting', url);
    const ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';
    this.ws = ws;

    ws.onopen = () => {
      this._send({
        t: 'hello',
        proto: PROTO_VERSION,
        code: code || '',
        name: deviceName,
        model: deviceModel,
        client: 'TyraX Cam 1.0.0',
        sixdof: !!sixDof,
      });
      if (preview) this.setPreview(preview);
    };

    ws.onmessage = (event) => {
      const frame = decodeFrame(event.data);
      if (!frame) return;
      const { msg, bin } = frame;
      switch (msg.t) {
        case 'welcome':
          this.ready = true;
          this.handlers.onState?.('connected', msg.project || '');
          break;
        case 'deny':
          // The editor closes the socket right after a deny; report the reason
          // before onclose overwrites the state with a bare "disconnected".
          this.closedByUs = true;
          this.handlers.onState?.('denied', msg.reason || '');
          break;
        case 'bye':
          this.closedByUs = true;
          this.handlers.onState?.('closed', msg.reason || '');
          break;
        case 'frame':
          if (bin.length) {
            this.handlers.onFrame?.(
              `data:image/jpeg;base64,${toBase64(bin)}`,
              msg.w,
              msg.h
            );
          }
          break;
        case 'status':
          this.handlers.onStatus?.(msg);
          break;
        default:
          break;
      }
    };

    ws.onerror = () => {
      if (!this.closedByUs) this.handlers.onState?.('error', 'cannot reach the editor');
    };

    ws.onclose = () => {
      this.ready = false;
      this.ws = null;
      if (!this.closedByUs) this.handlers.onState?.('disconnected', '');
    };
  }

  disconnect() {
    if (!this.ws) return;
    this.closedByUs = true;
    try {
      this._send({ t: 'bye' });
      this.ws.close();
    } catch (e) {
      /* already gone */
    }
    this.ws = null;
    this.ready = false;
  }

  // { maxw, maxh, fps, quality } - the editor honours these over its own
  // defaults, because only the device knows its screen and its Wi-Fi.
  setPreview(preview) {
    this._send({ t: 'cfg', ...preview });
  }

  // 'record' | 'stop' | 'recenter'
  command(cmd) {
    this._send({ t: 'cmd', cmd });
  }

  // pose: { ts, px, py, pz, qx, qy, qz, qw, fov } - fov 0/omitted keeps the
  // editor's own field of view.
  sendPose(pose, sendFov) {
    if (!this.ready) return;
    const msg = {
      t: 'pose',
      ts: pose.ts,
      p: [pose.px, pose.py, pose.pz],
      q: [pose.qx, pose.qy, pose.qz, pose.qw],
    };
    if (sendFov && pose.fov > 1) msg.fov = pose.fov;
    this._send(msg);
  }

  _send(obj, bin) {
    const ws = this.ws;
    if (!ws || ws.readyState !== 1) return;
    try {
      ws.send(encodeFrame(obj, bin));
    } catch (e) {
      /* the socket died between the check and the send */
    }
  }
}
