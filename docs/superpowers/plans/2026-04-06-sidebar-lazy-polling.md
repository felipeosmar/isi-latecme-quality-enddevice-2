# Sidebar Navigation + Lazy Polling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Substituir as abas horizontais por uma sidebar com hamburger (mobile) / persistente (desktop), e centralizar o gerenciamento de polling para que apenas a aba ativa faça requisições.

**Architecture:** `core.js` passa a gerenciar um `pollManager` central que inicia/pausa timers ao trocar de aba e ao page visibility change. Cada módulo de aba registra sua `pollFn` e `pollInterval` via `registerModule()`. `loadModule()` se torna uma Promise e nunca re-inicializa módulos já carregados.

**Tech Stack:** HTML/CSS/JS vanilla, ESP-IDF LittleFS (www partition), `./flash.sh www` para deploy.

---

## Mapa de arquivos

| Arquivo | Mudança |
|---|---|
| `main/www/index.html` | Remove `<nav>`, adiciona `<aside id="sidebar">`, `<div id="sidebar-overlay">`, `<button id="menu-toggle">` no header |
| `main/www/style.css` | Remove estilos `nav` / `.nav-btn` horizontais; adiciona sidebar, overlay, `#menu-toggle`, breakpoint desktop |
| `main/www/core.js` | Module registry com campos `pollFn`/`pollInterval`; `loadModule` → Promise; `registerModule` novo assinatura; `pollManager`; `switchTab` atualizado; sidebar toggle; DOMContentLoaded atualizado |
| `main/www/tabs/sensors.js` | Remove `sensorPollTimer`; usa `pollFn` no `registerModule` |
| `main/www/tabs/lorawan.js` | Remove `lorawanPollTimer`; usa `pollFn` no `registerModule` |
| `main/www/tabs/tasks.js` | Migra de init-próprio para `registerModule`; remove lógica do checkbox auto-refresh |
| `main/www/tabs/tasks.html` | Remove toolbar com checkbox auto-refresh |

---

## Task 1: Atualizar index.html

**Files:**
- Modify: `main/www/index.html`

- [ ] **Step 1: Substituir o conteúdo completo do arquivo**

```html
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LoRaWAN Sensor</title>
    <link rel="icon" type="image/x-icon" href="favicon.ico">
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <header>
        <button id="menu-toggle">&#9776;</button>
        <h1>LoRaWAN Sensor</h1>
        <div class="status">
            <span id="wifi-badge" class="badge">WiFi</span>
            <span id="lorawan-badge" class="badge">LoRaWAN</span>
        </div>
    </header>

    <div id="connection-banner" class="hidden"></div>
    <div id="sidebar-overlay"></div>

    <aside id="sidebar">
        <button class="nav-btn active" data-tab="sensors">
            <span class="nav-icon">&#128225;</span>
            <span class="nav-label">Sensors</span>
        </button>
        <button class="nav-btn" data-tab="lorawan">
            <span class="nav-icon">&#128279;</span>
            <span class="nav-label">LoRaWAN</span>
        </button>
        <button class="nav-btn" data-tab="system">
            <span class="nav-icon">&#128187;</span>
            <span class="nav-label">System</span>
        </button>
        <button class="nav-btn" data-tab="tasks">
            <span class="nav-icon">&#128203;</span>
            <span class="nav-label">Tasks</span>
        </button>
        <button class="nav-btn" data-tab="config">
            <span class="nav-icon">&#9881;</span>
            <span class="nav-label">Config</span>
        </button>
        <button class="nav-btn" data-tab="files">
            <span class="nav-icon">&#128193;</span>
            <span class="nav-label">Files</span>
        </button>
        <button class="nav-btn" data-tab="ota">
            <span class="nav-icon">&#8635;</span>
            <span class="nav-label">OTA</span>
        </button>
    </aside>

    <main>
        <section id="tab-sensors" class="tab active">
            <div class="tab-loading">Loading...</div>
        </section>
        <section id="tab-lorawan" class="tab">
            <div class="tab-loading">Loading...</div>
        </section>
        <section id="tab-system" class="tab">
            <div class="tab-loading">Loading...</div>
        </section>
        <section id="tab-tasks" class="tab">
            <div class="tab-loading">Loading...</div>
        </section>
        <section id="tab-config" class="tab">
            <div class="tab-loading">Loading...</div>
        </section>
        <section id="tab-files" class="tab">
            <div class="tab-loading">Loading...</div>
        </section>
        <section id="tab-ota" class="tab">
            <div class="tab-loading">Loading...</div>
        </section>
    </main>

    <div id="toast"></div>

    <footer id="log-viewer" class="log-viewer collapsed">
        <div class="log-header" onclick="toggleLogViewer()">
            <span class="log-title">Logs</span>
            <span class="log-badge" id="log-count">0</span>
            <span class="log-toggle" id="log-toggle-icon">&#9650;</span>
        </div>
        <div class="log-content">
            <div class="log-toolbar">
                <select id="log-level-filter">
                    <option value="0">All Levels</option>
                    <option value="1">Error+</option>
                    <option value="2">Warn+</option>
                    <option value="3">Info+</option>
                    <option value="4">Debug+</option>
                </select>
                <input type="text" id="log-tag-filter" placeholder="Filter by tag...">
                <button class="btn btn-small" onclick="clearLogs()">Clear</button>
                <label class="log-autoscroll">
                    <input type="checkbox" id="log-autoscroll" checked> Auto-scroll
                </label>
            </div>
            <div class="log-entries" id="log-entries"></div>
        </div>
    </footer>

    <script src="core.js"></script>
</body>
</html>
```

> Nota: ícones usam HTML entities (`&#128225;` = 📡, `&#128279;` = 🔗, etc.) para compatibilidade máxima com o servidor HTTP do ESP32 sem encoding issues.

- [ ] **Step 2: Commit**

```bash
git add main/www/index.html
git commit -m "feat(www): replace top nav with sidebar HTML structure"
```

---

## Task 2: Atualizar style.css

**Files:**
- Modify: `main/www/style.css`

- [ ] **Step 1: Substituir o bloco `nav` / `.nav-btn` horizontal**

Localizar e remover este bloco (linhas 52–74 do arquivo atual):

```css
nav {
    display: flex;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
}

.nav-btn {
    flex: 1;
    padding: 12px;
    border: none;
    background: none;
    color: var(--text-muted);
    font-size: 13px;
    cursor: pointer;
    border-bottom: 2px solid transparent;
    transition: all 0.2s;
}

.nav-btn:hover { color: var(--text); }
.nav-btn.active {
    color: var(--accent);
    border-bottom-color: var(--accent);
}
```

Substituir por:

```css
/* ============================================================================
   Sidebar Navigation
   ============================================================================ */

#menu-toggle {
    background: none;
    border: none;
    color: var(--text);
    font-size: 22px;
    cursor: pointer;
    padding: 2px 8px;
    border-radius: 4px;
    line-height: 1;
    flex-shrink: 0;
}

#menu-toggle:hover { background: var(--border); }

#sidebar-overlay {
    display: none;
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.5);
    z-index: 89;
}

body.sidebar-open #sidebar-overlay { display: block; }

#sidebar {
    position: fixed;
    top: 0;
    left: -260px;
    bottom: 0;
    width: 260px;
    background: var(--surface);
    border-right: 1px solid var(--border);
    z-index: 90;
    display: flex;
    flex-direction: column;
    padding: 8px;
    overflow-y: auto;
    transition: left 0.25s ease;
}

body.sidebar-open #sidebar { left: 0; }

.nav-btn {
    display: flex;
    align-items: center;
    gap: 12px;
    width: 100%;
    padding: 11px 14px;
    border: none;
    background: none;
    color: var(--text-muted);
    font-size: 14px;
    cursor: pointer;
    text-align: left;
    border-left: 3px solid transparent;
    border-radius: 6px;
    transition: all 0.15s;
    margin-bottom: 2px;
}

.nav-btn:hover {
    color: var(--text);
    background: rgba(255, 255, 255, 0.04);
}

.nav-btn.active {
    color: var(--accent);
    border-left-color: var(--accent);
    background: rgba(88, 166, 255, 0.08);
}

.nav-icon { font-size: 16px; flex-shrink: 0; }
.nav-label { font-weight: 500; }

/* Desktop: sidebar sempre visível */
@media (min-width: 768px) {
    #menu-toggle { display: none; }

    #sidebar {
        top: 57px; /* altura do header */
        left: 0;
        z-index: 85;
    }

    #sidebar-overlay { display: none !important; }

    main {
        margin-left: 260px;
    }
}
```

- [ ] **Step 2: Adicionar `position: sticky` ao header para que ele não suma ao rolar no desktop**

Localizar o bloco `header` existente e adicionar `position: sticky; top: 0; z-index: 95;`:

```css
header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 12px 16px;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
    position: sticky;
    top: 0;
    z-index: 95;
}
```

- [ ] **Step 3: Commit**

```bash
git add main/www/style.css
git commit -m "feat(www): add sidebar CSS with responsive desktop/mobile behavior"
```

---

## Task 3: Atualizar core.js — Module Registry e loadModule

**Files:**
- Modify: `main/www/core.js`

- [ ] **Step 1: Atualizar o Module Registry para incluir campos `pollFn` e `pollInterval`**

Substituir:
```js
const modules = {
    sensors: { loaded: false, init: null },
    lorawan: { loaded: false, init: null },
    system: { loaded: false, init: null },
    tasks: { loaded: false, init: null },
    config: { loaded: false, init: null },
    files: { loaded: false, init: null },
    ota: { loaded: false, init: null }
};
```

Por:
```js
const modules = {
    sensors: { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    lorawan: { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    system:  { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    tasks:   { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    config:  { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    files:   { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
    ota:     { loaded: false, init: null, pollFn: null, pollInterval: 5000 },
};
```

- [ ] **Step 2: Substituir `loadModule` por versão que retorna Promise e não re-inicializa módulos já carregados**

Substituir a função `loadModule` existente inteira:

```js
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
```

- [ ] **Step 3: Substituir `registerModule` por versão que aceita `options`**

Substituir:
```js
function registerModule(name, initFn) {
    modules[name].init = initFn;
    modules[name].loaded = true;
}
```

Por:
```js
function registerModule(name, initFn, options = {}) {
    modules[name].init = initFn;
    modules[name].loaded = true;
    if (options.pollFn)       modules[name].pollFn = options.pollFn;
    if (options.pollInterval) modules[name].pollInterval = options.pollInterval;
}
```

- [ ] **Step 4: Commit**

```bash
git add main/www/core.js
git commit -m "refactor(www): loadModule returns Promise, registerModule accepts pollFn/pollInterval"
```

---

## Task 4: Adicionar Poll Manager ao core.js

**Files:**
- Modify: `main/www/core.js`

- [ ] **Step 1: Adicionar o objeto `pollManager` após o bloco do Module Registry**

Inserir após a declaração de `modules`:

```js
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
```

- [ ] **Step 2: Commit**

```bash
git add main/www/core.js
git commit -m "feat(www): add centralized poll manager with Page Visibility API support"
```

---

## Task 5: Atualizar switchTab, sidebar toggle e DOMContentLoaded no core.js

**Files:**
- Modify: `main/www/core.js`

- [ ] **Step 1: Adicionar funções de sidebar toggle**

Adicionar novo bloco após o bloco de Connection State Management (ou antes do Tab Navigation):

```js
// ============================================================================
// Sidebar
// ============================================================================

function toggleSidebar() {
    document.body.classList.toggle('sidebar-open');
}

function closeSidebar() {
    document.body.classList.remove('sidebar-open');
}
```

- [ ] **Step 2: Substituir `switchTab` e remover os event listeners de nav antigos**

Localizar e substituir o bloco inteiro de Tab Navigation:

```js
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
        // erro já exibido por loadModule
    });
}
```

> Remover as linhas antigas que estavam fora do DOMContentLoaded:
> ```js
> // REMOVER ESTAS LINHAS (ficavam fora do DOMContentLoaded):
> document.querySelectorAll('.nav-btn').forEach(btn => {
>     btn.addEventListener('click', () => switchTab(btn.dataset.tab));
> });
> ```

- [ ] **Step 3: Substituir o bloco DOMContentLoaded**

Localizar e substituir:
```js
document.addEventListener('DOMContentLoaded', () => {
    // Update status badges
    updateStatusBadges();
    setInterval(updateStatusBadges, 10000);

    // Load initial tab (sensors)
    loadModule('sensors');

    // Setup log filter listeners
    document.getElementById('log-level-filter').addEventListener('change', updateLogFilter);
    document.getElementById('log-tag-filter').addEventListener('input', updateLogFilter);
});
```

Por:
```js
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
```

- [ ] **Step 4: Commit**

```bash
git add main/www/core.js
git commit -m "feat(www): update switchTab to use poll manager, add sidebar toggle"
```

- [ ] **Step 5: Flash www e verificar navegação no browser**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh
./flash.sh www
```

Abrir `http://<ip-do-dispositivo>/` e verificar:
- No mobile: botão ☰ no header aparece, clique abre sidebar com overlay escuro, clicar fora fecha
- No desktop (≥768px): sidebar sempre visível à esquerda, botão ☰ some
- Trocar de aba funciona, aba ativa fica destacada na sidebar
- Aba "Sensors" carrega ao abrir (lazy load na primeira visita)

---

## Task 6: Atualizar sensors.js

**Files:**
- Modify: `main/www/tabs/sensors.js`

- [ ] **Step 1: Substituir o conteúdo do arquivo**

```js
// Sensors Tab Module

async function refreshSensors() {
    try {
        const d = await api('sensors/status');
        document.getElementById('sensor-name').textContent = d.sensor_name || 'Not detected';
        document.getElementById('sensor-temp').textContent = d.temp_hum_valid ? d.temperature.toFixed(1) + ' \u00b0C' : 'N/A';
        document.getElementById('sensor-hum').textContent = d.temp_hum_valid ? d.humidity.toFixed(1) + ' %' : 'N/A';
        document.getElementById('sensor-last-read').textContent = d.timestamp_ms ? formatUptime(d.timestamp_ms) : '--';

        if (d.thermocouple_valid !== undefined) {
            document.getElementById('tc-temp').textContent = d.thermocouple_valid ? d.thermocouple_temp.toFixed(1) + ' \u00b0C' : 'N/A';
            document.getElementById('tc-status').textContent = d.thermocouple_valid ? 'Connected' : 'Not connected';
        }
    } catch (e) {
        console.error('Failed to refresh sensors:', e);
    }

    try {
        const c = await api('sensors/config');
        document.getElementById('sensor-interval').textContent = c.interval + ' s';
        document.getElementById('sensor-temp-corr').textContent = c.temp_correction.toFixed(1) + ' \u00b0C';
        document.getElementById('sensor-hum-corr').textContent = c.hum_correction.toFixed(1) + ' %';
    } catch (e) {}
}

function initSensors() {
    refreshSensors();
}

registerModule('sensors', initSensors, { pollFn: refreshSensors, pollInterval: 5000 });
```

- [ ] **Step 2: Commit**

```bash
git add main/www/tabs/sensors.js
git commit -m "refactor(www): sensors.js delegates polling to poll manager"
```

---

## Task 7: Atualizar lorawan.js

**Files:**
- Modify: `main/www/tabs/lorawan.js`

- [ ] **Step 1: Substituir o conteúdo do arquivo**

```js
// LoRaWAN Tab Module

async function refreshLoRaWAN() {
    try {
        const d = await api('lorawan/status');
        document.getElementById('lora-join-status').textContent = d.joined ? 'Joined' : 'Not Joined';
        document.getElementById('lora-join-status').className = d.joined ? 'text-success' : 'text-warning';
        document.getElementById('lora-devaddr').textContent = d.dev_addr || '--';
        document.getElementById('lora-uplink-count').textContent = d.uplink_count || 0;
        document.getElementById('lora-last-uplink').textContent = d.last_uplink_time ? formatUptime(d.last_uplink_time) : '--';
        document.getElementById('lora-rssi').textContent = d.rssi ? d.rssi + ' dBm' : '-- dBm';
        document.getElementById('lora-snr').textContent = d.snr ? d.snr.toFixed(1) + ' dB' : '-- dB';
        document.getElementById('lora-data-rate').textContent = d.data_rate !== undefined ? 'DR' + d.data_rate : '--';
    } catch (e) {
        console.error('Failed to refresh LoRaWAN status:', e);
    }
}

async function loadLoRaWANConfig() {
    try {
        const d = await api('lorawan/config');
        document.getElementById('lora-dev-eui').value = d.dev_eui || '';
        document.getElementById('lora-join-eui').value = d.join_eui || '';
        document.getElementById('lora-app-key').value = d.app_key || '';
        document.getElementById('lora-port').value = d.port || 1;
        document.getElementById('lora-interval').value = d.uplink_interval || 60;
        document.getElementById('lora-sub-band').value = d.sub_band || 2;
        document.getElementById('lora-adr').checked = d.adr_enabled !== false;
    } catch (e) {
        console.error('Failed to load LoRaWAN config:', e);
    }
}

async function saveLoRaWANConfig() {
    const config = {
        dev_eui: document.getElementById('lora-dev-eui').value.trim(),
        join_eui: document.getElementById('lora-join-eui').value.trim(),
        app_key: document.getElementById('lora-app-key').value.trim(),
        port: parseInt(document.getElementById('lora-port').value),
        uplink_interval: parseInt(document.getElementById('lora-interval').value),
        sub_band: parseInt(document.getElementById('lora-sub-band').value),
        adr_enabled: document.getElementById('lora-adr').checked
    };

    try {
        const r = await api('lorawan/config', 'POST', config);
        if (r.success) {
            toast('LoRaWAN config saved. Restarting...', 'success');
            setTimeout(() => location.reload(), 3000);
        } else {
            toast(r.message || 'Failed to save config', 'error');
        }
    } catch (e) {
        toast('Failed to save LoRaWAN config', 'error');
    }
}

async function forceJoin() {
    try {
        const r = await api('lorawan/join', 'POST');
        toast(r.success ? 'Join request sent' : (r.message || 'Failed'), r.success ? 'success' : 'error');
    } catch (e) {
        toast('Failed to send join request', 'error');
    }
}

function initLoRaWAN() {
    refreshLoRaWAN();
    loadLoRaWANConfig();
}

registerModule('lorawan', initLoRaWAN, { pollFn: refreshLoRaWAN, pollInterval: 5000 });
```

- [ ] **Step 2: Commit**

```bash
git add main/www/tabs/lorawan.js
git commit -m "refactor(www): lorawan.js delegates polling to poll manager"
```

---

## Task 8: Migrar tasks.js e tasks.html

**Files:**
- Modify: `main/www/tabs/tasks.js`
- Modify: `main/www/tabs/tasks.html`

- [ ] **Step 1: Substituir tasks.js**

O arquivo atual usa init próprio (sem `registerModule`) e tem lógica do checkbox auto-refresh. Substituir pelo padrão unificado:

```js
// Tasks Monitor Module

async function refreshTasks() {
    try {
        const response = await fetch('/api/tasks');
        const data = await response.json();

        document.getElementById('heap-free').textContent = formatBytes(data.heap_free);
        document.getElementById('heap-min').textContent = formatBytes(data.heap_min);
        document.getElementById('uptime').textContent = formatUptime(data.uptime_s);
        document.getElementById('task-count').textContent = data.task_count;

        const tasks = data.tasks.sort((a, b) => b.cpu_percent - a.cpu_percent);

        const tbody = document.getElementById('tasks-tbody');
        tbody.innerHTML = tasks.map(task => `
            <tr>
                <td><strong>${task.name}</strong></td>
                <td><span class="state-${task.state}">${task.state}</span></td>
                <td>${task.priority}</td>
                <td>${task.cpu_percent}%</td>
                <td>${formatBytes(task.stack_hwm * 4)}</td>
            </tr>
        `).join('');

        const idleTasks = tasks.filter(t => t.name === 'IDLE0' || t.name === 'IDLE1');
        const otherTasks = tasks.filter(t => t.name !== 'IDLE0' && t.name !== 'IDLE1');

        const coreUsageHtml = idleTasks.map(task => {
            const coreNum = task.name === 'IDLE0' ? '0' : '1';
            const usagePercent = 100 - task.cpu_percent;
            return `
            <div class="cpu-bar">
                <span class="cpu-bar-label">Core ${coreNum}</span>
                <div class="cpu-bar-track">
                    <div class="cpu-bar-fill" style="width: ${Math.min(usagePercent, 100)}%"></div>
                </div>
                <span class="cpu-bar-value">${usagePercent}%</span>
            </div>`;
        }).join('');

        const otherTasksHtml = otherTasks.slice(0, 4).map(task => `
            <div class="cpu-bar">
                <span class="cpu-bar-label">${task.name}</span>
                <div class="cpu-bar-track">
                    <div class="cpu-bar-fill" style="width: ${Math.min(task.cpu_percent, 100)}%"></div>
                </div>
                <span class="cpu-bar-value">${task.cpu_percent}%</span>
            </div>
        `).join('');

        document.getElementById('cpu-bars').innerHTML = coreUsageHtml + otherTasksHtml;

    } catch (error) {
        console.error('Failed to fetch tasks:', error);
    }
}

function initTasksTab() {
    refreshTasks();
}

registerModule('tasks', initTasksTab, { pollFn: refreshTasks, pollInterval: 2000 });
```

> `formatBytes` e `formatUptime` eram duplicados em tasks.js — o arquivo os redefinía localmente. As versões em `core.js` são globais e idênticas; as definições locais são removidas.

- [ ] **Step 2: Atualizar tasks.html — remover toolbar com checkbox auto-refresh**

Substituir o conteúdo completo de `tasks.html`:

```html
<div class="toolbar">
    <button onclick="refreshTasks()" class="btn-icon" title="Refresh">
        <svg viewBox="0 0 24 24">
            <path d="M17.65 6.35A7.958 7.958 0 0012 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0112 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z" />
        </svg>
    </button>
</div>

<div class="card">
    <h3>System Overview</h3>
    <div class="info-row"><span>Heap Free</span><span id="heap-free">--</span></div>
    <div class="info-row"><span>Heap Min</span><span id="heap-min">--</span></div>
    <div class="info-row"><span>Uptime</span><span id="uptime">--</span></div>
    <div class="info-row"><span>Tasks</span><span id="task-count">--</span></div>
</div>

<div class="card">
    <h3>Task List</h3>
    <div class="tasks-table-container">
        <table class="tasks-table">
            <thead>
                <tr>
                    <th>Task</th>
                    <th>State</th>
                    <th>Prio</th>
                    <th>CPU</th>
                    <th>Stack</th>
                </tr>
            </thead>
            <tbody id="tasks-tbody">
                <tr>
                    <td colspan="5">Loading...</td>
                </tr>
            </tbody>
        </table>
    </div>
</div>

<div class="card">
    <h3>CPU Usage</h3>
    <div id="cpu-bars"></div>
</div>
```

- [ ] **Step 3: Commit**

```bash
git add main/www/tabs/tasks.js main/www/tabs/tasks.html
git commit -m "refactor(www): migrate tasks to registerModule, remove auto-refresh checkbox"
```

---

## Task 9: Flash final e verificação completa

**Files:** nenhum arquivo novo

- [ ] **Step 1: Build e flash**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh
./flash.sh www
```

- [ ] **Step 2: Verificar no mobile (ou DevTools com viewport estreito)**

1. Abrir `http://<ip-do-dispositivo>/`
2. Botão ☰ visível no header
3. Clicar ☰ → sidebar desliza, overlay escuro cobre o conteúdo
4. Clicar em "LoRaWAN" → aba muda, sidebar fecha automaticamente
5. Clicar fora da sidebar (no overlay) → fecha sem trocar de aba
6. Navegar para "Sensors" → dados aparecem, polling inicia
7. Trocar para "System" → dados de system aparecem; abrir DevTools > Network e confirmar que `/api/sensors/status` **não** aparece mais enquanto "System" está ativo
8. Voltar para "Sensors" → polling retoma após 5 segundos (sem re-fetch imediato)

- [ ] **Step 3: Verificar no desktop (viewport ≥ 768px)**

1. Sidebar visível à esquerda sem interação
2. Botão ☰ ausente
3. Header fica fixo ao rolar
4. Trocar abas não fecha nada (sidebar não tem overlay no desktop)
5. DevTools > Network: ao ficar 1 minuto na aba "Tasks", confirmar que `/api/sensors/status` não aparece na lista de requests

- [ ] **Step 4: Verificar Page Visibility API**

1. Com "Sensors" ativo e DevTools > Network aberto
2. Bloquear a tela ou trocar para outra aba do browser
3. Voltar → confirmar que o polling pausou enquanto a página estava escondida (nenhuma request durante o período)
