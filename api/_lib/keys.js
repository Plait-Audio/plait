/**
 * License key generation and validation.
 *
 * Format: ISO-XXXX-XXXX-XXXX-XXXX
 * Charset: uppercase alphanumeric with ambiguous characters removed
 * (no 0, O, I, 1 — reduces transcription errors).
 */

const CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

/**
 * Returns a cryptographically random 4-char group.
 */
function group() {
  const buf = new Uint8Array(4);
  crypto.getRandomValues(buf);
  return Array.from(buf, b => CHARS[b % CHARS.length]).join('');
}

/**
 * Generate a new unique license key.
 * @returns {string} e.g. "ISO-A3BK-9QZP-7YHF-R2MN"
 */
export function generateLicenseKey() {
  return `ISO-${group()}-${group()}-${group()}-${group()}`;
}

/**
 * Validate license key format (does not check DB).
 * @param {string} key
 * @returns {boolean}
 */
export function isValidKeyFormat(key) {
  if (typeof key !== 'string') return false;
  return /^ISO-[A-Z2-9]{4}-[A-Z2-9]{4}-[A-Z2-9]{4}-[A-Z2-9]{4}$/.test(
    key.trim().toUpperCase()
  );
}

/**
 * Normalise a key string to uppercase trimmed form.
 * @param {string} key
 * @returns {string}
 */
export function normaliseKey(key) {
  return key.trim().toUpperCase();
}
