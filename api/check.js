/**
 * POST /api/check
 *
 * Periodic license validity check (called by the app after 30+ days offline).
 *
 * Body: { licenseKey: string, machineId: string }
 *
 * Response:
 *   200 { valid: true }
 *   200 { valid: false, message: string }
 */

import { getLicense } from './_lib/db.js';
import { isValidKeyFormat, normaliseKey } from './_lib/keys.js';

const CORS = {
  'Access-Control-Allow-Origin':  '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

export default async function handler(req, res) {
  if (req.method === 'OPTIONS') {
    return res.status(204).set(CORS).end();
  }

  if (req.method !== 'POST') {
    return res.status(405).json({ valid: false, message: 'Method not allowed' });
  }

  Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));

  try {
    const { licenseKey, machineId } = req.body ?? {};

    if (!licenseKey || !machineId) {
      return res.status(200).json({ valid: false, message: 'licenseKey and machineId are required' });
    }

    const key = normaliseKey(String(licenseKey));
    const mid = String(machineId).trim().toUpperCase();

    if (!isValidKeyFormat(key)) {
      return res.status(200).json({ valid: false, message: 'Invalid key format' });
    }

    const record = await getLicense(key);

    if (!record) {
      return res.status(200).json({ valid: false, message: 'Unknown license key' });
    }

    if (!record.activatedMachines.includes(mid)) {
      return res.status(200).json({ valid: false, message: 'Machine not registered for this key' });
    }

    return res.status(200).json({ valid: true });

  } catch (err) {
    console.error('[check]', err);
    // Return valid:true on server error so a transient outage doesn't lock out the user
    return res.status(200).json({ valid: true, message: 'Server error — grace period extended' });
  }
}
