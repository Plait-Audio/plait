/**
 * POST /api/event
 *
 * Privacy-first, anonymous product analytics ingest for ISO Drums.
 *
 * Design constraints (must stay true — it's a brand promise):
 *   - Opt-in only. The app sends nothing unless the user turns it on.
 *   - No PII. `installId` is a random UUID generated per install, NOT derived
 *     from hardware, license, email, or MAC. It only enables coarse
 *     unique-install / retention counts.
 *   - No content. Never any audio, filenames, or file paths.
 *   - Aggregate only. We store daily counters in Vercel KV, not a raw event log.
 *
 * Body: { event, installId?, appVersion?, os?, arch?, dims? }
 * Response: 204 (always, unless method/shape is wrong — never errors the client).
 */

import { kv } from '@vercel/kv';

const CORS = {
  'Access-Control-Allow-Origin':  '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

const EVENTS = new Set(['launch', 'separate', 'export_wav', 'export_midi']);
const TTL_SECONDS = 60 * 60 * 24 * 400;              // ~13 months, then auto-expires
const DUR_BUCKETS = ['0-30s', '30-90s', '90-300s', '300s+'];

const clean = (s, max = 24) =>
  String(s ?? '').replace(/[^A-Za-z0-9._-]/g, '').slice(0, max);

function today() {
  return new Date().toISOString().slice(0, 10);       // YYYY-MM-DD (UTC)
}

async function bump(key) {
  try { await kv.incr(key); await kv.expire(key, TTL_SECONDS); } catch { /* best-effort */ }
}

export default async function handler(req, res) {
  if (req.method === 'OPTIONS') return res.status(204).set(CORS).end();
  Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
  if (req.method !== 'POST')
    return res.status(405).json({ ok: false });

  try {
    const b = req.body ?? {};
    const event = clean(b.event);
    if (!EVENTS.has(event)) return res.status(204).end();  // silently ignore unknown

    const d = today();
    const ver = clean(b.appVersion, 16) || 'unknown';

    await bump(`a:${d}:${event}`);
    await bump(`a:${d}:${event}:v:${ver}`);

    // Coarse unique-install set for DAU/retention (random UUIDs only, expiring).
    if (b.installId) {
      const id = clean(b.installId, 40);
      if (id) {
        try {
          await kv.sadd(`a:${d}:installs`, id);
          await kv.expire(`a:${d}:installs`, TTL_SECONDS);
        } catch { /* best-effort */ }
      }
    }

    // Extra dimensions for the one event where they're genuinely useful.
    if (event === 'separate') {
      const dims = b.dims ?? {};
      const ok = String(dims.ok) === 'false' ? 'fail' : 'ok';
      await bump(`a:${d}:sep:${ok}`);
      const bucket = DUR_BUCKETS.includes(dims.durBucket) ? dims.durBucket : null;
      if (bucket) await bump(`a:${d}:sepdur:${bucket}`);
    }

    return res.status(204).end();
  } catch {
    return res.status(204).end();  // never surface errors to the client
  }
}
