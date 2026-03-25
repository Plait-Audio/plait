/**
 * Transactional email via Resend (https://resend.com).
 *
 * Set RESEND_API_KEY and FROM_EMAIL in your Vercel environment.
 */

import { Resend } from 'resend';

const resend = new Resend(process.env.RESEND_API_KEY);
const FROM   = process.env.FROM_EMAIL ?? 'ISO Drums <licenses@plaitaudio.com>';

/**
 * Send the license key delivery email after a successful purchase.
 *
 * @param {object} opts
 * @param {string} opts.to           Recipient email address
 * @param {string} opts.licenseKey   e.g. "ISO-A3BK-9QZP-7YHF-R2MN"
 * @param {string} opts.orderId      Payment provider order reference
 * @returns {Promise<void>}
 */
export async function sendLicenseEmail({ to, licenseKey, orderId }) {
  await resend.emails.send({
    from:    FROM,
    to,
    subject: 'Your ISO Drums License Key',
    html: `
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
           background: #0d0d1f; color: #e8e8f0; margin: 0; padding: 0; }
    .wrap { max-width: 520px; margin: 40px auto; padding: 0 20px; }
    .card { background: #1a1a2e; border: 1px solid rgba(255,255,255,0.08);
            border-radius: 16px; padding: 36px; }
    h1   { color: #e94560; font-size: 22px; margin: 0 0 8px; }
    p    { color: #9999bb; line-height: 1.6; margin: 0 0 16px; }
    .key-box { background: #0d0d1f; border: 1px solid #e94560;
               border-radius: 10px; padding: 18px 24px; text-align: center;
               margin: 24px 0; }
    .key { font-family: 'SF Mono', 'Fira Code', monospace; font-size: 22px;
           font-weight: bold; letter-spacing: 2px; color: #e94560; }
    .step { display: flex; gap: 12px; align-items: flex-start; margin-bottom: 12px; }
    .num  { background: #e94560; color: white; border-radius: 50%;
            width: 22px; height: 22px; display: flex; align-items: center;
            justify-content: center; font-size: 12px; font-weight: bold;
            flex-shrink: 0; margin-top: 2px; }
    .foot { color: #555577; font-size: 12px; margin-top: 32px; text-align: center; }
    a    { color: #6a7aff; text-decoration: none; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card">
      <h1>ISO Drums</h1>
      <p>Thanks for your purchase! Here is your license key:</p>

      <div class="key-box">
        <div class="key">${licenseKey}</div>
      </div>

      <p><strong style="color:#e8e8f0">How to activate:</strong></p>

      <div class="step">
        <div class="num">1</div>
        <p style="margin:0">Open <strong style="color:#e8e8f0">ISO Drums</strong> (download from
          <a href="https://plaitaudio.com/iso-drums#download">plaitaudio.com</a> if you haven't already).</p>
      </div>
      <div class="step">
        <div class="num">2</div>
        <p style="margin:0">Click the <strong style="color:#e8e8f0">License…</strong> button in the top-right of the app.</p>
      </div>
      <div class="step">
        <div class="num">3</div>
        <p style="margin:0">Paste your key and click <strong style="color:#e8e8f0">Activate</strong>.</p>
      </div>

      <p style="margin-top:20px">
        Your license works on up to <strong style="color:#e8e8f0">2 machines</strong>.
        Questions? Reply to this email or visit
        <a href="https://plaitaudio.com/support">plaitaudio.com/support</a>.
      </p>
    </div>
    <p class="foot">
      Order reference: ${orderId}<br>
      © 2026 Plait Audio · <a href="https://plaitaudio.com/privacy">Privacy</a>
    </p>
  </div>
</body>
</html>
    `.trim(),
    text: [
      'ISO Drums — Your License Key',
      '',
      `License key: ${licenseKey}`,
      '',
      'How to activate:',
      '1. Open ISO Drums (download from https://plaitaudio.com/iso-drums#download if needed).',
      '2. Click the "License…" button in the top-right.',
      '3. Paste your key and click Activate.',
      '',
      'Your license works on up to 2 machines.',
      '',
      `Order reference: ${orderId}`,
      '© 2026 Plait Audio',
    ].join('\n'),
  });
}
