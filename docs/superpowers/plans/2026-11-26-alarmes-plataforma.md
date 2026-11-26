# Alarmes sincronizados com a plataforma — firmware — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O dispositivo aplica os limites de alarme que a plataforma manda por LoRaWAN (fPort 11) ou pela página `limites.html`, e informa à plataforma (fPort 3) o estado de cada canal, o aperto do botão, a versão dos limites e o IP local.

**Architecture:**
- O codec puro (`alarm_sync.c`) não depende do ESP-IDF e tem teste de host com `gcc`.
- O `lorawan_send` só copia o downlink da fPort 11 para uma fila; uma task pequena (`alarm_sync_task`) aplica os limites fora do mutex do rádio.
- Os eventos são **sinalizados**, não enfileirados um a um: um event group mais contadores. A `uplink_task` monta o retrato do estado na hora de enviar, o que junta rajadas num único uplink e não perde evento se ainda não houver join.

**Tech Stack:** ESP-IDF 5.5.3, FreeRTOS, RadioLib 7.5.0, cJSON, esp_http_server, LittleFS (partição `www`).

**Spec:** `isi-iot-platform/docs/superpowers/specs/2026-11-25-gestao-alarmes-design.md`, §7. A plataforma tem plano próprio, `isi-iot-platform/docs/superpowers/plans/2026-11-26-alarmes-plataforma.md`. Os formatos abaixo são o contrato entre os dois.

## Global Constraints

- Branch `feat/alarmes-plataforma`, a partir de `origin/main_dev`; PR para `main_dev`. Commits em inglês, Conventional Commits com escopo (`feat(alarm): …`).
- Todo SKU compila: `./build.sh build jvtech_4mb_standard`, `jvtech_4mb_thermocouple`, `jvtech_2mb_standard` e `jvtech_2mb_headless`. Código de rede, Wi-Fi e web fica sob `#if CONFIG_WIFI_ENABLED`. Sem Wi-Fi, o IP vai `0.0.0.0` e não existe página.
- **Downlink fPort 11, 18 bytes, big-endian:** `0x01 | versao u16 | 3 × [flags u8 | low i16 | high i16]`, na ordem temperatura, umidade, termopar. Valores em décimos (0,1 °C ou 0,1 %UR). Flags: bit0 = ativo, bit1 = **manter** (ignora os demais campos do canal e não mexe nele). Tamanho ≠ 18, primeiro byte ≠ 0x01 ou versão fora de 1–32767 → rejeitado (erro 1).
- **Uplink de evento fPort 3, 10 bytes, big-endian:** `0x01 | versao u16 (bit15 = 1 quando a última alteração foi local) | estados u8 (bits 1-0 temperatura, 3-2 umidade, 5-4 termopar; 0 IDLE, 1 ACTIVE, 2 ACKNOWLEDGED) | botao u8 (toques que silenciaram desde o último evento enviado, satura em 255) | ipv4 4 bytes | erro u8`. Não confirmado.
- **Códigos de erro:** 0 OK, 1 formato, 2 `low ≥ high`, 3 fora da faixa física, 4 falha ao gravar. Faixas físicas: temperatura −40,0 a 125,0 °C; umidade 0,0 a 100,0 %; termopar 0,0 a 1024,0 °C (MAX6675). Valem só para canal **ativo**.
- Termopar em SKU sem `CONFIG_THERMOCOUPLE_ENABLED`: o canal 2 é tratado como "manter", e o estado vai sempre 0.
- **Fragmento do `limites.html`:** `#v=<versao>&c0=<flags>,<low>,<high>&c1=…&c2=…`, com as mesmas unidades e flags do downlink.
- O evento é enviado quando qualquer canal muda de estado, quando o botão silencia, quando limites são aplicados ou rejeitados (pelo downlink ou pela API) e como batimento a cada **15 min** desde o último evento enviado. Enquanto não houver join, fica pendente.
- A lógica local do alarme (latch, ACK de todos os canais, sem histerese) **não muda**.
- Nunca chamar `lorawan_send` dentro de callback de downlink (`lorawan_handler.h:50-51`).
- Sem suíte de testes no ESP-IDF (CLAUDE.md do repo). O teste automatizado é só o de host do codec (`test/host`). O resto é verificado em bancada, com o roteiro da Task 8.

## Review Focus

1. **Pilha da `uplink_task` (4096) e da `clock_sync_task`.**
   - O downlink chega dentro de `lorawan_send`, que já reserva 256 B de buffer.
   - O caminho da fPort 11 dentro do `lorawan_send` só pode copiar bytes para a fila, sem parse nem `config_save`.
   - Verificação: `uxTaskGetStackHighWaterMark` registrado na bancada (Task 8).
2. **Leitura dos limites pela metade.** `alarm_manager_evaluate` não pode ver `low` novo com `high` velho. Por isso a leitura e a gravação em bloco usam o `s_config_mutex` (Task 2).
3. **fPort de uplink configurada como 3 ou 11.** O `POST /api/lorawan/config` recusa `port` em {3, 11, 200, 201, 202} (Task 4).
4. **POST parcial na API.** Um POST com limite inválido não pode gravar **nada**: nem os outros campos de alarme, nem intervalo, nem buzzer. Ele responde 400 com JSON `{"success":false,"message":…}` (Task 6).
5. **Rajada de eventos.** Três canais mudando ao mesmo tempo, mais o ACK e a aplicação de limites, viram **um** uplink com o retrato final, e o contador de botão não é zerado antes de o envio dar certo (Task 4).

---

### Task 1: Codec puro `alarm_sync` com teste de host

**Files:**
- Create: `main/alarm/alarm_sync.h`, `main/alarm/alarm_sync.c` (C puro, só `<stdint.h>`, `<stdbool.h>` e `<string.h>`), `test/host/Makefile`, `test/host/test_alarm_sync.c`
- Modify: `main/CMakeLists.txt` (acrescentar `alarm/alarm_sync.c` às SRCS, fora do `if(CONFIG_WIFI_ENABLED)`)

**Interfaces:**
- Produces (`alarm_sync.h`):
  - `#define ALARM_SYNC_FPORT_LIMITS 11`, `#define ALARM_SYNC_FPORT_EVENT 3`, `#define ALARM_SYNC_LIMITS_LEN 18`, `#define ALARM_SYNC_EVENT_LEN 10`.
  - `typedef enum { ALARM_SYNC_OK=0, ALARM_SYNC_ERR_FORMAT=1, ALARM_SYNC_ERR_ORDER=2, ALARM_SYNC_ERR_RANGE=3, ALARM_SYNC_ERR_SAVE=4 } alarm_sync_err_t;`
  - `typedef struct { bool keep; bool enabled; int16_t low_d; int16_t high_d; } alarm_sync_channel_t;` (valores em décimos).
  - `typedef struct { uint16_t version; alarm_sync_channel_t ch[3]; } alarm_sync_limits_t;`
  - `alarm_sync_err_t alarm_sync_parse_limits(const uint8_t *buf, size_t len, alarm_sync_limits_t *out);`
  - `alarm_sync_err_t alarm_sync_validate(const alarm_sync_limits_t *l);`. Aplica as faixas físicas das Global Constraints aos canais `enabled && !keep`.
  - `typedef struct { uint16_t version; bool origin_local; uint8_t state[3]; uint8_t button; uint8_t ip[4]; uint8_t error; } alarm_sync_event_t;`
  - `size_t alarm_sync_build_event(const alarm_sync_event_t *ev, uint8_t out[ALARM_SYNC_EVENT_LEN]);`
  - `alarm_sync_err_t alarm_sync_parse_fragment(const char *frag, alarm_sync_limits_t *out);`. Lê o fragmento do `limites.html`. Não é usado pelo C hoje; existe para o teste de host fixar o formato que o JS da Task 7 também segue.

- [ ] **Step 1: Escrever o teste que falha.** Em `test/host/test_alarm_sync.c`, com `assert` simples e `main` que imprime `OK`:
  - Parse do vetor da plataforma `01 0007 01 0096 00fa 02 0000 0000 00 0000 0000` → versão 7; canal 0 ativo, 150..250; canal 1 `keep`; canal 2 desativado.
  - Parse com 17 bytes, com primeiro byte 0x02 e com versão 0 → `ALARM_SYNC_ERR_FORMAT`.
  - Validate: low 250 / high 150 → `ALARM_SYNC_ERR_ORDER`; temperatura high 1300 → `ALARM_SYNC_ERR_RANGE`; canal desativado com valores absurdos → OK; low negativo `ff97` (−10,5) → OK.
  - Build: versão 5, `origin_local=true`, estados {1,0,2}, botão 3, IP 192.168.10.23, erro 0 → `01 8005 21 03 c0a80a17 00`.
  - Fragmento: `v=7&c0=1,150,250&c1=2,0,0&c2=0,0,0` → o mesmo resultado do primeiro parse.
- [ ] **Step 2: Rodar e ver falhar.** `make -C test/host` → erro de link (funções inexistentes).
- [ ] **Step 3: Implementar `alarm_sync.c`.** O `Makefile` compila com `gcc -Wall -Wextra -Werror -I../../main/alarm ../../main/alarm/alarm_sync.c test_alarm_sync.c -o test_alarm_sync && ./test_alarm_sync`.
- [ ] **Step 4: Rodar e ver passar.** `make -C test/host` → `OK`. Depois, `./build.sh build jvtech_2mb_headless` compila.
- [ ] **Step 5: Commit.** `feat(alarm): pure codec for platform limits (fPort 11) and events (fPort 3)`

### Task 2: Config — versão, origem e acesso em bloco

**Files:**
- Modify: `main/config/config_manager.c`, `main/config/config_manager.h`, `main/alarm/alarm_manager.c` (`alarm_manager_evaluate` passa a ler em bloco)

**Interfaces:**
- Consumes: `alarm_sync_limits_t` (Task 1).
- Produces:
  - Os campos de `config_t` `alarm_limits_version` (uint16, padrão 0) e `alarm_limits_origin_local` (bool, padrão false).
  - No JSON, `sensors.alarms.limits_version` e `sensors.alarms.limits_origin_local`, opcionais na leitura, para manter configs antigas válidas.
  - `typedef struct { bool enabled; float low; float high; } alarm_channel_cfg_t;`
  - `void config_get_alarm_block(alarm_channel_cfg_t out[3]);`. Lê os três canais sob `s_config_mutex`. Sem termopar, o canal 2 vem `enabled=false`.
  - `esp_err_t config_apply_alarm_limits(const alarm_sync_limits_t *l, bool origin_local);`. Sob `s_config_mutex`:
    - aplica os canais com `!keep`;
    - grava a versão e a origem (com `origin_local=true`, a versão não muda);
    - chama `_config_save_internal`.

    Devolve o erro do save.
  - `uint16_t config_get_alarm_limits_version(void);` e `bool config_get_alarm_limits_origin_local(void);`.
  - `void config_mark_alarm_limits_local(void);`, que liga a origem local, sem salvar.

- [ ] **Step 1: Implementar seguindo a receita "New config field" do CLAUDE.md do repo.**
- [ ] **Step 2: Trocar em `alarm_manager_evaluate` (`alarm_manager.c:137-159`) os 9 getters individuais por um `config_get_alarm_block`.**
- [ ] **Step 3: Compilar os 4 SKUs.** Os quatro builds terminam sem erro.
- [ ] **Step 4: Commit.** `feat(config): alarm limits version/origin and locked block access`

### Task 3: Sinalização de eventos de alarme

**Files:**
- Create: `main/alarm/alarm_events.h`, `main/alarm/alarm_events.c`
- Modify: `main/alarm/alarm_manager.c`, `main/alarm/alarm_manager.h`, `main/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `void alarm_events_init(void);`, chamado em `main.c` antes de as tasks de sensor e de uplink subirem. Já nasce com `EVT_PENDING` ligado: o primeiro evento sai logo depois do join, e a plataforma recebe IP e versão após reinício ou atualização.
  - `void alarm_events_notify(void);`, que liga o bit `EVT_PENDING` num `EventGroupHandle_t`.
  - `void alarm_events_button(void);`, que soma 1 no contador de botão (satura em 255, protegido por `portMUX`/critical) e chama `notify`.
  - `void alarm_events_set_error(uint8_t code);`, que guarda o último código e chama `notify`.
  - `bool alarm_events_wait(TickType_t timeout);`. Espera o `EVT_PENDING` **sem limpar** e devolve se o bit estava ligado.
  - `void alarm_events_take(uint8_t *button, uint8_t *error);`. Lê os valores pendentes sem zerar.
  - `void alarm_events_commit(uint8_t button_sent);`. Subtrai `button_sent`, zera o erro e limpa o `EVT_PENDING` se o contador chegou a 0. Só é chamado depois de `lorawan_send` retornar `ESP_OK`.
  - Em `alarm_manager.h`, `uint8_t alarm_manager_get_channel_state(int ch);` (0, 1 ou 2; sob `s_mutex`).
- Ganchos:
  - Em `alarm_manager_evaluate`, guardar os 3 estados antes e, se algum mudou depois, incluindo as transições forçadas por "canal desabilitado", chamar `alarm_events_notify()`.
  - Em `alarm_manager_acknowledge`, se algum canal passou de ACTIVE para ACKNOWLEDGED, chamar `alarm_events_button()` uma vez.
  - O caminho de factory reset (`button_handler.c:60`) também passa por `acknowledge`. Isso é aceitável: o dispositivo reinicia em seguida.

- [ ] **Step 1: Implementar.**
- [ ] **Step 2: Compilar os 4 SKUs.**
- [ ] **Step 3: Commit.** `feat(alarm): coalesced event signaling for platform uplinks`

### Task 4: Uplink de evento fPort 3, batimento e guarda de fPort

**Files:**
- Modify: `main/main.c` (`uplink_task`), `main/webserver/handlers/api_lorawan.c` (recusar fPort reservada), `main/config/config_manager.c` (no `config_load`, se `lorawan.port` for reservada, voltar a 1 e registrar `ESP_LOGW`)

**Interfaces:**
- Consumes: `alarm_events_*` (Task 3), `alarm_sync_build_event` (Task 1), `config_get_alarm_limits_*` (Task 2), `wifi_manager_is_connected` / `wifi_manager_get_ip` (só com `CONFIG_WIFI_ENABLED`).
- Produces:
  - `static esp_err_t send_alarm_event(void)`, em `main.c`. Monta o retrato e chama `lorawan_send(buf, 10, ALARM_SYNC_FPORT_EVENT, false)`. Com `ESP_OK`, chama `alarm_events_commit`.
  - O IP só vai preenchido em modo STA conectado (`wifi_manager_is_connected()`). Em modo AP ou sem Wi-Fi, vai 0.
  - A espera da `uplink_task`, que hoje é `vTaskDelay`, passa a ser `alarm_events_wait(ticks_até_o_próximo_uplink_periódico)`:
    - se acordar por evento e houver join: chama `send_alarm_event()` e volta a esperar o **restante** do intervalo, sem adiantar a leitura periódica;
    - ao chegar a hora do uplink periódico: envia a leitura (fPort de leitura) e, em seguida, se passaram ≥ 15 min desde o último evento enviado **ou** se há um pendente, chama `send_alarm_event()`;
    - sem join: não envia e não limpa nada, e o pendente segue para depois do join.
  - `POST /api/lorawan/config` com `port` em {3, 11, 200, 201, 202} → 400 `{"success":false,"message":"Porta reservada"}`.

- [ ] **Step 1: Implementar.** O alinhamento ao relógio (`main.c:243-252`) continua: o cálculo do próximo uplink periódico vira um *deadline* absoluto em ticks, e a espera usa `deadline − agora`.
- [ ] **Step 2: Compilar os 4 SKUs.**
- [ ] **Step 3: Commit.** `feat(lorawan): fPort 3 alarm event uplink with 15 min heartbeat`

### Task 5: Downlink fPort 11 e aplicação dos limites

**Files:**
- Modify: `main/lorawan/lorawan_handler.cpp` e `.h`, `main/main.c` (criar a task)
- Create: `main/alarm/alarm_sync_task.c` e `.h`

**Interfaces:**
- Consumes: `alarm_sync_parse_limits`, `alarm_sync_validate` (Task 1); `config_apply_alarm_limits` (Task 2); `alarm_events_set_error`, `alarm_events_notify` (Task 3).
- Produces:
  - Em `lorawan_send`:
    - usar a sobrecarga `sendReceive(data, len, port, downlink_data, &downlink_len, confirmed, NULL, &eventDown)`;
    - quando `state > 0 && eventDown.fPort == ALARM_SYNC_FPORT_LIMITS`, copiar `{len, data[18]}` para `s_limits_queue` (`xQueueSend(..., 0)`, profundidade 2; se estiver cheia, descarta com `ESP_LOGW`);
    - nada de parse nem de save aqui;
    - as outras fPorts continuam descartadas, inclusive a 10.
  - `QueueHandle_t lorawan_limits_queue(void);`, exposta em `lorawan_handler.h`.
  - `void alarm_sync_task_start(void);`. Cria a task `"alarm_sync"` com 4096 de pilha, prioridade 3, core 0.
    - O laço faz `xQueueReceive(portMAX_DELAY)` → parse → validate.
    - Se OK: `config_apply_alarm_limits(&l, false)`. Falha no save → erro 4.
    - O resultado sempre vai para `alarm_events_set_error(err)`: 0 também conta, porque gera o evento de confirmação com a versão nova.

- [ ] **Step 1: Implementar.** A task sobe em `main.c` junto da `uplink_task`, nos dois SKUs, porque LoRaWAN existe em todos. Depois de cada downlink aplicado e de cada `send_alarm_event`, registrar `ESP_LOGI` com `uxTaskGetStackHighWaterMark(NULL)` da task corrente (`alarm_sync`, `uplink`; a `clock_sync` registra o seu após o envio da fPort 202), para o passo 8 da Task 8.
- [ ] **Step 2: Compilar os 4 SKUs.**
- [ ] **Step 3: Commit.** `feat(lorawan): apply platform alarm limits from fPort 11 downlink`

### Task 6: Validação e origem no `POST /api/sensors/config`

**Files:**
- Modify: `main/webserver/handlers/api_sensors.c`, `main/www/tabs/config.js` (mostrar `r.message` quando `success=false`)

**Interfaces:**
- Consumes: `alarm_sync_validate`, `config_apply_alarm_limits`, `config_get_alarm_block`, `alarm_events_set_error`.
- Produces:
  - O handler POST, se o corpo tem qualquer campo `alarm_*`:
    - monta um `alarm_sync_limits_t` partindo do bloco atual (os campos ausentes ficam como estão);
    - converte float para décimos com arredondamento;
    - valida **antes de aplicar qualquer campo** do corpo. Inválido → 400 JSON com mensagem em português ("Mínimo deve ser menor que o máximo" ou "Fora da faixa física"), sem gravar nada;
    - com `limits_version` presente (1–32767) → `config_apply_alarm_limits(&l, false)`, que conta como origem plataforma; sem ele → `config_apply_alarm_limits(&l, true)`, origem local;
    - chama `alarm_events_set_error(0)` para gerar o evento.
  - Os demais campos (`interval`, correções, buzzer) seguem o fluxo atual, mas só depois da validação dos alarmes passar.
  - O retorno de `config_save()` passa a ser verificado: falha → 500 JSON.
  - O GET passa a devolver também `alarm_limits_version` e `alarm_limits_origin_local`.

- [ ] **Step 1: Implementar.**
- [ ] **Step 2: Compilar os 4 SKUs.**
- [ ] **Step 3: Commit.** `feat(api): validate alarm limits and track local vs platform origin`

### Task 7: Página `limites.html`

**Files:**
- Create: `main/www/limites.html` (independente; CSS e JS embutidos, sem depender do `core.js`)
- Modify: `main/webserver/handlers/static_files.c`, `handlers.h`, `web_server.c` (registrar `GET /limites.html`; o total vai a 61 de 70 handlers)

**Interfaces:**
- Consumes: o contrato do fragmento (Global Constraints) e o `POST /api/sensors/config` da Task 6.
- Produces: uma página que:
  1. Lê `location.hash`, valida o formato com a mesma regra do `alarm_sync_parse_fragment` e mostra uma tabela "Canal / Situação / Mínimo / Máximo", em que a situação é "Manter o do dispositivo", "Desligado" ou "Ativo", com valores ÷ 10.
  2. Tem o botão "Gravar no dispositivo", que faz `fetch('/api/sensors/config', {method:'POST', …})` com `alarm_temp_enabled/low/high`, `alarm_hum_*` e `alarm_tc_*` dos canais que não são "manter", mais `limits_version`. Num 401, o navegador pede a senha da página (realm `ESP32`), que é o mesmo comportamento das abas atuais.
  3. Mostra o resultado: "Limites gravados. A plataforma confirma no próximo evento. Pode fechar esta aba." ou a `message` de erro.
  4. Com fragmento ausente ou inválido, mostra "Link incompleto. Abra de novo pela plataforma." e não exibe o botão.
- O handler é `limites_html_handler`, com `serve_file(req, "/www/limites.html", "text/html")`. Sem auth no HTML, como as outras páginas estáticas; a escrita é protegida pela API.

- [ ] **Step 1: Implementar.** O arquivo deve ficar ≤ 8 KB (a partição `www` tem cerca de 40 KB livres).
- [ ] **Step 2: Compilar os SKUs com Wi-Fi.** Conferir que `limites.html` entrou na imagem (`build/www.bin`; `littlefs` no CMake raiz).
- [ ] **Step 3: Commit.** `feat(www): limites.html page to apply platform limits over Wi-Fi`

### Task 8: Verificação em bancada e documentação

**Files:**
- Create: `docs/ALARMES_PLATAFORMA.md` (contrato dos payloads, roteiro de bancada e resultados)
- Modify: `README.md` (uma linha apontando para o doc) e `docs/PENDENCIAS.md`, se existir (nada a remover; só conferir que o item 1 continua lá)

**Roteiro de bancada** (registrar a saída no doc e no PR):
1. `make -C test/host` → `OK`.
2. Gravar `jvtech_4mb_standard` num dispositivo de teste já cadastrado na plataforma de desenvolvimento ou na de produção, num tipo com `aceita_limites_remotos` **desligado**.
3. Pelo console do ChirpStack, enfileirar na fPort 11 o hex `01000701009600fa02000000000000000000` (o vetor da Task 1). Conferir:
   - o log `alarm_sync: applied v7`;
   - `GET /api/sensors/config` com `alarm_temp_low=15`, `alarm_temp_high=25` e `alarm_limits_version=7`;
   - um uplink na fPort 3 com `0100 07…` no console.
4. Enfileirar um payload com `low ≥ high` → o evento chega com `erro=2` e a config não muda.
5. Esquentar o sensor acima do limite → evento com o estado 1. Apertar o botão → evento com o estado 2 e `botao=1`.
6. Abrir `http://<ip>/limites.html#v=8&c0=1,100,300&c1=2,0,0&c2=2,0,0`, gravar com a senha e conferir que o evento chega com a versão 8 sem o bit15. Gravar pela aba normal de configuração → o evento chega com o bit15.
7. Deixar 15 min parado → chega o batimento.
8. No log, `uxTaskGetStackHighWaterMark` de `uplink`, `alarm_sync` e `clock_sync` depois dos passos 3 a 7: cada um com folga ≥ 512 B (Review Focus 1).
9. `POST /api/lorawan/config` com `{"port":3}` → 400.
10. Compilar os 4 SKUs.

- [ ] **Step 1: Executar o roteiro e anotar os resultados.**
- [ ] **Step 2: Commit.** `docs(alarm): platform sync contract and bench test results`
- [ ] **Step 3: Abrir o PR para `main_dev`,** com o roteiro e os resultados no corpo.
