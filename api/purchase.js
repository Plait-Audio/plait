/**
 * POST /api/purchase
 *
 * LemonSqueezy webhook — fires when an order completes.
 *
 * What this handler does:
 *   1. Verifies the HMAC-SHA256 signature from LemonSqueezy.
 *   2. Ignores events other than "order_created".
 *   3. Generates a license key.
 *   4. Stores the key in the database.
 *   5. Sends the license email via Resend.
 *
 * Setup in LemonSqueezy dashboard:
 *   Settings → Webhooks → Add Webhook
 *     URL: https://plaitaudio.com/api/purchase
 *     Events: order_created
 *     Secret: <set LEMONSQUEEZY_WEBHOOK_SECRET in Vercel env>
 *
 * Raw body access is required for signature verification,
 * so Vercel's default body parser is disabled.
 */

import crypto from 'node:crypto';
import { createLicense } from './_lib/db.js';
import { generateLicenseKey } from './_lib/keys.js';
import { sendLicenseEmail } from './_lib/email.js';

export const config = {
  api: { bodyParser: false },
};

/**
 * Read the raw request body as a Buffer.
 */
function readRawBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', chunk => chunks.push(chunk));
    req.on('end',  ()    => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

/**
 * Verify LemonSqueezy webhook signature.
 * Header: X-Signature  (hex HMAC-SHA256 of raw body)
 */
function verifySignature(rawBody, signature, secret) {
  if (!signature || !secret) return false;
  const expected = crypto
    .createHmac('sha256', secret)
    .update(rawBody)
    .digest('hex');
  try {
    return crypto.timingSafeEqual(
      Buffer.from(signature, 'hex'),
      Buffer.from(expected,  'hex')
    );
  } catch {
    return false;
  }
}

export default async function handler(req, res) {
  if (req.method !== 'POST') {
    return res.status(405).json({ error: 'Method not allowed' });
  }

  // Read raw body before any parsing
  const rawBody  = await readRawBody(req);
  const signature = req.headers['x-signature'] ?? '';
  const secret    = process.env.LEMONSQUEEZY_WEBHOOK_SECRET ?? '';

  if (!verifySignature(rawBody, signature, secret)) {
    console.warn('[purchase] Invalid webhook signature');
    return res.status(401).json({ error: 'Invalid signature' });
  }

  let payload;
  try {
    payload = JSON.parse(rawBody.toString('utf8'));
  } catch {
    return res.status(400).json({ error: 'Invalid JSON' });
  }

  const eventName = payload?.meta?.event_name;

  // Only process completed orders
  if (eventName !== 'order_created') {
    return res.status(200).json({ received: true, skipped: true, event: eventName });
  }

  const attrs   = payload?.data?.attributes ?? {};
  const email   = attrs.user_email ?? attrs.customer_email ?? '';
  const orderId = String(attrs.order_number ?? payload?.data?.id ?? 'unknown');

  if (!email) {
    console.error('[purchase] No email in webhook payload', JSON.stringify(payload));
    return res.status(400).json({ error: 'No customer email in payload' });
  }

  try {
    const licenseKey = generateLicenseKey();

    await createLicense({
      licenseKey,
      email,
      orderId,
      maxMachines: 2,
    });

    await sendLicenseEmail({ to: email, licenseKey, orderId });

    console.log(`[purchase] License created: ${licenseKey} → ${email} (order ${orderId})`);

    return res.status(200).json({ received: true, licenseKey });

  } catch (err) {
    console.error('[purchase] Error processing order:', err);
    return res.status(500).json({ error: 'Failed to process order' });
  }
}
