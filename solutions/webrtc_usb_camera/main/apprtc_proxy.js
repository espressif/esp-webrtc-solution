#!/usr/bin/env node
/*
 * AppRTC proxy for local testing against webrtc.espressif.com origin checks.
 *
 * Usage:
 *   npm i ws
 *   node apprtc_proxy.js --port 8080
 *
 * Then open sender page with server/proxy set to:
 *   http://<your-ip>:8080
 */

const http = require('http');
const https = require('https');
const WebSocket = require('ws');

const DEFAULT_PORT = 8080;
const APPRTC_HTTP_HOST = 'webrtc.espressif.com';
const APPRTC_WS_HOST = 'webrtc.espressif.com:8089';
const APPRTC_ICE_HOST = 'webrtc.espressif.com:3033';
const APPRTC_WS_URL = 'wss://webrtc.espressif.com:8089/ws';
const APPRTC_ORIGIN = 'https://webrtc.espressif.com';

function getArgValue(flag, fallback) {
    const idx = process.argv.indexOf(flag);
    if (idx >= 0 && idx + 1 < process.argv.length) {
        return process.argv[idx + 1];
    }
    return fallback;
}

const PORT = Number(getArgValue('--port', DEFAULT_PORT));

function rewriteUrls(text, proxyHost) {
    if (typeof text !== 'string' || !text.includes('espressif.com')) {
        return text;
    }

    let result = text.replace(/wss:\/\/webrtc\.espressif\.com(:[0-9]+)?/g, `ws://${proxyHost}`);
    result = result.replace(/https:\/\/webrtc\.espressif\.com(:[0-9]+)?/g, `http://${proxyHost}`);
    result = result.replace(/webrtc\.espressif\.com(:[0-9]+)?/g, proxyHost);
    return result;
}

function buildForwardHeaders(req) {
    const useWsHost = req.method === 'DELETE';
    var headers = {
        ...req.headers,
        host: useWsHost ? APPRTC_WS_HOST : APPRTC_HTTP_HOST,
        origin: APPRTC_ORIGIN,
        referer: `${APPRTC_ORIGIN}/`,
    };
    const useICE = req.url.includes("iceconfig?key=");
    if (useICE) {
        headers.host = APPRTC_ICE_HOST;
    }

    if (req.url.includes('/leave/')) {
        const parts = req.url.split('/').filter(Boolean);
        if (parts.length >= 2) {
            const roomId = parts[parts.length - 2];
            headers.referer = `${APPRTC_ORIGIN}/r/${roomId}`;
        }
    }

    return headers;
}

const server = http.createServer((req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', '*');
    res.setHeader('Access-Control-Allow-Headers', '*');

    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }
    const useWsHost = req.method === 'DELETE';
    const useICE = req.url.includes("iceconfig?key=");

    options = {
        hostname: APPRTC_HTTP_HOST,
        port: useWsHost ? 8089 : 443,
        path: req.url,
        method: req.method,
        headers: buildForwardHeaders(req),
        rejectUnauthorized: false,
    };
    if (useICE) {
        options.hostname = APPRTC_HTTP_HOST;
        options.port = 3033;
    }

    console.log(`[Proxy][HTTP] ${req.method} ${req.url}`);

    const proxyReq = https.request(options, (proxyRes) => {
        const chunks = [];
        proxyRes.on('data', (chunk) => chunks.push(chunk));
        proxyRes.on('end', () => {
            let body = Buffer.concat(chunks).toString();
            body = rewriteUrls(body, req.headers.host);

            const outHeaders = { ...proxyRes.headers };
            delete outHeaders['content-length'];

            res.writeHead(proxyRes.statusCode || 500, outHeaders);
            res.end(body);
        });
    });

    proxyReq.on('error', (err) => {
        console.error('[Proxy][HTTP] error:', err.message);
        res.writeHead(500);
        res.end('proxy http error');
    });

    req.pipe(proxyReq);
});

const wss = new WebSocket.Server({ server });

wss.on('connection', (browserWs, req) => {
    const browserHost = req.headers.host;
    console.log(`[Proxy][WS] browser connected host=${browserHost}`);

    const targetWs = new WebSocket(APPRTC_WS_URL, {
        headers: {
            origin: APPRTC_ORIGIN,
            host: APPRTC_WS_HOST,
        },
        rejectUnauthorized: false,
    });

    targetWs.on('open', () => {
        console.log('[Proxy][WS] connected to AppRTC');
        if (browserWs.readyState === WebSocket.OPEN) {
            browserWs.send(JSON.stringify({ cmd: 'proxy_ready' }));
        }
    });

    targetWs.on('message', (data) => {
        if (browserWs.readyState === WebSocket.OPEN) {
            browserWs.send(rewriteUrls(data.toString(), browserHost));
        }
    });

    browserWs.on('message', (data) => {
        if (targetWs.readyState === WebSocket.OPEN) {
            targetWs.send(data);
        }
    });

    const closeBoth = () => {
        if (browserWs.readyState <= WebSocket.OPEN) {
            browserWs.close();
        }
        if (targetWs.readyState <= WebSocket.OPEN) {
            targetWs.close();
        }
    };

    browserWs.on('close', closeBoth);
    targetWs.on('close', closeBoth);

    browserWs.on('error', (err) => console.error('[Proxy][WS] browser error:', err.message));
    targetWs.on('error', (err) => console.error('[Proxy][WS] target error:', err.message));
});

server.listen(PORT, '0.0.0.0', () => {
    console.log('=====================================================');
    console.log('AppRTC CORS/Origin Proxy');
    console.log(`Listening: http://0.0.0.0:${PORT}`);
    console.log('Rewrites webrtc.espressif.com URLs to proxy host for browser-side signaling.');
    console.log('=====================================================');
});
