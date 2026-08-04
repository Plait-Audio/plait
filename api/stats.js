/**
 * GET /api/stats?token=SECRET&days=30
 *
 * Read-only aggregate dashboard data for ISO Drums analytics.
 * Protected by the ANALYTICS_STATS_TOKEN env var (set it in Vercel).
 * Returns per-day counters — no per-user or per-event detail exists to return.
 */

import { kv } from '@vercel/kv';

const EVENTS = ['launch', 'separate', 'export_wav', 'export_midi'];
const DUR_BUCKETS = ['0-30s', '30-90s', '90-300s', '300s+'];

function lastNDates(n) {
  const out = [];
  const now = Date.now();
  for (let i = 0; i < n; i++) {
    out.push(new Date(now - i * 86400000).toISOString().slice(0, 10));
  }
  return out.reverse();                          // oldest -> newest (nicer for charts)
}

const num = v => Number(v || 0);

export default async function handler(req, res) {
  const token = req.query?.token;
  const expected = process.env.ANALYTICS_STATS_TOKEN;
  if (!expected || token !== expected)
    return res.status(401).json({ ok: false, message: 'unauthorized' });

  const days = Math.min(Math.max(parseInt(req.query?.days ?? '30', 10) || 30, 1), 400);
  const dates = lastNDates(days);

  try {
    const daily = {};
    const totals = { ...Object.fromEntries(EVENTS.map(e => [e, 0])), installs: 0, sep_ok: 0, sep_fail: 0, subscribe: 0 };
    const durBuckets = Object.fromEntries(DUR_BUCKETS.map(b => [b, 0]));

    for (const d of dates) {
      const [counts, installs, ok, fail, durs, subs] = await Promise.all([
        Promise.all(EVENTS.map(e => kv.get(`a:${d}:${e}`))),
        kv.scard(`a:${d}:installs`).catch(() => 0),
        kv.get(`a:${d}:sep:ok`),
        kv.get(`a:${d}:sep:fail`),
        Promise.all(DUR_BUCKETS.map(b => kv.get(`a:${d}:sepdur:${b}`))),
        kv.get(`a:${d}:subscribe`),
      ]);

      const row = { installs: num(installs), sep_ok: num(ok), sep_fail: num(fail), subscribe: num(subs) };
      EVENTS.forEach((e, i) => { row[e] = num(counts[i]); totals[e] += row[e]; });
      totals.installs += row.installs;
      totals.sep_ok  += row.sep_ok;
      totals.sep_fail += row.sep_fail;
      totals.subscribe += row.subscribe;
      DUR_BUCKETS.forEach((b, i) => { durBuckets[b] += num(durs[i]); });
      daily[d] = row;
    }

    // Newest-first list of { email, ts } from the sorted set.
    const flat = (await kv.zrange('subscribers', 0, -1, { withScores: true, rev: true }).catch(() => [])) || [];
    const subscribers = [];
    for (let i = 0; i < flat.length; i += 2)
      subscribers.push({ email: flat[i], ts: Number(flat[i + 1]) || 0 });
    return res.status(200).json({
      ok: true, days, dates, totals, durBuckets,
      subscribersTotal: subscribers.length, subscribers, daily,
    });
  } catch (e) {
    return res.status(500).json({ ok: false, message: String(e) });
  }
}
