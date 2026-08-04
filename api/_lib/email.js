/**
 * Transactional email via Resend (https://resend.com).
 *
 * Set RESEND_API_KEY and FROM_EMAIL in your Vercel environment.
 */

import { Resend } from 'resend';

const resend = new Resend(process.env.RESEND_API_KEY);
const FROM   = process.env.FROM_EMAIL ?? 'ISO Drums <licenses@plaitaudio.com>';

const A = {           // brand palette (matches the app + site)
  bg:     '#09090B',
  card:   '#131316',
  border: '#2A2A2F',
  gold:   '#C9A96E',
  text:   '#E8E4DF',
  dim:    '#8A8A8E',
  muted:  '#5A5A5E',
};

const step = (n, html) => `
      <tr>
        <td style="width:26px; vertical-align:top; padding:0 0 12px;">
          <div style="background:${A.gold}; color:#000; border-radius:50%; width:20px; height:20px; text-align:center; line-height:20px; font-size:11px; font-weight:700;">${n}</div>
        </td>
        <td style="vertical-align:top; padding:0 0 12px 10px;">
          <span style="font-size:14px; color:${A.dim}; line-height:1.5;">${html}</span>
        </td>
      </tr>`;

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
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1"></head>
<body style="margin:0; padding:0; background:${A.bg};">
  <div style="max-width:520px; margin:0 auto; padding:40px 20px; font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;">

    <div style="padding:0 4px 24px;">
      <img src="https://www.plaitaudio.com/logo-iso-drums-horiz-white.png" alt="ISO Drums"
           width="172" style="display:block; width:172px; max-width:58%; height:auto; border:0;">
    </div>

    <div style="background:${A.card}; border:1px solid ${A.border}; border-radius:14px; padding:34px;">
      <div style="font-size:20px; font-weight:700; color:${A.text}; margin:0 0 8px;">You're in. 🥁</div>
      <p style="font-size:15px; color:${A.dim}; line-height:1.65; margin:0 0 22px;">Thanks for supporting ISO&nbsp;Drums. Here's your license key:</p>

      <div style="background:${A.bg}; border:1px solid ${A.gold}; border-radius:10px; padding:18px 24px; text-align:center; margin:0 0 26px;">
        <div style="font-family:'SF Mono','Fira Code',ui-monospace,monospace; font-size:21px; font-weight:700; letter-spacing:2px; color:${A.gold};">${licenseKey}</div>
      </div>

      <div style="font-size:13px; font-weight:600; color:${A.text}; letter-spacing:0.4px; text-transform:uppercase; margin:0 0 14px;">How to activate</div>

      <table role="presentation" cellpadding="0" cellspacing="0" style="width:100%; margin:0 0 6px;">
        ${step(1, `Open <strong style="color:${A.text};">ISO Drums</strong> (<a href="https://www.plaitaudio.com/#download" style="color:${A.gold}; text-decoration:none;">download here</a> if you haven't yet).`)}
        ${step(2, `Click the <strong style="color:${A.text};">gear icon → License…</strong> in the top-right.`)}
        ${step(3, `Paste your key and hit <strong style="color:${A.text};">Activate</strong>.`)}
      </table>

      <div style="text-align:center; margin:28px 0 6px;">
        <a href="https://www.plaitaudio.com/#download" style="display:inline-block; background:${A.gold}; color:#000; text-decoration:none; font-weight:600; font-size:14px; padding:12px 26px; border-radius:9px;">Download ISO&nbsp;Drums</a>
      </div>

      <p style="font-size:13px; color:${A.muted}; line-height:1.6; margin:20px 0 0; text-align:center;">
        Works on up to <strong style="color:${A.dim};">2 machines</strong>. Questions? Just reply to this email.
      </p>
    </div>

    <div style="text-align:center; margin-top:30px;">
      <img src="https://www.plaitaudio.com/logo-plait-horiz-white.png" alt="Plait Audio"
           width="82" style="display:inline-block; width:82px; height:auto; border:0; opacity:0.65;">
      <div style="color:${A.muted}; font-size:12px; margin-top:14px; line-height:1.7;">
        Order reference: ${orderId}<br>
        © 2026 Plait Audio · <a href="https://www.plaitaudio.com/privacy.html" style="color:${A.dim}; text-decoration:none;">Privacy</a> · <a href="https://www.plaitaudio.com/terms.html" style="color:${A.dim}; text-decoration:none;">Terms</a>
      </div>
    </div>
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
      '1. Open ISO Drums (download: https://www.plaitaudio.com/#download).',
      '2. Click the gear icon -> "License..." in the top-right.',
      '3. Paste your key and click Activate.',
      '',
      'Works on up to 2 machines. Questions? Just reply to this email.',
      '',
      `Order reference: ${orderId}`,
      '© 2026 Plait Audio',
    ].join('\n'),
  });
}
