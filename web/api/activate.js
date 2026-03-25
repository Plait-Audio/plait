/**
 * POST /api/activate
 *
 * Body: { licenseKey: string, machineId: string }
 *
 * Response:
 *   200 { success: true,  message: string }
 *   400 { success: false, message: string }   ← invalid key / too many machines
 *   500 { success: false, message: string }   ← server error
 */

import { getLicense, setLicense } from './_lib/db.js';
import { isValidKeyFormat, normaliseKey } from './_lib/keys.js';

const CORS = {
  'Access-Control-Allow-Origin':  '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

export default async function handler(req, res) {
  // CORS pre-flight
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

    if (!isValidKeyFormat(key)) {
      return res.status(400).json({ success: false, message: 'Invalid license key format' });
    }

    const mid = String(machineId).trim().toUpperCase();
    if (!mid) {
      return res.status(400).json({ success: false, message: 'machineId is required' });
    }

    const record = await getLicense(key);

    if (!record) {
      return res.status(400).json({ success: false, message: 'Unknown license key' });
    }

    // Already activated on this machine — idempotent success
    if (record.activatedMachines.includes(mid)) {
      return res.status(200).json({ success: true, message: 'Already activated on this machine' });
    }

    // Machine limit check
    if (record.activatedMachines.length >= record.maxMachines) {
      return res.status(400).json({
        success: false,
        message: `This key has reached its machine activation limit (${record.maxMachines}). Deactivate a machine first.`,
      });
    }

    // Activate
    record.activatedMachines.push(mid);
    await setLicense(key, record);

    return res.status(200).json({ success: true, message: 'Activation successful' });

  } catch (err) {
    console.error('[activate]', err);
    return res.status(500).json({ success: false, message: 'Internal server error' });
  }
}
