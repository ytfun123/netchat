# netchat

Calculator-side chat program. Talks over a **custom USB link** (not TI's
file-transfer protocol) using two bulk endpoints -- see `src/linklib.c`,
adapted from the CE toolchain's own `link_library` example.

## Protocol

- Newline-delimited text lines, both directions.
- On connect, the browser side and this program exchange a fixed key
  string (`netcalc-chat-v1`, set in the makefile's `LINKLIB_KEY`) to
  confirm they speak the same protocol version. Bump this string on both
  sides if the wire format ever changes incompatibly.
- The calculator doesn't know or care what happens after the browser
  receives a line -- relaying it to a real chat server is entirely the
  browser's job.

## Building

Same as netcalc -- requires `CEDEV`/`PATH` pointed at your CEdev install:

```bash
make
```

Output: `bin/NETCHAT.8xp`. Send it via the flasher site like anything else.

## Known limitation (v1)

Pressing Enter opens the OS's own text-entry line (`os_GetStringInput`),
which is much more reliable than hand-rolling alpha-key input, but it
blocks the main loop while you're typing. Any incoming message just queues
up on the USB side and appears the moment you finish typing -- it's not
lost, just delayed. Fine for now; a fully custom non-blocking input editor
is a nice v2 upgrade if it turns out to feel laggy in practice.

## Status

- [x] Compiles clean, valid .8xp
- [ ] Confirmed working over real WebUSB with the browser side
- [ ] Browser-side raw WebUSB client (next step)
- [ ] Real chat server behind it

## Setup wizard (new)

First launch walks through username / color / password, saved to an on-calc
AppVar (`NCCFG`) so it only runs once. The chosen username + color are sent
automatically as a `#CONFIG#username=...;color=...` line the moment the USB
link connects -- the browser side picks this up to label your messages.

The password field is stored but **not used for anything yet** -- there's no
account server to check it against. It's there for later, not enforced now.

## chat.html (new, replaces chat-test.html)

Full restyled UI: dark theme, colored message bubbles keyed to your calc's
chosen color, plus direct (P2P) messaging between two people using PeerJS
over its free public signaling broker. Each browser gets a random Peer ID;
share it with a friend, they paste it in and hit Connect, and messages flow:

```
your calc -> your browser (USB) -> WebRTC data channel -> their browser -> their calc (USB)
```

No server ever sees message content -- the public broker only helps the two
browsers find each other, actual data goes peer-to-peer after that.

### Known limitations / next steps
- Only one peer connection at a time (no group chat yet)
- Peer IDs are random and don't persist across page reloads -- fine for now,
  a "claim a friendly ID" feature would be a nice follow-up
- No reconnect-on-drop for the P2P side yet

## Message history (Supabase, new)

Every message -- from the calc, typed in the browser composer, or received
over P2P -- gets saved to a Supabase table via `api/proxy.js`, a Vercel
serverless function that injects your Supabase key server-side (same
pattern as your ChillCord proxy, trimmed down to just the Supabase piece).
The key never reaches the browser.

### One-time setup
1. In your Supabase project's SQL editor, run `supabase-setup.sql` (creates
   the `messages` table + row-level-security policies).
2. In Vercel: Settings -> Environment Variables, add:
   - `SUPABASE_URL` = `https://evfkddxmaadmsampxxdt.supabase.co`
   - `SUPABASE_KEY` = your anon/publishable key
   (add to Production, Preview, and Development)
3. Put `api/proxy.js` at your project root (Vercel auto-detects anything
   under `/api`). Redeploy.

On page load, the last 50 messages get pulled in automatically once your
Peer ID is assigned (needed to correctly tell "yours" from "theirs" in the
UI). Every new message after that gets saved immediately as it's sent or
received.

### Known limitations
- Single shared history table -- not yet scoped per-friend-conversation,
  though the schema (`sender_peer_id`/`recipient_peer_id`) supports
  filtering that in later
- No pagination past the most recent 50
- No delete/edit yet (append-only)

## Better GUI, live typing, settings, filter (new)

### Calc side
- **Non-blocking input** -- typing no longer freezes the screen. Incoming
  messages appear live while you're composing, matching the actual TI-84 CE
  alpha keyboard layout (verified against the toolchain's own
  `link_library` example, not guessed -- see `src/input.c`'s comment).
  `[alpha]` toggles letters/digits, `[del]` backspaces, `[clear]` clears
  the line (or quits if pressed twice on an empty line).
- **Settings menu** -- press `[mode]` any time to change username, color,
  or password individually, without re-running the whole wizard.
- **Typing indicator** -- sends `#TYPING#<name>` / `#STOPTYPING#` control
  lines automatically as you type; shows the other person's typing status
  top-right, if they're doing the same.
- Your own sent messages now show in your chosen wizard color.

### Browser side (chat.html)
- Relays typing indicators between P2P peers, shows "X is typing..." above
  the composer, and forwards it back down to your calc too.

### Backend (api/proxy.js)
- **Bad-word filter for global chat** -- messages saved *without* an
  active P2P recipient (i.e. the shared history table) get checked against
  a blocklist server-side, so it can't be bypassed by calling the proxy
  directly. Private P2P DMs are not filtered.

### Known limitations / next steps
- Typing indicator has no timeout -- if a client disconnects mid-type
  without sending #STOPTYPING#, the indicator can get stuck. Minor, worth
  a timeout-based fix later.
- Settings menu step counter always shows "step 1 of 3" even when changing
  a single field -- cosmetic only.
- Servers/groups (multi-person chat) not built yet -- next up, needs an
  architecture decision (star/hub topology via one member relaying, most
  likely) before implementation.

## Discord-style typing dots + Global Chat tab (new)

### Typing indicator
Replaced the text-based indicator with an actual animated 3-dot bounce
(CSS on the browser side, an animated dot cluster next to the status LED
on the calc side) -- shows only while the other person is actively typing,
nothing otherwise, matching Discord's behavior.

### Global Chat tab
`chat.html` now has two tabs above the message log:
- **Direct Message** -- the existing P2P flow, unchanged
- **Global Chat** -- everyone who opens the site sees the same shared log,
  saved to Supabase with `recipient_peer_id = null`, polled every 4s for
  new messages, subject to the bad-word filter already in `api/proxy.js`

Whichever tab is active determines where calc-typed messages go too --
the calc doesn't need its own concept of "which chat," it just sends
whatever the browser is currently showing.

### Calc GUI redesign
- Colored header bar (title + status LED dot instead of text)
- Left accent strip on the compose bar in your chosen wizard color
- Animated typing-dots cluster near the status area

### Known limitations
- Global chat polls every 4s rather than using realtime push -- fine for
  casual use, would need Supabase Realtime (or similar) for instant delivery
- Typing-dot animation timing on the calc is tuned by feel (a loop-iteration
  counter, not a real clock) -- let me know if it looks too fast/slow

## Account system: unique usernames + device tokens (new)

### How it works
1. Calc's `#CONFIG#` line now includes `token=` (empty until assigned) and
   `password=` (sent once, over the local USB link only).
2. Browser side, on first-ever connect (empty token):
   - Checks Supabase (via `api/proxy.js`, server-side) if the username is
     taken.
   - Not taken -> registers it, gets back a device token, sends
     `#SETID#<token>` down to the calc, which saves it to the `NCCFG`
     AppVar permanently.
   - Taken -> shows a modal: enter that account's password to claim it
     (useful if you reset your calc or use a second one), or pick a
     different username entirely (loops back through the same check --
     picking another already-taken name just re-shows the modal again).
3. On every future connect, the calc sends its saved token. The browser
   verifies it server-side and skips the modal entirely for the normal
   "same calc reconnecting" case.

### Security notes
- Passwords are **never** stored or compared in plaintext. `api/proxy.js`
  hashes with Node's built-in `scrypt` (random salt per account) and
  compares with a timing-safe check.
- The password does travel calc -> browser in plaintext once, but only
  over the physical USB link you already control -- it never touches the
  network unhashed.
- Account reads/writes use a **separate, more privileged Supabase key**
  (`SUPABASE_SERVICE_KEY`, your service_role key) than the messages table
  does, and go entirely through the proxy -- the browser never talks to
  the `accounts` table directly, so there's no anon RLS policy that could
  leak password hashes.

### New setup step
Add `SUPABASE_SERVICE_KEY` (your service_role key, Supabase Settings ->
API -- different from the anon key you already have) to Vercel's env
vars, and re-run `supabase-setup.sql` (it's additive -- creates the new
`accounts` table without touching `messages`).

### Known limitations
- No "forgot password" flow -- if you lose the password for a claimed
  username, that name is stuck (would need an email-based reset, real
  scope creep for now)
- No rate-limiting on password guesses yet -- fine for casual use, would
  matter more with wider adoption

## Login gating (fixed)

The status LED next to "Calculator" is now three-state:
- **Red** -- nothing attached (USB not up)
- **Yellow** -- calculator attached but the account/password hasn't been
  confirmed yet
- **Green** -- signed in

Sending from the calc while yellow shows "Log in first..." instead of
relaying; the browser composer is blocked too. The account flow only turns
the dot green once the `#CONFIG#` token/username round-trip actually
succeeds (or when `ACCOUNTS_ENABLED` is off).

## Background images (new)

Upload a picture from `chat.html` and it gets pushed over the USB link to
the calc as a 320x240 background, stored on-calc and selectable per-session.

### How it works
1. On connect, the calc sends its fixed 20-color **image palette** as a
   `#PAL#<n>#<idx:rrggbb,...>` line (the 8 accent colors + 12 settings
   picker colors). The browser stores it.
2. The upload flow: pick a slot (0-5), choose an image file. The browser
   scales it to 320x240 (cover-fit, black letterbox), maps every pixel to
   the nearest of the 20 palette colors, and streams the resulting 76,800
   index bytes as hex:
   - `#BGIMG#<slot>|<name>` -- start (name <= 16 chars)
   - `#BGDATA#<hex...>` -- chunks of 292 bytes (592 hex chars) per line
   - `#BGEND#` -- finalize + archive
   - `#BGCANCEL#` -- abort (also sent on a failed upload)
   Lines are paced so the calc's single-buffered USB receive never overruns.
3. The calc streams the bytes straight into a pair of AppVars
   (`BGIMG<n>A` = name + top half, `BGIMG<n>B` = bottom half) -- each fits
   under the CE's 64KB-per-variable limit -- and archives them on `#BGEND#`.
4. Calc-side management: Settings -> **8: Background image**. Lists the six
   slots with their names, `[up]/[down]` to move, `[enter]` to set the
   active background, `[del]` to delete one. The active slot is saved in the
   `NCCGUI` AppVar (`bg_image` byte, appended so older vars still load).
5. The browser learns which slots are used via `#BGLIST#` / `#BGSLOTS#`
   (sent on every connect) so the slot picker can show free/used.

### Notes
- The image is drawn through the calc's live palette (pixels are palette
  indices, no palette travels with the image), dimmed with the theme
  background so message text stays readable.
- Messages, typing indicators, and the composer are paused while a receive
  is in flight; the calc ignores stray lines mid-upload.
- No image -> the theme/custom background color is used as before.
