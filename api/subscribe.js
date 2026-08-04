/**
 * POST /api/subscribe   Body: { email }
 *
 * Optional "notify me about updates" list. Stores unique emails in a Vercel KV
 * set and bumps a daily counter so signups show up in the analytics dashboard.
 * This is the only place we intentionally store personal data (an email the
 * user chose to give us) — disclosed in the privacy policy.
 */

import { kv } from '@vercel/kv';

const CORS = {
  'Access-Control-Allow-Origin':  '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

const isEmail = s => /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(s);
const today = () => new Date().toISOString().slice(0, 10);

export default async function handler(req, res) {
  if (req.method === 'OPTIONS') return res.status(204).set(CORS).end();
  Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
  if (req.method !== 'POST') return res.status(405).json({ ok: false });

  try {
    const email = String(req.body?.email ?? '').trim().toLowerCase();
    if (!isEmail(email) || email.length > 200)
      return res.status(400).json({ ok: false, message: 'Please enter a valid email address.' });

    // Sorted set keyed by signup time (ms). nx keeps the original signup date
    // if they submit again. Returns the number of NEW members added (1 or 0).
    const added = await kv.zadd('subscribers', { nx: true }, { score: Date.now(), member: email });
    if (added) {
      const key = `a:${today()}:subscribe`;
      await kv.incr(key);
      await kv.expire(key, 60 * 60 * 24 * 400);
    }
    return res.status(200).json({ ok: true });
  } catch {
    return res.status(500).json({ ok: false, message: 'Something went wrong. Try again later.' });
  }
}
