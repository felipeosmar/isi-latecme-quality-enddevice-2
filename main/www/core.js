// LoRaWAN Sensor Node - Core Module
// Handles: Utilities, API, Toast, Navigation, Lazy Loading

// ============================================================================
// Module Registry
// ============================================================================

const modules = {
    sensors: { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    lorawan: { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    system:  { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    tasks:   { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    config:  { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    files:   { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    ota:     { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
};

// ============================================================================
// Poll Manager
// ============================================================================

const pollManager = {
    activeTab: null,
    timer: null,

    start(tabName) {
        const mod = modules[tabName];
        if (!mod || !mod.pollFn) return;
        this.stop();
        this.activeTab = tabName;
        this.timer = setInterval(mod.pollFn, mod.pollInterval);
    },

    stop() {
        if (this.timer) {
            clearInterval(this.timer);
            this.timer = null;
        }
    }
};

// Pausa ao esconder a página (tela bloqueada, outra aba do browser, etc.)
document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
        pollManager.stop();
    } else if (pollManager.activeTab) {
        pollManager.start(pollManager.activeTab);
    }
});

// ============================================================================
// Utilities
// ============================================================================

function toast(msg, type = 'info') {
    const t = document.getElementById('toast');
    t.textContent = msg;
    t.className = 'show ' + type;
    setTimeout(() => t.className = '', 2500);
}

function formatUptime(ms) {
    const s = Math.floor(ms / 1000);
    const m = Math.floor(s / 60);
    const h = Math.floor(m / 60);
    if (h > 0) return `${h}h ${m % 60}m`;
    if (m > 0) return `${m}m ${s % 60}s`;
    return `${s}s`;
}

function formatBytes(b) {
    if (b < 1024) return b + ' B';
    return (b / 1024).toFixed(1) + ' KB';
}

// ============================================================================
// API Helper
// ============================================================================

async function api(endpoint, method = 'GET', data = null) {
    try {
        const opts = { method, headers: { 'Content-Type': 'application/json' } };
        if (data) opts.body = JSON.stringify(data);
        const res = await fetch('/api/' + endpoint, opts);

        // Any successful response means connection is working
        connectionState.connected = true;
        connectionState.lastSuccessfulPing = Date.now();

        return await res.json();
    } catch (e) {
        // Network error - trigger reconnection if we were connected
        if (connectionState.connected) {
            toast('Communication error', 'error');
            startReconnection();
        }

        throw e;
    }
}

// ============================================================================
// Connection State Management
// ============================================================================

const connectionState = {
    connected: true,
    reconnecting: false,
    retryCount: 0,
    lastSuccessfulPing: null
};

let reconnectionTimer = null;

function updateConnectionBanner() {
    const banner = document.getElementById('connection-banner');
    if (!banner) return;

    if (connectionState.connected) {
        banner.className = 'connection-banner hidden';
        banner.textContent = '';
    } else if (connectionState.reconnecting) {
        banner.className = 'connection-banner show reconnecting';
        banner.textContent = `Connection lost. Reconnecting... (attempt ${connectionState.retryCount})`;
    } else {
        banner.className = 'connection-banner show disconnected';
        banner.textContent = 'Connection lost';
    }
}

function startReconnection() {
    if (connectionState.reconnecting) return;

    connectionState.connected = false;
    connectionState.reconnecting = true;
    connectionState.retryCount = 0;

    updateConnectionBanner();

    function scheduleNextAttempt() {
        if (connectionState.connected) return;

        connectionState.retryCount++;
        updateConnectionBanner();

        checkConnection().then(isConnected => {
            if (isConnected) {
                connectionState.connected = true;
                connectionState.reconnecting = false;
                connectionState.retryCount = 0;
                connectionState.lastSuccessfulPing = Date.now();
                updateConnectionBanner();

                // Show success briefly
                const banner = document.getElementById('connection-banner');
                if (banner) {
                    banner.className = 'connection-banner show connected';
                    banner.textContent = 'Connection restored';
                    setTimeout(() => updateConnectionBanner(), 2000);
                }
            } else {
                // Exponential backoff: 1s, 1.5s, 2.25s, 3.375s, ... max 30s
                const delay = Math.min(1000 * Math.pow(1.5, connectionState.retryCount - 1), 30000);
                reconnectionTimer = setTimeout(scheduleNextAttempt, delay);
            }
        });
    }

    scheduleNextAttempt();
}

async function checkConnection() {
    try {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 5000);

        const res = await fetch('/api/status', {
            method: 'GET',
            signal: controller.signal
        });

        clearTimeout(timeoutId);
        return res.ok;
    } catch (e) {
        return false;
    }
}

// ============================================================================
// Sidebar
// ============================================================================

function toggleSidebar() {
    document.body.classList.toggle('sidebar-open');
}

function closeSidebar() {
    document.body.classList.remove('sidebar-open');
}

// ============================================================================
// Status Badge Updates
// ============================================================================

async function updateStatusBadges() {
    try {
        const d = await api('status');
        const wifiBadge = document.getElementById('wifi-badge');
        const lorawanBadge = document.getElementById('lorawan-badge');
        wifiBadge.className = 'badge ' + (d.wifi_status >= 3 ? 'on' : 'off');
        lorawanBadge.className = 'badge ' + (d.lorawan_joined ? 'on' : 'off');
    } catch (e) {}
}

// ============================================================================
// Lazy Loading System
// ============================================================================

function loadModule(name) {
    return new Promise((resolve, reject) => {
        // Módulo já carregado: resolve imediatamente sem re-fetch nem re-init
        if (modules[name].loaded) {
            resolve();
            return;
        }

        const container = document.getElementById('tab-' + name);

        fetch(`tabs/${name}.html`)
            .then(r => {
                if (!r.ok) throw new Error('HTML not found');
                return r.text();
            })
            .then(html => {
                container.innerHTML = html;

                const script = document.createElement('script');
                script.src = `tabs/${name}.js`;
                script.onload = () => {
                    // registerModule() já foi chamado pelo script, que define
                    // modules[name].loaded = true e modules[name].init
                    if (modules[name].init) modules[name].init();
                    resolve();
                };
                script.onerror = () => {
                    console.error(`Failed to load ${name}.js`);
                    toast(`Failed to load ${name} module`, 'error');
                    reject(new Error(`Failed to load ${name}.js`));
                };
                document.body.appendChild(script);
            })
            .catch(e => {
                console.error(`Failed to load module ${name}:`, e);
                container.innerHTML = `<div class="tab-error">Failed to load module</div>`;
                reject(e);
            });
    });
}

// Register module init function (called by each module)
function registerModule(name, initFn, options = {}) {
    modules[name].init = initFn;
    modules[name].loaded = true;
    if (options.pollFn)       modules[name].pollFn = options.pollFn;
    if (options.pollInterval) modules[name].pollInterval = options.pollInterval;
}

// ============================================================================
// Tab Navigation
// ============================================================================

let currentTab = 'sensors';

function switchTab(tabName) {
    pollManager.stop();

    document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
    document.querySelector(`.nav-btn[data-tab="${tabName}"]`).classList.add('active');

    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.getElementById('tab-' + tabName).classList.add('active');

    currentTab = tabName;
    closeSidebar();

    loadModule(tabName).then(() => {
        pollManager.start(tabName);
    }).catch(() => {
        // error already shown by loadModule
    });
}

// ============================================================================
// Log Viewer
// ============================================================================

const logState = {
    expanded: false,
    lastSequence: 0,
    pollInterval: null,
    entries: [],
    errorCount: 0,
    warnCount: 0
};

const LOG_LEVELS = ['', 'E', 'W', 'I', 'D', 'V'];
const LOG_LEVEL_NAMES = ['None', 'Error', 'Warn', 'Info', 'Debug', 'Verbose'];

function toggleLogViewer() {
    const viewer = document.getElementById('log-viewer');
    logState.expanded = !logState.expanded;

    if (logState.expanded) {
        viewer.classList.remove('collapsed');
        viewer.classList.add('expanded');
        document.body.classList.add('log-expanded');
        startLogPolling();
    } else {
        viewer.classList.remove('expanded');
        viewer.classList.add('collapsed');
        document.body.classList.remove('log-expanded');
        stopLogPolling();
    }
}

function startLogPolling() {
    if (logState.pollInterval) return;
    fetchLogs();
    logState.pollInterval = setInterval(fetchLogs, 1500);
}

function stopLogPolling() {
    if (logState.pollInterval) {
        clearInterval(logState.pollInterval);
        logState.pollInterval = null;
    }
}

async function fetchLogs() {
    try {
        const data = await api(`logs?since=${logState.lastSequence}`);

        if (data.logs && data.logs.length > 0) {
            // Add new entries
            for (const log of data.logs) {
                logState.entries.push(log);
                if (log.lvl === 1) logState.errorCount++;
                if (log.lvl === 2) logState.warnCount++;
            }

            // Keep only last 200 entries in memory
            if (logState.entries.length > 200) {
                const removed = logState.entries.splice(0, logState.entries.length - 200);
                // Adjust counts for removed entries
                for (const log of removed) {
                    if (log.lvl === 1) logState.errorCount--;
                    if (log.lvl === 2) logState.warnCount--;
                }
            }

            logState.lastSequence = data.sequence;
            renderLogs();
        }

        updateLogBadge();
    } catch (e) {
        // Silently ignore - connection handling is done elsewhere
    }
}

function renderLogs() {
    const container = document.getElementById('log-entries');
    const levelFilter = parseInt(document.getElementById('log-level-filter').value);
    const tagFilter = document.getElementById('log-tag-filter').value.toLowerCase();
    const autoScroll = document.getElementById('log-autoscroll').checked;

    // Filter entries
    const filtered = logState.entries.filter(log => {
        if (levelFilter > 0 && log.lvl > levelFilter) return false;
        if (tagFilter && !log.tag.toLowerCase().includes(tagFilter)) return false;
        return true;
    });

    if (filtered.length === 0) {
        container.innerHTML = '<div class="log-empty">No logs to display</div>';
        return;
    }

    // Build HTML
    let html = '';
    for (const log of filtered) {
        const ts = formatLogTimestamp(log.ts);
        const lvl = LOG_LEVELS[log.lvl] || '?';
        html += `<div class="log-entry level-${log.lvl}">
            <span class="log-ts">${ts}</span>
            <span class="log-lvl">${lvl}</span>
            <span class="log-tag">${escapeHtml(log.tag)}</span>
            <span class="log-msg">${escapeHtml(log.msg)}</span>
        </div>`;
    }

    container.innerHTML = html;

    // Auto-scroll to bottom
    if (autoScroll) {
        container.scrollTop = container.scrollHeight;
    }
}

function formatLogTimestamp(ms) {
    const s = Math.floor(ms / 1000);
    const m = Math.floor(s / 60);
    const sec = s % 60;
    const msec = ms % 1000;
    return `${m}:${sec.toString().padStart(2, '0')}.${msec.toString().padStart(3, '0')}`;
}

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

function updateLogBadge() {
    const badge = document.getElementById('log-count');
    const count = logState.entries.length;
    badge.textContent = count;

    badge.classList.remove('has-errors', 'has-warnings');
    if (logState.errorCount > 0) {
        badge.classList.add('has-errors');
    } else if (logState.warnCount > 0) {
        badge.classList.add('has-warnings');
    }
}

async function clearLogs() {
    try {
        await api('logs/clear', 'POST');
        logState.entries = [];
        logState.lastSequence = 0;
        logState.errorCount = 0;
        logState.warnCount = 0;
        renderLogs();
        updateLogBadge();
        toast('Logs cleared', 'success');
    } catch (e) {
        toast('Failed to clear logs', 'error');
    }
}

function updateLogFilter() {
    renderLogs();
}

// ============================================================================
// Initialization
// ============================================================================

document.addEventListener('DOMContentLoaded', () => {
    // Status badges
    updateStatusBadges();
    setInterval(updateStatusBadges, 10000);

    // Sidebar
    document.getElementById('menu-toggle').addEventListener('click', toggleSidebar);
    document.getElementById('sidebar-overlay').addEventListener('click', closeSidebar);

    // Nav buttons
    document.querySelectorAll('.nav-btn').forEach(btn => {
        btn.addEventListener('click', () => switchTab(btn.dataset.tab));
    });

    // Load initial tab
    loadModule('sensors').then(() => {
        pollManager.start('sensors');
    });

    // Log filters
    document.getElementById('log-level-filter').addEventListener('change', updateLogFilter);
    document.getElementById('log-tag-filter').addEventListener('input', updateLogFilter);
});
