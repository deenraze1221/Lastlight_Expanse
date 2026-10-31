# Multiplayer: Render or LAN

Both modes use the same four-player lobby and eight-digit room codes.
All players must connect to the same server. Solo play needs no server.

## Internet / Render

1. On dashboard.render.com create a **Blueprint**, connect this repository and select main. The included render.yaml builds and starts the game server. Alternatively create a Node Web Service: build command `npm ci && npm run build`, start command `npm run server`.
2. Open the resulting https://YOUR-SERVICE.onrender.com address. The game and WebSocket server run on the same service; the address is selected automatically.
3. For a separately downloaded game, choose **Settings → Connection → Internet**, enter `YOUR-SERVICE.onrender.com` or `wss://YOUR-SERVICE.onrender.com`. It is saved locally.
4. Create a lobby, select Open for network and share the code. Friends choose Join by code on the same server.

The Render dashboard URL is not a game server address. Free services may sleep; allow time for the first connection. Restarting the service clears rooms. No Render account credentials are entered in the game.

## Local network without Render

Requires Node.js 22.12+ on one computer only. On Windows double-click **start-lan.bat**. Or run from the project directory:

```sh
npm ci
npm run build
npm run server
```

The host opens http://localhost:3000. Find the host's private IPv4 address using ipconfig (Windows) or ip addr (Linux). Friends on the same router/Wi-Fi open e.g. **http://192.168.1.10:3000**; no installation is required for them. The server address is automatic. In manual settings choose LAN and enter 192.168.1.10:3000.

Create/open the lobby and join using its code. Keep the server console open. Allow Node.js/TCP port 3000 on the private-network firewall. Guest Wi-Fi isolation can prevent connections. No public port forwarding is needed.

Use the HTTP game served by the LAN host: browsers block insecure ws:// connections from an HTTPS game. This is a local server mode, not serverless peer-to-peer or automatic LAN discovery.
