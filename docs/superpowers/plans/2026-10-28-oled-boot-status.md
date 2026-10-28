# OLED como painel de status do boot — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O OLED sobe no início do boot e mostra, em ordem: a lista de etapas do boot; o estado do modo setup ESP-NOW; e, na operação normal, temperatura e umidade sempre visíveis, com o status de WiFi e LoRa nas linhas de rótulo.

**Architecture:** O barramento I2C é separado de `sensor_manager_init()` para o OLED subir antes do config. Um módulo novo, `boot_status`, guarda a lista de etapas, escreve no log e desenha a lista via `oled_display`. O `oled_display` ganha um mutex recursivo e é inicializado uma única vez. `lorawan_handler` expõe um estado de link, e `espnow_setup` guarda uma máquina de estados para a linha de estado da tela SETUP. Falhas de WiFi, web, LoRa e config deixam de abortar o `app_main`.

**Tech Stack:** C (ESP-IDF v5.5.3, FreeRTOS), C++ só em `lorawan_handler.cpp`, SSD1306 128×64 via `driver/i2c_master.h`.

**Spec:** `docs/superpowers/specs/2026-10-28-oled-boot-status-design.md`

## Global Constraints

- ESP-IDF v5.5.3: sempre `. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh` antes de compilar.
- Os 4 SKUs precisam compilar: `jvtech_4mb_standard`, `jvtech_4mb_thermocouple`, `jvtech_2mb_standard`, `jvtech_2mb_headless`. O headless não tem `CONFIG_OLED_ENABLED` nem `CONFIG_WIFI_ENABLED`.
- O projeto **não tem suíte de testes**. O ciclo de cada task é: build do(s) SKU(s) afetado(s) → flash → verificação na placa pelo monitor serial e pelo OLED. Não criar infraestrutura de teste.
- Nenhum texto no OLED passa de **21 colunas** (fonte 6×8). A fonte cobre só ASCII 32–126, então nada de acentos nos textos de tela.
- Toda função pública nova de `oled_display.h` precisa de um stub `static inline` no ramo `#else` (OLED desligado).
- Seguir o estilo atual: `static const char *TAG`, `ESP_LOG*`, comentários em inglês no código, guards `#if CONFIG_OLED_ENABLED` / `#if CONFIG_WIFI_ENABLED`.
- Os textos de tela são fixos pelo spec:
  - Prefixos da lista de boot: `ok`, `..`, `ER`.
  - Setup: `Aguardando dongle`, `Dongle conectado`, `Identificando`, `Config OK, falta CMT`, `ERRO ao salvar cfg`, `Gravado! Reiniciando`.
  - WiFi: `WiFi:...`, `WiFi:STA`, `WiFi:AP`, `WiFi:FALHA`.
  - LoRa: `LoRa:...`, `LoRa:ERRO`, `LoRa:semcred`, `LoRa:joinN`, `LoRa:OK`.
- A branch já tem trabalho não commitado (`oled_display_show_setup`, `setup_display_refresh`, re-leitura após SET). O plano parte dele. Commitar esse trabalho primeiro, num commit próprio (Task 0).

## Review Focus

1. **OLED ausente na placa** (SKU com OLED, display desconectado): o boot precisa seguir normalmente, só com o log serial, sem travar no mutex nem dar crash. Verificado na Task 8.
2. **Corrida no framebuffer** entre `app_main` (lista de boot), `display_task`, `button_task` (barra de factory reset) e `espnow_setup_run`: nenhuma tela deve sair misturada. Verificado na Task 1 (mutex) e na Task 8 (segurar o botão durante a tela de sensores).
3. **LittleFS do userdata corrompido ou ausente:** `ER Config`, o setup é pulado mesmo com o botão pressionado, e a tela de sensores aparece com `WiFi:AP` e `LoRa:semcred`. Verificado na Task 8 (flash com a partição userdata apagada).
4. **Credenciais LoRa com hex inválido** (tamanho certo, caractere não hex): devem aparecer como `LoRa:semcred`, não como `LoRa:joinN` subindo para sempre. Verificado na Task 5.
5. **Rolagem da lista de boot com mais de 7 linhas** (WiFi STA falha → AP, todas as etapas com detalhe): a etapa atual precisa continuar visível. Verificado na Task 4.

---

## File Structure

| Arquivo | Responsabilidade |
|---|---|
| `main/display/boot_status.c/.h` (**novo**) | Lista de etapas do boot: estado, log serial, montagem das linhas e desenho via `oled_display_show_boot()`. Compilado em todos os SKUs. |
| `main/display/oled_display.c/.h` | Mutex recursivo, init idempotente, `oled_display_is_ready()`, `oled_display_show_boot()`, linha de estado em `oled_display_show_setup()` e status em `oled_display_show_sensors()`. |
| `main/sensors/sensor_manager.c/.h` | `sensor_manager_bus_init()`, separado de `sensor_manager_init()`. |
| `main/lorawan/lorawan_handler.cpp/.h` | `lorawan_link_state_t` e `lorawan_get_link_state()`. |
| `main/setup/espnow_setup.c` | Máquina de estados do setup. `wifi_espnow_init()` passa a retornar `esp_err_t`, sem `ESP_ERROR_CHECK`. Remove o init próprio do OLED. |
| `main/main.c` | Nova ordem de init com etapas, sem `return` em falha, setup pulado se o config falhar, `display_task` sem init e sem a espera de 5 s, formatação do status. |
| `main/CMakeLists.txt` | Adiciona `display/boot_status.c` à lista base de `srcs`. |

---

### Task 0: Commitar o trabalho em andamento da tela SETUP

**Files:**
- Modify (já modificados): `main/display/oled_display.c`, `main/display/oled_display.h`, `main/setup/espnow_setup.c`

- [ ] **Step 1: Build do SKU padrão**

Run: `cd /home/felipe/work/isi-latecme-quality-enddevice-2 && ./build.sh build jvtech_4mb_standard`
Expected: `Project build complete`

- [ ] **Step 2: Commit só desses três arquivos** (não incluir `sdkconfig*` nem `.vscode/`)

```bash
git add main/display/oled_display.c main/display/oled_display.h main/setup/espnow_setup.c
git commit -m "feat(setup): show MAC and readings/offsets on OLED in setup mode"
```

---

### Task 1: OLED thread-safe, init único e tela de boot

**Files:**
- Modify: `main/display/oled_display.c`, `main/display/oled_display.h`

**Interfaces:**
- Produces:
  - `bool oled_display_is_ready(void);` retorna `oled_initialized`. O stub retorna `false`.
  - `void oled_display_show_boot(const char *header, const char *const *lines, size_t n_lines);` desenha `header` em vídeo invertido na página 0 e até 7 `lines` nas páginas 1–7. Linhas `NULL` ou além de 7 são ignoradas.
  - `esp_err_t oled_display_init(void *i2c_bus)` passa a ser idempotente: se `oled_initialized`, retorna `ESP_OK` sem tocar no barramento.
  - Helper interno `static void invert_page(uint8_t page)`: XOR `0xFF` nos 128 bytes da página no framebuffer. É reusado nas Tasks 6 e 7.

- [ ] **Step 1: Mutex recursivo** `static SemaphoreHandle_t s_oled_mutex`, criado em `oled_display_init()` antes do probe. Toda função pública (`clear`, `text`, `update`, `show_*`, `next_page`, `get_page`) faz `xSemaphoreTakeRecursive(..., portMAX_DELAY)` e `Give` quando o mutex existe. Ser recursivo permite que um `show_*` chame `oled_display_text()` / `update()` por dentro.

- [ ] **Step 2: Init idempotente, `oled_display_is_ready()` e `oled_display_show_boot()`**, com stubs no `#else` do `.h` (o `.h` precisa de `#include <stddef.h>`).

- [ ] **Step 3: Build dos 4 SKUs**

Run: `for s in jvtech_4mb_standard jvtech_4mb_thermocouple jvtech_2mb_standard jvtech_2mb_headless; do ./build.sh build $s || break; done`
Expected: os 4 terminam em `Project build complete`

- [ ] **Step 4: Na placa** (`./build.sh update` + `idf.py -p /dev/ttyUSB0 monitor`), a tela de sensores e a página de sistema funcionam como antes. Nenhuma tela nova aparece ainda.

- [ ] **Step 5: Commit**

```bash
git add main/display/oled_display.c main/display/oled_display.h
git commit -m "feat(oled): recursive mutex, idempotent init and boot list screen"
```

---

### Task 2: Módulo `boot_status`

**Files:**
- Create: `main/display/boot_status.h`, `main/display/boot_status.c`
- Modify: `main/CMakeLists.txt` (adicionar `"display/boot_status.c"` na lista base de `srcs`, fora do `if(CONFIG_OLED_ENABLED)`)

**Interfaces:**
- Consumes: `oled_display_show_boot()`, `oled_display_is_ready()` (Task 1).
- Produces:

```c
typedef enum {
    BOOT_STEP_RUNNING = 0,  // ".."
    BOOT_STEP_OK,           // "ok"
    BOOT_STEP_ERROR,        // "ER"
} boot_step_state_t;

void boot_status_init(void);                                   // mutex + header with version
int  boot_status_begin(const char *name, const char *detail);  // returns step id (>=0), or -1 if full
void boot_status_end(int step, boot_step_state_t state, const char *detail); // detail NULL keeps the current one
void boot_status_finish(void);                                 // stops drawing; other screens take over
bool boot_status_is_finished(void);
```

- [ ] **Step 1: Implementar.** Valores fixos:
  - No máximo 12 etapas, `name` com até 18 caracteres e `detail` com até 18 (truncar).
  - Cabeçalho: `" BOOT"` à esquerda e `esp_app_get_description()->version` alinhada à direita, tudo em 21 colunas. A versão é truncada pela esquerda se não couber.
  - Cada etapa vira a linha `"<pp> <name>"`, onde `pp` ∈ {`ok`, `..`, `ER`}. Se `detail` não estiver vazio, vem a linha `"   <detail>"`.
  - Rolagem: só as **últimas 7 linhas** vão para `oled_display_show_boot()`.
  - Cada `begin` e `end` escreve no log (`TAG "BOOT"`, `ESP_LOGI`; `ESP_LOGW` para `ER`) no formato `[ok] Config (detalhe)` e redesenha, a menos que `boot_status_finish()` já tenha sido chamado. Depois do finish, `begin`/`end` só registram no log.
  - `step` inválido em `end` é ignorado.

- [ ] **Step 2: Chamada provisória para validar.** Em `app_main`, logo após `buzzer_init()`, chamar `boot_status_init()` e registrar 9 etapas fictícias com detalhe. O OLED ainda nem foi inicializado aqui, então a validação nesta task é pelo log serial. A tela é validada na Task 4.

- [ ] **Step 3: Build dos 4 SKUs** (mesmo comando da Task 1). Expected: os 4 completam.

- [ ] **Step 4: Na placa**, o monitor mostra as 9 linhas `[..]`/`[ok]` com a tag `BOOT` (lembrar `esp_log_level_set("BOOT", ESP_LOG_INFO)` no `app_main`). Depois, **remover as etapas fictícias** e manter só o `boot_status_init()` e o `esp_log_level_set`.

- [ ] **Step 5: Commit**

```bash
git add main/display/boot_status.c main/display/boot_status.h main/CMakeLists.txt main/main.c
git commit -m "feat(boot): add boot_status step list (log + OLED)"
```

---

### Task 3: Separar o barramento I2C da detecção de sensores

**Files:**
- Modify: `main/sensors/sensor_manager.c:99-135`, `main/sensors/sensor_manager.h`

**Interfaces:**
- Produces: `esp_err_t sensor_manager_bus_init(void);` cria o `i2c_bus` (mesma `bus_cfg` de hoje) e é idempotente (`ESP_OK` se já existe). Não lê config. `sensor_manager_get_i2c_bus()` retorna o barramento logo depois dessa chamada.
- `sensor_manager_init()` passa a chamar `sensor_manager_bus_init()` em vez de criar o barramento. O resto (mutex de dados, `detect_sensor()`, MAX6675) não muda.

- [ ] **Step 1: Implementar a separação.**
- [ ] **Step 2: Build dos 4 SKUs.** Expected: os 4 completam.
- [ ] **Step 3: Na placa**, o log ainda mostra `I2C bus initialized (SDA=21, SCL=22)` e o sensor detectado, e a tela de sensores mostra os valores.
- [ ] **Step 4: Commit**

```bash
git add main/sensors/sensor_manager.c main/sensors/sensor_manager.h
git commit -m "refactor(sensors): split I2C bus creation from sensor detection"
```

---

### Task 4: Nova sequência de boot no `app_main`

**Files:**
- Modify: `main/main.c` (`init_wifi`, `display_task`, `app_main`)

**Interfaces:**
- Consumes: `boot_status_*` (Task 2), `sensor_manager_bus_init()` (Task 3), `oled_display_init()` / `oled_display_is_ready()` (Task 1).
- Produces: `static bool s_wifi_failed` em `main.c` (lido pela Task 6).

- [ ] **Step 1: Ordem do `app_main`.** Os nomes das etapas ficam entre aspas:
  1. `log_buffer_init`, ISR de GPIO, níveis de log (incluindo `BOOT`), `buzzer_init`, `boot_status_init`.
  2. `"Display"`: `sensor_manager_bus_init()` + `oled_display_init(sensor_manager_get_i2c_bus())` (só com `CONFIG_OLED_ENABLED`). Se falhar, `ER` e segue.
  3. `"Config"`: `bool config_ok = (config_init() == ESP_OK)`. Se falhar, `ER` com o detalhe `"usando padroes"` e **segue** (sem `return`). O `config_init()` já carrega os padrões e cria o mutex antes de montar o LittleFS (`config_manager.c:593-614`).
  4. `"Sensor"`: `sensor_manager_init()` + `sensor_manager_read()`. O detalhe é `sensor_name` de `sensor_manager_get_data()`, com `" +TC"` se `thermocouple_valid`. Se o init falhar, `ER`.
  5. (WiFi) A decisão de setup passa a ser `config_ok && (!provisioned || button_held)`. Se entrar, a etapa `"Modo SETUP"` fica `ok` com o detalhe `"botao"` ou `"nao provisionado"` e chama `espnow_setup_run()`. A chamada duplicada de `sensor_manager_init/read` nesse ramo é removida.
  6. Melodia de boot e status LED (não são etapas).
  7. (WiFi) Etapas em `init_wifi()`. Se `wifi_manager_init` falhar: `"WiFi"` com `ER` e `s_wifi_failed = true`. Senão, quando há SSID e o modo AP está desligado: `"WiFi STA"` com detalhe = SSID, `ok` ou `ER`. Se cair para AP: `"WiFi AP"` com detalhe = SSID do AP, `ok` ou `ER` (o `ER` também marca `s_wifi_failed`). Em qualquer falha, **nenhum `return`** no `app_main`.
  8. (WiFi) `"Web"` só se o WiFi não falhou. O detalhe é o IP de `wifi_manager_get_ip()`. Em falha, `ER` e segue.
  9. `health_monitor`, `clock_sync_init`, `alarm_manager`, `button_handler`: sem etapas, como hoje.
  10. `"Tasks"`: cria as tasks como hoje e marca `ok`. Depois `vTaskDelay(pdMS_TO_TICKS(1000))` e `boot_status_finish()`.

- [ ] **Step 2: `display_task`.** Remover o `vTaskDelay(5000)` e o `oled_display_init()`. No início: `while (!boot_status_is_finished()) vTaskDelay(pdMS_TO_TICKS(100));` e, se `!oled_display_is_ready()`, `vTaskDelete(NULL)`.

- [ ] **Step 3: Build dos 4 SKUs.** Expected: os 4 completam.

- [ ] **Step 4: Na placa, boot normal com WiFi STA válido:** a lista mostra `ok Display`, `ok Config`, `ok Sensor` + modelo, `ok WiFi STA` + SSID, `ok Web` + IP e `ok Tasks`. Cerca de 1 s depois, aparece a tela de sensores.

- [ ] **Step 5: Na placa, SSID errado pela web UI e reboot (Review Focus 5):** `ER WiFi STA` e `ok WiFi AP`. Com mais de 7 linhas, a lista rola e a etapa atual fica visível. A tela de sensores aparece no fim.

- [ ] **Step 6: Commit**

```bash
git add main/main.c
git commit -m "feat(boot): early OLED, step list on screen, failures no longer abort boot"
```

---

### Task 5: Estado do link LoRa

**Files:**
- Modify: `main/lorawan/lorawan_handler.h`, `main/lorawan/lorawan_handler.cpp` (`lorawan_task`, linhas ~490-556)

**Interfaces:**
- Produces:

```c
typedef enum {
    LORAWAN_LINK_IDLE = 0,          // task not started yet
    LORAWAN_LINK_RADIO_ERROR,       // lorawan_init() failed
    LORAWAN_LINK_NO_CREDENTIALS,    // missing or invalid DevEUI/JoinEUI/AppKey
    LORAWAN_LINK_JOINING,
    LORAWAN_LINK_JOINED,
} lorawan_link_state_t;

lorawan_link_state_t lorawan_get_link_state(uint8_t *join_attempt); // join_attempt may be NULL; = stats.join_attempts
```

- [ ] **Step 1: Implementar.** O estado fica numa variável `static volatile`, com escrita só pela `lorawan_task`. Transições:
  - `lorawan_init()` falha → `RADIO_ERROR` (antes do `vTaskDelete`).
  - Laço de espera de credenciais → `NO_CREDENTIALS`.
  - Cada `lorawan_join()` → `JOINING`.
  - Join ok → `JOINED`.
  - `lorawan_join()` retorna `ESP_ERR_INVALID_ARG` (hex inválido) → `NO_CREDENTIALS` (Review Focus 4).
  - Re-join no laço principal → `JOINING`, depois `JOINED` se der certo.

- [ ] **Step 2: Build dos 4 SKUs.** Expected: os 4 completam.

- [ ] **Step 3: Na placa**, adicionar temporariamente um `ESP_LOGI` do estado no laço de join e conferir três casos: `NO_CREDENTIALS` sem credenciais; `NO_CREDENTIALS` com DevEUI de 16 caracteres contendo `ZZ`; `JOINING` → `JOINED` com credenciais válidas e gateway ao alcance. Remover o log temporário.

- [ ] **Step 4: Commit**

```bash
git add main/lorawan/lorawan_handler.h main/lorawan/lorawan_handler.cpp
git commit -m "feat(lorawan): expose link state (radio error, no creds, joining, joined)"
```

---

### Task 6: Status WiFi/LoRa na tela de sensores

**Files:**
- Modify: `main/display/oled_display.c` (`oled_display_show_sensors`), `main/display/oled_display.h`, `main/main.c` (`display_task`)

**Interfaces:**
- Consumes: `lorawan_get_link_state()` (Task 5), `wifi_manager_get_status()`, `s_wifi_failed` (Task 4).
- Produces: `void oled_display_show_sensors(float temp, float hum, float tc_temp, const char *status_top, const char *status_bottom);`. Cada status `NULL` ou vazio não desenha nada. O texto vai **alinhado à direita** (`x = OLED_WIDTH - strlen * 6`) na página 0 (`status_top`) e na página 4 (`status_bottom`), truncado em 16 caracteres. Os rótulos e os valores 3x não mudam, nas duas variantes (padrão e termopar).

- [ ] **Step 1: Mudar a assinatura e o stub.**

- [ ] **Step 2: Em `main.c`, `static void format_wifi_status(char *out, size_t n)` e `static void format_lora_status(char *out, size_t n)`.**
  - WiFi: `s_wifi_failed` ou `WIFI_STATUS_ERROR` → `WiFi:FALHA`; `CONNECTED` → `WiFi:STA`; `AP_MODE` → `WiFi:AP`; `CONNECTING`/`DISCONNECTED` → `WiFi:...`. Sem `CONFIG_WIFI_ENABLED`, a string fica vazia.
  - LoRa: `IDLE` → `LoRa:...`; `RADIO_ERROR` → `LoRa:ERRO`; `NO_CREDENTIALS` → `LoRa:semcred`; `JOINING` → `LoRa:join%u` (tentativa); `JOINED` → `LoRa:OK`.

  A `display_task` passa os dois para `oled_display_show_sensors()`.

- [ ] **Step 3: Build dos 4 SKUs.** Expected: os 4 completam.

- [ ] **Step 4: Na placa, sem credenciais LoRa e com WiFi STA ok:** `Temp       WiFi:STA` e `Hum    LoRa:semcred`, com T/H em 3x como antes. No SKU termopar, conferir `Int.`/`Ext.` com o status e a alternância T/H funcionando.

- [ ] **Step 5: Commit**

```bash
git add main/display/oled_display.c main/display/oled_display.h main/main.c
git commit -m "feat(oled): WiFi and LoRa status on sensor screen label rows"
```

---

### Task 7: Linha de estado do setup e ESP-NOW sem panic

**Files:**
- Modify: `main/setup/espnow_setup.c`, `main/display/oled_display.c/.h` (`oled_display_show_setup`)

**Interfaces:**
- Consumes: `boot_status_begin/end/finish` (Task 2), `invert_page()` (Task 1).
- Produces: `void oled_display_show_setup(const char *mac_str, const sensor_data_t *data, float temp_corr, float hum_corr, float tc_corr, const char *state);`. `state` vai na página 3, com um espaço à esquerda, completado com espaços até 21 colunas e com `invert_page(3)`. `NULL` deixa a página vazia.

- [ ] **Step 1: Máquina de estados em `espnow_setup.c`.**

```c
typedef enum {
    SETUP_ST_WAITING = 0,   // "Aguardando dongle"
    SETUP_ST_CONNECTED,     // "Dongle conectado"
    SETUP_ST_CONFIG_OK,     // "Config OK, falta CMT"
    SETUP_ST_SAVE_ERROR,    // "ERRO ao salvar cfg"
    SETUP_ST_REBOOTING,     // "Gravado! Reiniciando"
} setup_state_t;
static setup_state_t s_state;
static TickType_t s_identify_until;   // "Identificando" overrides the text while now < this
```

  Transições (spec, seção 2):
  - `handle_discover`/`handle_read`: só `WAITING` → `CONNECTED`.
  - `handle_set` endereçado a este MAC: `config_save` ok → `CONFIG_OK`; falha → `SAVE_ERROR`. Erros antes de conferir o MAC (`missing enc`, `decrypt failed`, `bad json`, `no mem`) **não mudam** o estado.
  - `handle_identify` endereçado: `s_identify_until = now + pdMS_TO_TICKS(3000)`.
  - `handle_commit` endereçado: falha → `SAVE_ERROR`; ok → `REBOOTING` e `setup_display_refresh()` **antes** do `vTaskDelay(200)` / `esp_restart()`.

  `setup_display_refresh()` passa o texto do estado. `Identificando` volta sozinho, porque o laço já redesenha a cada 2 s.

- [ ] **Step 2: ESP-NOW sem panic.** `static esp_err_t wifi_espnow_init(const char **failed_step)` troca cada `ESP_ERROR_CHECK` por checagem com retorno. O retry de NVS erase continua. `failed_step` recebe um nome curto (`"nvs"`, `"wifi"`, `"espnow"`, `"queue"`, `"peer"`). Em `espnow_setup_run()`:
  - `boot_status_begin("ESP-NOW", NULL)`.
  - Em falha: `boot_status_end(id, BOOT_STEP_ERROR, detalhe)`, com o detalhe `"<failed_step> <esp_err_to_name>"` truncado. Depois `for (;;) vTaskDelay(portMAX_DELAY);` (sem reboot).
  - Em sucesso: `ok`, depois `boot_status_finish()` e a tela SETUP.
  - Remover o bloco `#if CONFIG_OLED_ENABLED` que inicializa o OLED. O `sensor_manager_init/read` no início vira redundante, mas pode ficar (é idempotente).

- [ ] **Step 3: Build dos 4 SKUs.** Expected: os 4 completam.

- [ ] **Step 4: Na placa, com o dongle e o app `isi-latecme-quality-donglesetup`:**
  - Boot não provisionado: a lista mostra `ok Modo SETUP` / `nao provisionado` e `ok ESP-NOW`, e a tela SETUP aparece com `Aguardando dongle`.
  - DISCOVER → `Dongle conectado`.
  - SET → `Config OK, falta CMT`, e um READ depois disso **mantém** esse estado.
  - IDENTIFY → `Identificando` por ~3 s, depois volta.
  - COMMIT → `Gravado! Reiniciando` e o reboot vai para o boot normal.
  - Segurar o botão no boot: o detalhe mostra `botao`.

- [ ] **Step 5: Commit**

```bash
git add main/setup/espnow_setup.c main/display/oled_display.c main/display/oled_display.h
git commit -m "feat(setup): state line on SETUP screen; ESP-NOW init failure no longer panics"
```

---

### Task 8: Verificação final na placa (Review Focus)

**Files:** nenhum, a não ser que algum caso falhe e precise de correção.

- [ ] **Step 1: Build limpo dos 4 SKUs.** Expected: os 4 completam sem warnings novos.

- [ ] **Step 2: OLED desconectado (Review Focus 1)** no `jvtech_4mb_standard`: o log mostra `[ER] Display`, o boot completa, o uplink LoRa e a web funcionam, e não há crash nem WDT no monitor.

- [ ] **Step 3: Corrida no display (Review Focus 2):** com a tela de sensores ativa, segurar o botão ~5 s e soltar. A barra de factory reset e a tela de sensores alternam sem pixels misturados. Depois, um toque curto troca para a página de sistema.

- [ ] **Step 4: Config falhando (Review Focus 3):** apagar só a partição userdata (`parttool.py --port /dev/ttyUSB0 erase_partition --partition-name userdata`, depois reboot sem formatar).
  - Expected: `ER Config` / `usando padroes`, e **nenhum** modo setup, mesmo com o botão pressionado no boot.
  - Expected: a tela de sensores aparece com `WiFi:AP` e `LoRa:semcred`.
  - Se o `config_init` formatar a partição sozinho e o caso não reproduzir, registrar isso como o comportamento real.
  - Restaurar com `./build.sh all`.

- [ ] **Step 5: SKU headless:** `./build.sh build jvtech_2mb_headless` e flash. A lista de boot aparece só no log, e o uplink funciona.

- [ ] **Step 6: Commit de qualquer correção**, com uma mensagem que descreva o caso corrigido.
