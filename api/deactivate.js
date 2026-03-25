/**
 * POST /api/deactivate
 *
 * Body: { licenseKey: string, machineId: string }
 *
 * Response:
 *   200 { success: true }
 *   400 { success: false, message: string }
 */

import { getLicense, setLicense } from './_lib/db.js';
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
    return res.status(405).json({ success: false, message: 'Method not allowed' });
  }

  Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));

  try {
    const { licenseKey, machineId } = req.body ?? {};

    if (!licenseKey || !machineId) {
      return res.status(400).json({ success: false, message: 'licenseKey and machineId are required' });
    }

    const key = normaliseKey(String(licenseKey));
    const mid = String(machineId).trim().toUpperCase();

    if (!isValidKeyFormat(key)) {
      return res.status(400).json({ success: false, message: 'Invalid license key format' });
    }

    const record = await getLicense(key);

    if (!record) {
      // Nothing to deactivate — treat as success so the client can clear its local state
      return res.status(200).json({ success: true });
    }

    record.activatedMachines = record.activatedMachines.filter(m => m !== mid);
    await setLicense(key, record);

    return res.status(200).json({ success: true });

  } catch (err) {
    console.error('[deactivate]', err);
    return res.status(500).json({ success: false, message: 'Internal server error' });
  }
}
