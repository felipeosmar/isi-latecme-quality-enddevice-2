# OTA Auto-Update Log Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Exibir os logs do auto-updater em tempo real na interface web quando o usuário clica em "Check Now" na aba OTA.

**Architecture:** Adicionar um buffer de log estático (`s_last_run_log[1024]`) e uma flag `s_is_checking` ao `auto_updater.c`. Uma função helper `upd_log()` substitui os `ESP_LOGI` chave dentro de `run_check()` e funções auxiliares, appendando ao buffer enquanto mantém o log serial. O `GET /api/ota/auto-update` passa a retornar `is_checking` e `last_run_log`. O frontend mostra uma caixa de log e faz polling a cada 2s após o clique em "Check Now".

**Tech Stack:** ESP-IDF v5.5.3, C (FreeRTOS), cJSON, HTML/JS (vanilla)

> **Nota:** Este projeto não tem infraestrutura de testes. Verificação é feita por build bem-sucedido (`idf.py build`) e teste manual no dispositivo.

---

### Task 1: Adicionar buffer de log e helper `upd_log()` ao auto_updater.c

**Files:**
- Modify: `main/update/auto_updater.c`

- [ ] **Step 1: Adicionar includes e variáveis de estado**

Em `main/update/auto_updater.c`, após a linha `#include <time.h>`, adicionar:

```c
#include <stdarg.h>
```

Após o bloco `// State` (após `static bool s_initialized = false;`), adicionar:

```c
#define LAST_RUN_LOG_SIZE 1024
static char s_last_run_log[LAST_RUN_LOG_SIZE] = {0};
static bool s_is_checking = false;
```

- [ ] **Step 2: Adicionar a função helper `upd_log()`**

Após o bloco `// State`, antes do bloco `// Helpers`, adicionar:

```c
// ============================================================================
// Log helper — appends to in-memory log buffer AND writes to serial
// ============================================================================

static void upd_log(const char *fmt, ...)
{
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    ESP_LOGI(TAG, "%s", line);

    size_t used = strlen(s_last_run_log);
    size_t remaining = LAST_RUN_LOG_SIZE - used;
    if (remaining < 2) return;  // buffer full, drop line

    strncat(s_last_run_log, line, remaining - 1);
    used = strlen(s_last_run_log);
    if (used < LAST_RUN_LOG_SIZE - 1) {
        s_last_run_log[used]     = '\n';
        s_last_run_log[used + 1] = '\0';
    }
}
```

- [ ] **Step 3: Verificar build**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Esperado: `Project build complete.`

- [ ] **Step 4: Commit**

```bash
git add main/update/auto_updater.c
git commit -m "feat(auto-updater): add log buffer and upd_log() helper"
```

---

### Task 2: Instrumentar run_check() e funções auxiliares com upd_log()

**Files:**
- Modify: `main/update/auto_updater.c`

- [ ] **Step 1: Instrumentar `find_latest_tag()` — "Latest tag" log**

Na função `find_latest_tag()`, localizar a linha:
```c
    ESP_LOGI(TAG, "Latest tag for branch '%s': %s (N=%d)", branch, out_tag, n);
```
Substituir por:
```c
    upd_log("Latest tag for branch '%s': %s (N=%d)", branch, out_tag, n);
```

- [ ] **Step 2: Instrumentar `flash_firmware_from_url()` — tamanho e flash completo**

Na função `flash_firmware_from_url()`, localizar:
```c
    ESP_LOGI(TAG, "Flashing firmware from: %s", url);
```
Substituir por:
```c
    upd_log("Flashing firmware from: %s", url);
```

Localizar:
```c
    ESP_LOGI(TAG, "Firmware size: %d bytes", content_len);
```
Substituir por:
```c
    upd_log("Firmware size: %d bytes", content_len);
```

Localizar:
```c
    ESP_LOGI(TAG, "Firmware flash complete (%lu bytes) -> %s", written, update_part->label);
```
Substituir por:
```c
    upd_log("Firmware flash complete (%lu bytes) -> %s", written, update_part->label);
```

- [ ] **Step 3: Instrumentar `run_check()` — limpar buffer, setar flag, mensagens chave**

No início de `run_check()`, antes de qualquer lógica, adicionar:
```c
    s_is_checking = true;
    s_last_run_log[0] = '\0';
```

Localizar:
```c
        ESP_LOGI(TAG, "First check: storing current latest tag '%s' without updating", latest_tag);
```
Substituir por:
```c
        upd_log("First check: storing current latest tag '%s' without updating", latest_tag);
```

Localizar:
```c
        ESP_LOGI(TAG, "New firmware available: %s -> %s", fw_tag, latest_tag);
```
Substituir por:
```c
        upd_log("New firmware available: %s -> %s", fw_tag, latest_tag);
```

Localizar (dentro do bloco de erro do flash de firmware):
```c
            ESP_LOGE(TAG, "Firmware flash failed");
            s_last_result = AUTO_UPDATE_RESULT_ERROR;
            return;
```
Substituir por:
```c
            upd_log("Firmware flash failed");
            s_last_result = AUTO_UPDATE_RESULT_ERROR;
            s_is_checking = false;
            return;
```

Localizar:
```c
        ESP_LOGI(TAG, "Firmware updated to %s, rebooting...", latest_tag);
```
Substituir por:
```c
        upd_log("Firmware updated to %s, rebooting...", latest_tag);
```

Localizar:
```c
        ESP_LOGI(TAG, "www partition outdated: %s -> %s", www_tag, latest_tag);
```
Substituir por:
```c
        upd_log("www partition outdated: %s -> %s", www_tag, latest_tag);
```

Localizar (dentro do bloco de erro do flash www):
```c
            ESP_LOGE(TAG, "www flash failed");
            // Revert tag on failure
            config_set_auto_update_www_tag(www_tag);
            config_save();
            s_last_result = AUTO_UPDATE_RESULT_ERROR;
            return;
```
Substituir por:
```c
            upd_log("www flash failed");
            // Revert tag on failure
            config_set_auto_update_www_tag(www_tag);
            config_save();
            s_last_result = AUTO_UPDATE_RESULT_ERROR;
            s_is_checking = false;
            return;
```

Localizar:
```c
        ESP_LOGI(TAG, "www updated to %s, rebooting...", latest_tag);
```
Substituir por:
```c
        upd_log("www updated to %s, rebooting...", latest_tag);
```

Localizar (linha final de run_check):
```c
    ESP_LOGI(TAG, "Already up to date (%s)", latest_tag);
    s_last_result = AUTO_UPDATE_RESULT_UP_TO_DATE;
```
Substituir por:
```c
    upd_log("Already up to date (%s)", latest_tag);
    s_last_result = AUTO_UPDATE_RESULT_UP_TO_DATE;
    s_is_checking = false;
```

- [ ] **Step 4: Adicionar `s_is_checking = false` nos early returns de run_check()**

Localizar (skip por WiFi desconectado):
```c
        ESP_LOGI(TAG, "WiFi not connected in STA mode, skipping check");
        return;
```
Substituir por:
```c
        upd_log("WiFi not connected, skipping check");
        s_is_checking = false;
        return;
```

Localizar (skip por auto-update desabilitado):
```c
        ESP_LOGI(TAG, "Auto-update disabled, skipping check");
        return;
```
Substituir por:
```c
        upd_log("Auto-update disabled, skipping check");
        s_is_checking = false;
        return;
```

Localizar (branch vazio):
```c
        ESP_LOGW(TAG, "No branch configured");
        s_last_result = AUTO_UPDATE_RESULT_ERROR;
        return;
```
Substituir por:
```c
        upd_log("No branch configured");
        s_last_result = AUTO_UPDATE_RESULT_ERROR;
        s_is_checking = false;
        return;
```

Localizar (falha ao buscar releases):
```c
        ESP_LOGE(TAG, "Failed to fetch GitHub releases");
        s_last_result = AUTO_UPDATE_RESULT_ERROR;
        return;
```
Substituir por:
```c
        upd_log("Failed to fetch GitHub releases");
        s_last_result = AUTO_UPDATE_RESULT_ERROR;
        s_is_checking = false;
        return;
```

Localizar (falha ao parsear tag):
```c
        free(json);
        s_last_result = AUTO_UPDATE_RESULT_ERROR;
        return;
```
Substituir por:
```c
        free(json);
        upd_log("Failed to find release tag for branch '%s'", branch);
        s_last_result = AUTO_UPDATE_RESULT_ERROR;
        s_is_checking = false;
        return;
```

Localizar o bloco do first check (dentro do `if (fw_tag[0] == '\0')`, que já foi modificado no Step 3 acima com `upd_log("First check: storing...")`). O trecho completo após a modificação do Step 3 ficará assim — encontrar o `return` dentro desse bloco específico:
```c
        upd_log("First check: storing current latest tag '%s' without updating", latest_tag);
        config_set_auto_update_firmware_tag(latest_tag);
        config_set_auto_update_www_tag(latest_tag);
        config_save();
        s_last_result = AUTO_UPDATE_RESULT_UP_TO_DATE;
        return;   // ← este return
```
Substituir apenas o `return` final desse bloco por:
```c
        s_last_result = AUTO_UPDATE_RESULT_UP_TO_DATE;
        s_is_checking = false;
        return;
```

- [ ] **Step 5: Verificar build**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Esperado: `Project build complete.`

- [ ] **Step 6: Commit**

```bash
git add main/update/auto_updater.c
git commit -m "feat(auto-updater): instrument run_check() with upd_log() for web log display"
```

---

### Task 3: Expor API pública em auto_updater.h e implementar getters

**Files:**
- Modify: `main/update/auto_updater.h`
- Modify: `main/update/auto_updater.c`

- [ ] **Step 1: Adicionar declarações ao header**

Em `main/update/auto_updater.h`, após a declaração de `auto_updater_trigger_now()`, adicionar:

```c
/**
 * @brief Returns true if an update check is currently in progress.
 */
bool auto_updater_is_checking(void);

/**
 * @brief Returns the log from the last (or current) update check run.
 *        String is newline-separated, up to 1024 bytes. Never NULL.
 */
const char *auto_updater_get_last_run_log(void);
```

- [ ] **Step 2: Implementar getters em auto_updater.c**

Em `main/update/auto_updater.c`, no bloco `// Public API`, após `auto_updater_trigger_now()`, adicionar:

```c
bool auto_updater_is_checking(void)
{
    return s_is_checking;
}

const char *auto_updater_get_last_run_log(void)
{
    return s_last_run_log;
}
```

- [ ] **Step 3: Verificar build**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Esperado: `Project build complete.`

- [ ] **Step 4: Commit**

```bash
git add main/update/auto_updater.h main/update/auto_updater.c
git commit -m "feat(auto-updater): expose is_checking and get_last_run_log() public API"
```

---

### Task 4: Adicionar campos is_checking e last_run_log ao GET /api/ota/auto-update

**Files:**
- Modify: `main/webserver/handlers/api_ota.c`

- [ ] **Step 1: Adicionar os dois campos ao JSON response**

Em `main/webserver/handlers/api_ota.c`, na função `api_ota_auto_update_get_handler()`, localizar:

```c
    cJSON_AddStringToObject(root, "last_check_result", result_str);

    char *json_str = cJSON_PrintUnformatted(root);
```

Substituir por:

```c
    cJSON_AddStringToObject(root, "last_check_result", result_str);
    cJSON_AddBoolToObject(root,   "is_checking",      auto_updater_is_checking());
    cJSON_AddStringToObject(root, "last_run_log",     auto_updater_get_last_run_log());

    char *json_str = cJSON_PrintUnformatted(root);
```

- [ ] **Step 2: Verificar build**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Esperado: `Project build complete.`

- [ ] **Step 3: Commit**

```bash
git add main/webserver/handlers/api_ota.c
git commit -m "feat(api): add is_checking and last_run_log to GET /api/ota/auto-update"
```

---

### Task 5: Adicionar log box ao ota.html

**Files:**
- Modify: `main/www/tabs/ota.html`

- [ ] **Step 1: Adicionar o elemento de log abaixo do botão Check Now**

Em `main/www/tabs/ota.html`, localizar:

```html
        <div style="margin-top:16px;">
            <button class="btn btn-secondary" onclick="auCheckNow()" id="au-check-btn">Check Now</button>
        </div>
    </div>
```

Substituir por:

```html
        <div style="margin-top:16px;">
            <button class="btn btn-secondary" onclick="auCheckNow()" id="au-check-btn">Check Now</button>
        </div>
        <div id="au-log-box" class="hidden" style="margin-top:12px;">
            <pre id="au-log-content" style="background:#111;color:#0f0;font-size:0.8em;padding:10px;border-radius:4px;max-height:200px;overflow-y:auto;white-space:pre-wrap;margin:0;"></pre>
        </div>
    </div>
```

- [ ] **Step 2: Commit**

```bash
git add main/www/tabs/ota.html
git commit -m "feat(web): add log box to OTA auto-update card"
```

---

### Task 6: Atualizar auCheckNow() em ota.js para polling do log

**Files:**
- Modify: `main/www/tabs/ota.js`

- [ ] **Step 1: Substituir a função auCheckNow() pelo versão com polling**

Em `main/www/tabs/ota.js`, localizar e substituir a função `auCheckNow()` completa:

```javascript
async function auCheckNow() {
    const btn = document.getElementById('au-check-btn');
    if (btn) btn.disabled = true;
    try {
        await api('ota/auto-update', 'POST', { trigger_now: true });
        toast('Check triggered', 'success');
    } catch (e) {
        toast('Failed to trigger check', 'error');
    } finally {
        if (btn) setTimeout(() => { btn.disabled = false; }, 3000);
    }
}
```

Por:

```javascript
async function auCheckNow() {
    const btn = document.getElementById('au-check-btn');
    const logBox = document.getElementById('au-log-box');
    const logContent = document.getElementById('au-log-content');

    if (btn) btn.disabled = true;
    if (logBox) logBox.classList.remove('hidden');
    if (logContent) logContent.textContent = 'Aguardando resposta do dispositivo...';

    try {
        await api('ota/auto-update', 'POST', { trigger_now: true });
        toast('Check triggered', 'success');
    } catch (e) {
        toast('Failed to trigger check', 'error');
        if (btn) btn.disabled = false;
        return;
    }

    // Poll every 2s until check completes
    let pollTimer = setInterval(async () => {
        try {
            const au = await api('ota/auto-update');
            if (au.last_run_log) {
                logContent.textContent = au.last_run_log;
                logContent.scrollTop = logContent.scrollHeight;
            }
            if (!au.is_checking && au.last_run_log) {
                clearInterval(pollTimer);
                if (btn) btn.disabled = false;
            }
        } catch (e) {
            // Device may be rebooting after firmware update — stop polling
            clearInterval(pollTimer);
            if (btn) btn.disabled = false;
        }
    }, 2000);
}
```

- [ ] **Step 2: Verificar build completo (firmware + web)**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

Esperado: `Project build complete.`

- [ ] **Step 3: Commit**

```bash
git add main/www/tabs/ota.js
git commit -m "feat(web): show auto-update log in real time after Check Now click"
```

---

## Teste Manual

Após flashar o dispositivo com `./flash.sh update`:

1. Abrir a interface web → aba OTA → card Auto Update
2. Clicar em "Check Now"
3. O log box aparece com "Aguardando resposta do dispositivo..."
4. Após ~2s, as primeiras linhas de log aparecem (ex: "Latest tag for branch 'main'...")
5. O log vai atualizando a cada 2s até o check terminar
6. Se houver update disponível, as linhas de flash aparecem e o dispositivo reinicia
7. Se já estiver up to date, o log termina com "Already up to date (main-rN)"
