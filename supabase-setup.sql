-- Run this once in your Supabase project (Dashboard -> SQL Editor -> New query).
-- It creates the two tables the proxy needs: `messages` (public chat log,
-- accessed with the anon key via RLS) and `accounts` (username/password/device
-- token, only ever touched by the proxy with the service_role key).

-- ---------------------------------------------------------------------------
-- messages
-- ---------------------------------------------------------------------------
create table if not exists public.messages (
  id bigint generated always as identity primary key,
  created_at timestamptz not null default now(),
  sender_peer_id text,
  recipient_peer_id text,
  username text not null default '',
  color text,
  body text not null default ''
);

-- Make sure every column the frontend/proxy uses exists, even if an older
-- version of this table was created earlier.
alter table public.messages add column if not exists sender_peer_id text;
alter table public.messages add column if not exists recipient_peer_id text;
alter table public.messages add column if not exists username text not null default '';
alter table public.messages add column if not exists color text;
alter table public.messages add column if not exists body text not null default '';

create index if not exists messages_recipient_idx on public.messages (recipient_peer_id, created_at);
create index if not exists messages_global_idx on public.messages (created_at) where recipient_peer_id is null;

alter table public.messages enable row level security;

-- The proxy calls Supabase with the ANON key for messages, so RLS must allow
-- anon/authenticated to insert and select. (The blocklist runs in the proxy
-- before this is ever reached.)
drop policy if exists "anon can insert messages" on public.messages;
create policy "anon can insert messages" on public.messages
  for insert to anon, authenticated
  with check (true);

drop policy if exists "anon can select messages" on public.messages;
create policy "anon can select messages" on public.messages
  for select to anon, authenticated
  using (true);

-- ---------------------------------------------------------------------------
-- accounts
-- ---------------------------------------------------------------------------
create table if not exists public.accounts (
  id uuid primary key default gen_random_uuid(),
  created_at timestamptz not null default now(),
  username text not null unique,
  password_hash text not null,
  device_token text not null,
  color text,
  peer_id text
);

alter table public.accounts add column if not exists password_hash text not null default '';
alter table public.accounts add column if not exists device_token text not null default '';
alter table public.accounts add column if not exists color text;
alter table public.accounts add column if not exists peer_id text;

alter table public.accounts enable row level security;

-- Newer Supabase projects don't auto-grant privileges on new tables, which
-- causes "permission denied for table accounts" (42501). Grant explicitly.
grant select, insert, update, delete on public.accounts to service_role;
grant select, insert on public.messages to anon, authenticated, service_role;
grant usage, select on all sequences in schema public to anon, authenticated, service_role;

-- IMPORTANT: no INSERT/SELECT policies for anon here. The proxy uses the
-- SUPABASE_SERVICE_KEY (service_role), which bypasses RLS entirely. That keeps
-- usernames/password hashes unreachable from the browser.
-- The `username unique` constraint above is what enforces one-username-per-user.
