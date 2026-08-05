// Vercel serverless function — lives at api/proxy.js in your project root.
// Vercel auto-detects anything in /api as a function, no config needed.
//
// SETUP:
// 1. Vercel dashboard -> your project -> Settings -> Environment Variables -> add:
//      SUPABASE_URL         = https://evfkddxmaadmsampxxdt.supabase.co
//      SUPABASE_KEY         = <your anon/publishable key>       (messages table)
//      SUPABASE_SERVICE_KEY = <your service_role key>            (accounts table -- get from
//                                                                  Supabase Settings -> API.
//                                                                  Keep this one secret, never
//                                                                  send it to the browser.)
//    (Add all three to Production, Preview, and Development.)
// 2. Redeploy (env var changes need a redeploy to take effect).
// 3. Run the SQL in supabase-setup.sql once, in your Supabase project's
//    SQL editor, to create the messages table + access policies.
// 4. Your function is live at: https://your-project.vercel.app/api/proxy
//    Same-origin if index.html is served from the same Vercel project, so
//    the frontend just calls "/api/proxy" with no URL/key to configure.

import { randomUUID, randomBytes, scryptSync, timingSafeEqual } from 'crypto';

export default async function handler(req, res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Headers', 'content-type');
  if (req.method === 'OPTIONS') return res.status(200).end();

  const service = String(req.query.service || '').toLowerCase();

  try {
    if (service === 'supabase') {
      const { status, data } = await handleSupabase(req);
      return res.status(status).json(data);
    }
    if (service === 'account') {
      const { status, data } = await handleAccount(req);
      return res.status(status).json(data);
    }
    return res.status(400).json({ error: 'Unknown or missing service: ' + service });
  } catch (err) {
    return res.status(500).json({ error: String(err) });
  }
}

// Generic Supabase passthrough. The client sends
// { path, method, body, headers } where path is anything under Supabase's
// REST API (e.g. '/rest/v1/messages?order=created_at.desc&limit=50').
// This injects the real apikey/Authorization headers server-side, so the
// key never reaches the browser, and forwards Supabase's actual status
// code + body back untouched.
// Applied only to messages saved without a recipient_peer_id (i.e. the
// shared/global log everyone sees) -- private P2P DMs are left alone.
// Same idea as the username blocklist in your ChillCord proxy: this has
// to live server-side, since a bot hitting this endpoint directly has no
// client-side JS to bypass.
const MESSAGE_BLOCKLIST = ['fuck','shit','bitch','cunt','asshole','bastard','dick','pussy','whore','slut','nigger','nigga','faggot','fag','retard','rape','nazi','hitler'];
function containsBlockedWord(str) {
  const normalized = String(str || '').toLowerCase().replace(/[^a-z0-9]/g, '');
  return MESSAGE_BLOCKLIST.some(w => normalized.includes(w));
}

// ---------------------------------------------------------------------
// Accounts: username/password/device-token identity, backed by the
// `accounts` table. Uses SUPABASE_SERVICE_KEY (service_role, bypasses
// RLS) rather than the anon key -- this table is never touched directly
// by the browser, only through this proxy.
// ---------------------------------------------------------------------

function hashPassword(password) {
  const salt = randomBytes(16).toString('hex');
  const hash = scryptSync(password, salt, 64).toString('hex');
  return `${salt}:${hash}`;
}

function verifyPassword(password, stored) {
  const [salt, hash] = String(stored || '').split(':');
  if (!salt || !hash) return false;
  const candidate = scryptSync(password, salt, 64);
  const expected = Buffer.from(hash, 'hex');
  if (candidate.length !== expected.length) return false;
  return timingSafeEqual(candidate, expected);
}

async function supaFetch(path, options) {
  const supaUrl = process.env.SUPABASE_URL;
  const serviceKey = process.env.SUPABASE_SERVICE_KEY;
  if (!supaUrl || !serviceKey) {
    throw new Error('SUPABASE_URL/SUPABASE_SERVICE_KEY not set in Vercel env vars');
  }
  const r = await fetch(`${supaUrl}${path}`, {
    ...options,
    headers: {
      apikey: serviceKey,
      Authorization: `Bearer ${serviceKey}`,
      'Content-Type': 'application/json',
      ...(options && options.headers),
    },
  });
  const text = await r.text();
  let data;
  try { data = text ? JSON.parse(text) : null; } catch (e) { data = null; }
  return { ok: r.ok, status: r.status, data };
}

async function findAccount(username) {
  const { data } = await supaFetch(
    `/rest/v1/accounts?username=eq.${encodeURIComponent(username)}&select=*`,
    { method: 'GET' }
  );
  return Array.isArray(data) && data.length ? data[0] : null;
}

async function handleAccount(req) {
  const action = String(req.query.action || '').toLowerCase();
  const { username, password, color, token, peer_id } = req.body || {};

  if (!username || typeof username !== 'string' || !username.trim()) {
    return { status: 400, data: { error: 'Missing username' } };
  }

  if (action === 'check') {
    const existing = await findAccount(username);
    return { status: 200, data: { exists: !!existing } };
  }

  if (action === 'register') {
    if (!password) return { status: 400, data: { error: 'Missing password' } };
    const existing = await findAccount(username);
    if (existing) {
      return { status: 409, data: { error: 'Username already exists' } };
    }
    const newToken = randomUUID();
    const { ok, data } = await supaFetch('/rest/v1/accounts', {
      method: 'POST',
      headers: { Prefer: 'return=representation' },
      body: JSON.stringify({
        username,
        password_hash: hashPassword(password),
        device_token: newToken,
        color: color || null,
      }),
    });
    if (!ok) return { status: 500, data: { error: 'Failed to create account', details: data } };
    return { status: 200, data: { token: newToken, username } };
  }

  if (action === 'claim') {
    if (!password) return { status: 400, data: { error: 'Missing password' } };
    const existing = await findAccount(username);
    if (!existing) return { status: 404, data: { error: 'No account with that username' } };
    if (!verifyPassword(password, existing.password_hash)) {
      return { status: 403, data: { error: 'Incorrect password' } };
    }
    return { status: 200, data: { token: existing.device_token, username } };
  }

  if (action === 'verify-token') {
    if (!token) return { status: 400, data: { error: 'Missing token' } };
    const existing = await findAccount(username);
    if (!existing) return { status: 200, data: { valid: false } };
    return { status: 200, data: { valid: existing.device_token === token } };
  }

  if (action === 'get-peer') {
    if (!token) return { status: 400, data: { error: 'Missing token' } };
    const existing = await findAccount(username);
    if (!existing) return { status: 404, data: { error: 'No account with that username' } };
    if (existing.device_token !== token) {
      return { status: 403, data: { error: 'Invalid token' } };
    }
    return { status: 200, data: { peer_id: existing.peer_id || '' } };
  }

  if (action === 'save-peer') {
    if (!token) return { status: 400, data: { error: 'Missing token' } };
    if (!peer_id || typeof peer_id !== 'string') {
      return { status: 400, data: { error: 'Missing peer_id' } };
    }
    const existing = await findAccount(username);
    if (!existing) return { status: 404, data: { error: 'No account with that username' } };
    if (existing.device_token !== token) {
      return { status: 403, data: { error: 'Invalid token' } };
    }
    const { ok, data } = await supaFetch(
      `/rest/v1/accounts?username=eq.${encodeURIComponent(username)}`,
      {
        method: 'PATCH',
        headers: { Prefer: 'return=representation' },
        body: JSON.stringify({ peer_id }),
      }
    );
    if (!ok) return { status: 500, data: { error: 'Failed to save peer id', details: data } };
    return { status: 200, data: { ok: true, peer_id } };
  }

  return { status: 400, data: { error: 'Unknown action: ' + action } };
}

async function handleSupabase(req) {
  const supaUrl = process.env.SUPABASE_URL;
  const supaKey = process.env.SUPABASE_KEY;
  if (!supaUrl || !supaKey) {
    return { status: 500, data: { error: 'SUPABASE_URL/SUPABASE_KEY not set in Vercel env vars' } };
  }

  const { path, method, body, headers: extraHeaders } = req.body || {};
  if (!path || typeof path !== 'string' || !path.startsWith('/rest/v1/')) {
    return { status: 400, data: { error: 'Missing or invalid path (must start with /rest/v1/)' } };
  }

  if (path.startsWith('/rest/v1/messages') && (method || 'GET').toUpperCase() === 'POST') {
    let parsedBody = null;
    try { parsedBody = body ? JSON.parse(body) : null; } catch (e) { /* leave null */ }
    const isGlobal = !parsedBody || !parsedBody.recipient_peer_id;
    if (isGlobal && parsedBody && containsBlockedWord(parsedBody.body)) {
      return { status: 400, data: { error: 'Message blocked by content filter' } };
    }
  }

  const fetchHeaders = {
    apikey: supaKey,
    Authorization: `Bearer ${supaKey}`,
    ...(extraHeaders || {}),
  };
  if (body) fetchHeaders['Content-Type'] = 'application/json';

  const upstream = await fetch(`${supaUrl}${path}`, {
    method: method || 'GET',
    headers: fetchHeaders,
    body: body || undefined,
  });

  const text = await upstream.text();
  let data;
  try { data = text ? JSON.parse(text) : {}; } catch (e) { data = { raw: text }; }
  return { status: upstream.status, data };
}
