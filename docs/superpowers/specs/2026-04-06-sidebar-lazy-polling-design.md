# Design: Sidebar Navigation + Lazy Polling

**Data:** 2026-04-06  
**Branch:** salt_spray  
**Escopo:** Refatoração da interface web — navegação e ciclo de vida do polling

---

## Contexto

A interface web atual usa uma barra de abas horizontal no topo (`<nav>`). Problemas identificados:

1. **Polling contínuo**: `sensors.js` e `lorawan.js` criam `setInterval` que nunca param, mesmo quando o usuário está em outra aba. `tasks.js` tem lógica de init diferente dos outros módulos e também não para o polling ao sair.
2. **Navegação subótima para mobile**: abas horizontais no topo com 7 itens ficam muito apertadas em telas pequenas.

O lazy loading de HTML/JS já funciona corretamente (carrega apenas na primeira visita).

---

## Decisões de Design

| Decisão | Escolha |
|---|---|
| Tipo de navegação | Sidebar com hamburger menu |
| Estilo dos itens | Ícone + label |
| Desktop (≥ 768px) | Sidebar sempre visível, sem hamburger |
| Mobile | Hamburger no header, sidebar desliza sobre o conteúdo com overlay |
| Comportamento ao sair da aba | Pausar o polling (clearInterval, sem re-fetch imediato ao voltar) |
| Suporte a visibilidade | Sim — Page Visibility API pausa polling quando tela bloqueia |

---

## Seção 1 — HTML (`index.html`)

Remover `<nav>` com botões horizontais. Adicionar `<aside id="sidebar">` e `<div id="sidebar-overlay">`.

```html
<header>
  <button id="menu-toggle">☰</button>
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
    <span class="nav-icon">📡</span>
    <span class="nav-label">Sensors</span>
  </button>
  <button class="nav-btn" data-tab="lorawan">
    <span class="nav-icon">🔗</span>
    <span class="nav-label">LoRaWAN</span>
  </button>
  <button class="nav-btn" data-tab="system">
    <span class="nav-icon">💻</span>
    <span class="nav-label">System</span>
  </button>
  <button class="nav-btn" data-tab="tasks">
    <span class="nav-icon">📋</span>
    <span class="nav-label">Tasks</span>
  </button>
  <button class="nav-btn" data-tab="config">
    <span class="nav-icon">⚙️</span>
    <span class="nav-label">Config</span>
  </button>
  <button class="nav-btn" data-tab="files">
    <span class="nav-icon">📁</span>
    <span class="nav-label">Files</span>
  </button>
  <button class="nav-btn" data-tab="ota">
    <span class="nav-icon">🔄</span>
    <span class="nav-label">OTA</span>
  </button>
</aside>

<main>
  <!-- seções de tab sem mudança -->
</main>
```

`body.sidebar-open` é a classe que o JS adiciona para controlar estado aberto/fechado.

---

## Seção 2 — CSS (`style.css`)

Remover estilos de `nav` e `.nav-btn` horizontais. Adicionar estilos da sidebar.

### Mobile (padrão)

```css
#sidebar {
    position: fixed;
    top: 0; left: -260px; bottom: 0;
    width: 260px;
    background: var(--surface);
    border-right: 1px solid var(--border);
    transition: left 0.25s ease;
    z-index: 90;
    display: flex;
    flex-direction: column;
    padding-top: 56px; /* altura do header */
}

body.sidebar-open #sidebar { left: 0; }

#sidebar-overlay {
    display: none;
    position: fixed; inset: 0;
    background: rgba(0, 0, 0, 0.5);
    z-index: 89;
}

body.sidebar-open #sidebar-overlay { display: block; }

#menu-toggle {
    background: none;
    border: none;
    color: var(--text);
    font-size: 20px;
    cursor: pointer;
    padding: 4px 8px;
    border-radius: 4px;
}

#menu-toggle:hover { background: var(--border); }

.nav-btn {
    display: flex;
    align-items: center;
    gap: 12px;
    width: 100%;
    padding: 12px 16px;
    border: none;
    background: none;
    color: var(--text-muted);
    font-size: 14px;
    cursor: pointer;
    text-align: left;
    border-left: 3px solid transparent;
    transition: all 0.15s;
}

.nav-btn:hover { color: var(--text); background: rgba(255,255,255,0.04); }

.nav-btn.active {
    color: var(--accent);
    border-left-color: var(--accent);
    background: rgba(88, 166, 255, 0.08);
}

.nav-icon { font-size: 16px; flex-shrink: 0; }
.nav-label { font-weight: 500; }
```

### Desktop (≥ 768px)

```css
@media (min-width: 768px) {
    #menu-toggle { display: none; }

    #sidebar {
        left: 0;
        padding-top: 57px; /* alinha abaixo do header fixo */
    }

    #sidebar-overlay { display: none !important; }

    main {
        margin-left: 260px;
    }
}
```

### Ajuste do `main` e `body`

`main` mantém `padding: 16px` e `max-width: 600px`. O `margin-left: 260px` no desktop faz o conteúdo não ficar sob a sidebar.

O log viewer (`position: fixed; bottom: 0`) não precisa de mudança.

---

## Seção 3 — JS (`core.js`)

### Poll Manager

Novo objeto centralizado. Módulos não gerenciam mais `setInterval` diretamente.

```js
const pollManager = {
    activeTab: null,
    timer: null,

    start(tabName) {
        const mod = modules[tabName];
        if (!mod?.pollFn) return;
        this.stop();
        this.activeTab = tabName;
        this.timer = setInterval(mod.pollFn, mod.pollInterval);
    },

    stop() {
        clearInterval(this.timer);
        this.timer = null;
    }
};

document.addEventListener('visibilitychange', () => {
    if (document.hidden) pollManager.stop();
    else if (pollManager.activeTab) pollManager.start(pollManager.activeTab);
});
```

**Comportamento de pausa:** ao sair de uma aba o timer é limpo. Ao voltar, o `start()` recria o intervalo do zero — o primeiro disparo acontece após o intervalo completo (sem re-fetch imediato).

### `registerModule()` atualizado

```js
function registerModule(name, initFn, options = {}) {
    modules[name].init = initFn;
    modules[name].loaded = true;
    modules[name].pollFn = options.pollFn || null;
    modules[name].pollInterval = options.pollInterval || 5000;
}
```

### `switchTab()` atualizado

```js
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
    });
}
```

`loadModule()` retorna uma Promise. Se o módulo já está carregado, resolve imediatamente.

### Sidebar toggle

```js
function toggleSidebar() { document.body.classList.toggle('sidebar-open'); }
function closeSidebar()  { document.body.classList.remove('sidebar-open'); }

// Setup no DOMContentLoaded
document.getElementById('menu-toggle').addEventListener('click', toggleSidebar);
document.getElementById('sidebar-overlay').addEventListener('click', closeSidebar);
```

### `loadModule()` — retornar Promise e não re-inicializar

Hoje `loadModule` chama `modules[name].init()` em toda visita (inclusive as repetidas). Isso causaria um re-fetch imediato ao voltar para uma aba, contradizendo o comportamento de "pausar sem re-fetch".

A correção: na segunda visita, apenas retornar — o `pollManager.start()` em `switchTab` cuida do intervalo.

```js
function loadModule(name) {
    return new Promise((resolve, reject) => {
        if (modules[name].loaded) {
            resolve(); // já carregado — sem re-fetch, sem re-init
            return;
        }

        const container = document.getElementById('tab-' + name);

        fetch(`tabs/${name}.html`)
            .then(r => { if (!r.ok) throw new Error('HTML not found'); return r.text(); })
            .then(html => {
                container.innerHTML = html;
                const script = document.createElement('script');
                script.src = `tabs/${name}.js`;
                script.onload = () => {
                    modules[name].loaded = true;
                    if (modules[name].init) modules[name].init(); // fetch inicial, única vez
                    resolve();
                };
                script.onerror = () => {
                    toast(`Failed to load ${name} module`, 'error');
                    reject();
                };
                document.body.appendChild(script);
            })
            .catch(e => {
                container.innerHTML = `<div class="tab-error">Failed to load module</div>`;
                reject(e);
            });
    });
}
```

---

## Seção 4 — Módulos (tab JS files)

### `sensors.js`

Remove `sensorPollTimer`. Passa `pollFn`:

```js
async function refreshSensors() { /* sem mudança */ }

function initSensors() {
    refreshSensors(); // fetch inicial
}

registerModule('sensors', initSensors, { pollFn: refreshSensors, pollInterval: 5000 });
```

### `lorawan.js`

Remove `lorawanPollTimer`:

```js
function initLoRaWAN() {
    refreshLoRaWAN();
    loadLoRaWANConfig();
}

registerModule('lorawan', initLoRaWAN, { pollFn: refreshLoRaWAN, pollInterval: 5000 });
```

### `tasks.js`

Maior mudança. Hoje não usa `registerModule` e tem checkbox "auto-refresh". O checkbox é removido (poll manager gerencia automaticamente). Migra para o padrão:

```js
async function refreshTasks() { /* sem mudança */ }

function initTasksTab() {
    refreshTasks(); // fetch inicial
    // remove: lógica do auto-refresh checkbox
}

registerModule('tasks', initTasksTab, { pollFn: refreshTasks, pollInterval: 2000 });
```

O elemento `#auto-refresh` em `tasks.html` também é removido.

### `system.js`, `config.js`, `files.js`, `ota.js`

Sem polling — `registerModule` sem `pollFn`. Nenhuma mudança funcional:

```js
registerModule('system', initSystem); // sem mudança na assinatura
```

---

## Resumo de arquivos modificados

| Arquivo | Tipo de mudança |
|---|---|
| `main/www/index.html` | Remove `<nav>`, adiciona `<aside>` + overlay + `#menu-toggle` |
| `main/www/style.css` | Remove estilos horizontais, adiciona sidebar + overlay + breakpoint desktop |
| `main/www/core.js` | Poll manager, sidebar toggle, `switchTab` e `registerModule` atualizados, `loadModule` retorna Promise |
| `main/www/tabs/sensors.js` | Remove `sensorPollTimer`, usa `pollFn` |
| `main/www/tabs/lorawan.js` | Remove `lorawanPollTimer`, usa `pollFn` |
| `main/www/tabs/tasks.js` | Migra para `registerModule`, remove auto-refresh checkbox |
| `main/www/tabs/tasks.html` | Remove elemento `#auto-refresh` |
| `main/www/tabs/system.js` | Sem mudança funcional |
| `main/www/tabs/config.js` | Sem mudança funcional |
| `main/www/tabs/files.js` | Sem mudança funcional |
| `main/www/tabs/ota.js` | Sem mudança funcional |
