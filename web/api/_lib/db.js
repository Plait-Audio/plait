/**
 * License database — thin wrapper around Vercel KV (Upstash Redis).
 *
 * Each license is stored as a JSON value under the key "license:{KEY}".
 *
 * Schema:
 * {
 *   licenseKey:         string,   // e.g. "ISO-XXXX-XXXX-XXXX-XXXX"
 *   email:              string,
 *   maxMachines:        number,   // default 2
 *   activatedMachines:  string[], // machine ID strings
 *   createdAt:          string,   // ISO 8601
 *   orderId:            string,   // from payment provider
 * }
 *
 * To swap the storage backend, replace the `kv` import with any adapter
 * that implements `get(key)` → object | null and `set(key, value)` → void.
 */

import { kv } from '@vercel/kv';

const PREFIX = 'license:';

/**
 * Fetch a license record by key.
 * @param {string} licenseKey
 * @returns {Promise<object|null>}
 */
export async function getLicense(licenseKey) {
  return kv.get(PREFIX + licenseKey.toUpperCase());
}

/**
 * Persist (create or overwrite) a license record.
 * @param {string} licenseKey
 * @param {object} data
 */
export async function setLicense(licenseKey, data) {
  await kv.set(PREFIX + licenseKey.toUpperCase(), data);
}

/**
 * Create a brand-new license record.
 * @param {object} opts
 * @param {string} opts.licenseKey
 * @param {string} opts.email
 * @param {string} opts.orderId
 * @param {number} [opts.maxMachines=2]
 * @returns {Promise<object>} the created record
 */
export async function createLicense({ licenseKey, email, orderId, maxMachines = 2 }) {
  const record = {
    licenseKey,
    email,
    maxMachines,
    activatedMachines: [],
    createdAt: new Date().toISOString(),
    orderId,
  };
  await setLicense(licenseKey, record);
  return record;
}
