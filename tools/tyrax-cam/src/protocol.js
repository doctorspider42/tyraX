// The TyraX phone-camera wire format (docs/phone-camera.md).
//
// One WebSocket BINARY message = one editor frame:
//   [u32 jsonLen][u32 binLen][jsonLen bytes UTF-8 JSON][binLen bytes raw]
// both lengths little-endian. The JSON part carries the message ("t" = type),
// the binary trailer the bulk payload (a JPEG preview frame). Keeping bytes out
// of the JSON is deliberate on the editor side - its JSON reader collapses \u
// escapes, so binary must never pass through it.

export const PROTO_VERSION = 1;

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

export function encodeFrame(obj, bin) {
  const json = textEncoder.encode(JSON.stringify(obj));
  const body = bin || EMPTY;
  const out = new Uint8Array(8 + json.length + body.length);
  const view = new DataView(out.buffer);
  view.setUint32(0, json.length, true);
  view.setUint32(4, body.length, true);
  out.set(json, 8);
  out.set(body, 8 + json.length);
  return out;
}

const EMPTY = new Uint8Array(0);

// Returns { msg, bin } or null when the buffer is not a whole frame.
export function decodeFrame(buffer) {
  const u8 = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  if (u8.length < 8) return null;
  const view = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  const jsonLen = view.getUint32(0, true);
  const binLen = view.getUint32(4, true);
  if (u8.length < 8 + jsonLen + binLen) return null;
  let msg;
  try {
    msg = JSON.parse(textDecoder.decode(u8.subarray(8, 8 + jsonLen)));
  } catch (e) {
    return null;
  }
  return { msg, bin: u8.subarray(8 + jsonLen, 8 + jsonLen + binLen) };
}

// Base64, hand-rolled: React Native has no btoa and no Buffer, and pulling a
// polyfill in for the one thing we need would be silly. ~20 KB per frame at
// 15 fps is nothing measurable here.
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

export function toBase64(u8) {
  let out = '';
  const n = u8.length;
  const tail = n % 3;
  const end = n - tail;
  for (let i = 0; i < end; i += 3) {
    const v = (u8[i] << 16) | (u8[i + 1] << 8) | u8[i + 2];
    out += B64[(v >> 18) & 63] + B64[(v >> 12) & 63] + B64[(v >> 6) & 63] + B64[v & 63];
  }
  if (tail === 1) {
    const v = u8[end] << 16;
    out += B64[(v >> 18) & 63] + B64[(v >> 12) & 63] + '==';
  } else if (tail === 2) {
    const v = (u8[end] << 16) | (u8[end + 1] << 8);
    out += B64[(v >> 18) & 63] + B64[(v >> 12) & 63] + B64[(v >> 6) & 63] + '=';
  }
  return out;
}
