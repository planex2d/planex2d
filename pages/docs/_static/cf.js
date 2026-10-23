// --- Config ---
const ANALYTICS_ENDPOINT = 'https://planex2d.org/p';

// --- UUID ---
function generateUUID() {
    let d = Date.now();
    let d2 = (performance?.now?.() || 0) * 1000;
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => {
        let r = Math.random() * 16;
        if (d > 0) {
            r = (d + r) % 16 | 0;
            d = Math.floor(d / 16);
        } else {
            r = (d2 + r) % 16 | 0;
            d2 = Math.floor(d2 / 16);
        }
        return (c === 'x' ? r : (r & 0x3 | 0x8)).toString(16);
    });
}

function setOptOut() {
    document.cookie = 'analytics_optout=1; max-age=31536000; path=/; SameSite=Lax';
    alert('opt-out cookie set.');
}

// --- Bot Detection ---
function botDetected() {
    if (new URLSearchParams(window.location.search).get('knownBot') === '1') return true;

    if (document.cookie.includes('analytics_optout=1')) {
        console.log("opt-out.")
        return true;
    }

    const ua = navigator.userAgent || '';

    if (/chatgpt-user|claude-user|perplexity-user/i.test(ua)) return false;

    const isAutomated = !!navigator.webdriver;
    const bots = /bot|crawl|spider|google|bing|baidu|yandex|duckduckgo|facebook|slurp|exabot|facebot|scraper|headless|puppeteer|playwright|selenium|phantomjs|prerender|rendertron|screenshot|preview|facebookexternalhit|twitterbot|linkedinbot|slackbot|discordbot|are\.na|arena|microlink|diffbot|iframely|PTST|lighthouse|gptbot|chatgpt|claudebot|claude-searchbot|oai-searchbot|perplexitybot|anthropic-ai|anthropic|claude/i;

    return isAutomated || bots.test(ua);
}

// Sphinx: pages are /foo/bar.html, so keep the file name.
// Only /foo/index.html is folded into /foo/ (same page as the directory URL).
function normalizePath(path) {
    if (path.endsWith('/index.html')) return path.slice(0, -'index.html'.length);
    if (path.endsWith('/')) return path;
    const last = path.split('/').pop();
    if (last.includes('.')) return path;
    return path + '/';
}

// --- Core ---
const pageUUID = generateUUID();
const pagePath = normalizePath(window.location.pathname);

function sendInit() {
    fetch(ANALYTICS_ENDPOINT + '/init', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            uuid: pageUUID,
            page: pagePath,
            referrer: document.referrer || ''
        })
    });
}

function sendHeartbeat(delta) {
    fetch(ANALYTICS_ENDPOINT + '/heartbeat', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ uuid: pageUUID, delta })
    });
}

function sendEvent(type, url, anchorText) {
    navigator.sendBeacon(
        ANALYTICS_ENDPOINT + '/event',
        new Blob([JSON.stringify({
            uuid: pageUUID,
            type: type,
            url: url || '',
            anchor_text: anchorText || '',
            page: pagePath
        })], { type: 'application/json' })
    );
}

// --- Section time (Sphinx sections) ---
// Sphinx wraps every heading in <section id="..."> (or <div class="section" id="..."> on older docutils).
let currentSection = null;
let lastSectionTime = Date.now();

function sectionHeading(section) {
    const h = section.querySelector('h1, h2, h3, h4, h5, h6');
    return h ? h.textContent.replace('¶', '').trim() : section.id;
}

function trackSection() {
    let current = null;
    document.querySelectorAll('section[id], div.section[id]').forEach(s => {
        if (s.getBoundingClientRect().top <= 120) current = s;
    });
    if (!current || current === currentSection) return;

    const now = Date.now();
    if (currentSection) {
        // bucket to 30s intervals
        const seconds = Math.round((now - lastSectionTime) / 30000) * 30;
        sendEvent('section_time', '#' + currentSection.id, sectionHeading(currentSection) + '|' + seconds);
    }
    currentSection = current;
    lastSectionTime = now;
}

// --- Scroll depth ---
const scrollDepthFired = new Set();
const scrollDepthMarkers = [20, 40, 60, 80, 100];

window.addEventListener('scroll', function () {
    if (botDetected()) return;
    const scrollTop = document.documentElement.scrollTop;
    const docHeight = Math.max(
        document.documentElement.scrollHeight,
        document.body.scrollHeight
    ) - window.innerHeight;

    trackSection();

    if (docHeight <= 0) return;

    const scrollPercent = Math.round((scrollTop / docHeight) * 100);

    scrollDepthMarkers.forEach(marker => {
        if (!scrollDepthFired.has(marker) && scrollPercent >= marker) {
            scrollDepthFired.add(marker);
            sendEvent('scroll_depth', '', String(marker));
        }
    });
}, { passive: true });

// --- Init & Heartbeat ---
window.addEventListener('DOMContentLoaded', function () {
    if (botDetected()) return;

    sendInit();
    let heartbeatCount = 0;
    const MAX_HEARTBEATS = 5;
    const MAX_HEARTBEATS_SUPER_SLOW = 160;

    const slow = setInterval(() => {
        if (heartbeatCount >= MAX_HEARTBEATS) {
            clearInterval(slow);

            const superSlow = setInterval(() => {
                if (heartbeatCount >= MAX_HEARTBEATS_SUPER_SLOW) {
                    clearInterval(superSlow);
                    return;
                }
                sendHeartbeat(30);
                heartbeatCount++;
            }, 30000);

            return;
        }
        sendHeartbeat(5);
        heartbeatCount++;
    }, 5000);

    // --- Link Click Tracking ---
    document.querySelectorAll('a').forEach(function (link) {
        link.addEventListener('mousedown', function () {
            const rawHref = link.getAttribute('href') || '';
            const text = link.textContent.replace('¶', '').trim();

            let url;
            try {
                url = new URL(link.href);
            } catch {
                return;
            }

            let eventType;

            if (rawHref.match(/\.(pdf|zip|stl|step|docx|xlsx)$/i) && url.hostname === window.location.hostname) {
                eventType = 'download_click';
            } else if (url.hostname !== window.location.hostname) {
                eventType = 'outbound_click';
            } else {
                eventType = 'internal_click';
            }

            sendEvent(eventType, url.href, text);
        });
    });
});

// --- Copy (code and text) ---
document.addEventListener('copy', function () {
    if (botDetected()) return;
    const selection = window.getSelection();
    const text = selection?.toString().trim() || '';
    const isCode = selection?.anchorNode?.parentElement?.closest('pre, code') != null;
    sendEvent(isCode ? 'code_copy' : 'copy', window.location.href, text.substring(0, 100));
});