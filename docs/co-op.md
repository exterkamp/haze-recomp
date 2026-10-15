# Experimental campaign co-op

Campaign co-op uses invite codes, direct WebRTC peer connections and TURN relay
fallback. It does not use PSN, Ubisoft accounts or friend lists. Each player must
run a compatible native build and have their own prepared BLUS30094 game files.

There is no hosted service configured by this repository. Before launching the
game, set `HAZE_COOP_SERVICE` to your deployed `wss://YOUR-SERVICE/v1` endpoint.
The [rendezvous service guide](../services/rendezvous/README.md) explains deployment
with TLS and a TURN server. This configuration belongs on the machine running
the launcher so its child game process inherits it. Do not share a TURN secret
with players. Certificate verification uses Windows trust roots; a private CA
can be supplied with `HAZE_COOP_CA_FILE`.

## Host and join

1. The host selects **Campaign → Host Co-op → Create invite code**.
2. Copy the code and share it with the other players. Select **Continue**, then
   continue a campaign or choose a chapter.
3. Each joining player selects **Campaign → Join Co-op → Enter invite code**.
   Type or paste the code with Ctrl+V. Controller users can also add or delete
   characters using the rows below the connection status.
4. Select **Connect to host**, wait for a connected status, then **Continue**.
   Select the host's game in the game list to join the campaign.

Codes are case-insensitive; spaces and hyphens are ignored. A code expires when
the host leaves its room or the service restarts. Back from the invite page
closes an unfinished session. Disconnect releases that session and allows a
new invite. Continue preserves the connection while opening the game's campaign
or game-list screen. A host departure ends the other players' campaign session.

For LAN play, choose **Local network** on the host and join pages, then Continue.
For single player, choose **Single Player Campaign**; no co-op service is needed.

The **Player Profile** entry on the main menu changes the displayed name and
persists it across restarts. Names contain 3–16 ASCII letters, digits, `_` or `-`,
starting with a letter. Existing saves keep their original internal identity.
The menu header and server list use the chosen name. The in-game teammate label
still displays `User` in the current local reproduction; this is an outstanding
profile-display issue.

## Validation and limits

Two local game instances joined through the native invite pages, loaded the
same campaign, and synchronized movement. The host survived client departure,
a fresh client rejoined the same invite, and host closure produced the game's
lost-connection dialog. A separate run with `PS3_NET_ONLINE=0` loaded the
single-player checkpoint without a co-op service. Copied save profiles remained
unchanged.

Transport tests cover four direct peers and two peers using a selected TURN
relay route. Secure signaling tests accept a trusted certificate and reject
an unrelated CA, an untrusted certificate and a hostname mismatch.

Separate Windows machines, home routers, four-player gameplay and four-peer
relay operation remain unvalidated. The Windows VM test is deferred. Networks
that block all UDP require additional TCP/TLS relay support. Competitive modes,
voice chat, browser integration and full-campaign completion are outside the
validated co-op behavior. See the [implementation record](native-network-investigation.md).
