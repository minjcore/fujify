const CORS = {
  'Access-Control-Allow-Origin': 'https://fujify.app',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

export default {
  async fetch(request, env) {
    if (request.method === 'OPTIONS') {
      return new Response(null, { status: 204, headers: CORS });
    }

    const url = new URL(request.url);

    if (request.method === 'POST' && url.pathname === '/waitlist') {
      return handleWaitlist(request, env);
    }

    return new Response('Not found', { status: 404 });
  },
};

async function handleWaitlist(request, env) {
  let body;
  try {
    body = await request.json();
  } catch {
    return json({ error: 'Invalid JSON' }, 400);
  }

  const email = (body.email ?? '').trim().toLowerCase();
  if (!isValidEmail(email)) {
    return json({ error: 'Invalid email' }, 400);
  }

  // Deduplicate
  const existing = await env.WAITLIST.get(email);
  if (existing) {
    return json({ ok: true, already: true });
  }

  const ts = new Date().toISOString();
  await env.WAITLIST.put(email, ts);

  await Promise.allSettled([
    sendConfirmation(email, env),
    sendOwnerNotification(email, ts, env),
  ]);

  return json({ ok: true });
}

async function sendConfirmation(email, env) {
  const html = confirmationHtml(email);
  return resend(env.RESEND_API_KEY, {
    from: 'Fujify <hello@fujify.app>',
    to: email,
    subject: "You're on the Fujify beta list.",
    html,
  });
}

async function sendOwnerNotification(email, ts, env) {
  return resend(env.RESEND_API_KEY, {
    from: 'Fujify Waitlist <hello@fujify.app>',
    to: env.OWNER_EMAIL,
    subject: `New signup: ${email}`,
    html: `<p style="font-family:monospace">${email}<br><small>${ts}</small></p>`,
  });
}

async function resend(apiKey, payload) {
  const res = await fetch('https://api.resend.com/emails', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${apiKey}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(payload),
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Resend error ${res.status}: ${text}`);
  }
  return res.json();
}

function isValidEmail(email) {
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email) && email.length <= 254;
}

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { ...CORS, 'Content-Type': 'application/json' },
  });
}

function confirmationHtml(email) {
  return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>You're on the list.</title>
</head>
<body style="margin:0;padding:0;background:#0a0a0a;font-family:system-ui,-apple-system,'Helvetica Neue',sans-serif;">
<table width="100%" cellpadding="0" cellspacing="0" style="background:#0a0a0a;padding:48px 16px;">
  <tr>
    <td align="center">
      <table width="560" cellpadding="0" cellspacing="0" style="max-width:560px;width:100%;background:#111;border:1px solid #2a2a2a;">

        <!-- Header -->
        <tr>
          <td style="padding:40px 48px 32px;border-bottom:1px solid #2a2a2a;">
            <span style="font-family:Georgia,'Times New Roman',serif;font-size:22px;color:#c8a96e;letter-spacing:0.06em;">Fujify</span>
          </td>
        </tr>

        <!-- Body -->
        <tr>
          <td style="padding:48px 48px 40px;">
            <p style="font-family:Georgia,serif;font-size:26px;font-weight:400;font-style:italic;color:#e8e0d4;line-height:1.3;margin:0 0 24px;">
              You're on the list.
            </p>
            <p style="font-size:15px;color:#a09890;line-height:1.7;margin:0 0 20px;">
              We'll email you when the Android beta is ready. It won't be long.
            </p>
            <p style="font-size:15px;color:#a09890;line-height:1.7;margin:0 0 32px;">
              In the meantime — keep shooting.
            </p>
            <table cellpadding="0" cellspacing="0" style="margin-bottom:40px;">
              <tr>
                <td style="background:#1a1a1a;border:1px solid #2a2a2a;padding:16px 24px;">
                  <span style="font-size:12px;letter-spacing:0.12em;text-transform:uppercase;color:#666;">Edit · Share · Keep</span>
                </td>
              </tr>
            </table>
          </td>
        </tr>

        <!-- Footer -->
        <tr>
          <td style="padding:24px 48px;border-top:1px solid #2a2a2a;">
            <p style="font-size:11px;color:#444;margin:0;line-height:1.6;">
              fujify.app · You signed up at <a href="https://fujify.app" style="color:#666;text-decoration:none;">fujify.app</a><br />
              Not you? Just ignore this email.
            </p>
          </td>
        </tr>

      </table>
    </td>
  </tr>
</table>
</body>
</html>`;
}
