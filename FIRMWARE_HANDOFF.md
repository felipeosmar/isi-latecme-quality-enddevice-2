# Handoff — Alterações necessárias no firmware do end-device

**Data:** 2026-08-05
**Para:** agente/dev que vai trabalhar no firmware do end-device
**Repo alvo:** `/home/felipe/work/isi-latecme-quality-enddevice-2`
(branch atual: `feat/espnow-setup-mode`, HEAD `c82ba1c`)
**Repo de referência (já pronto):** `/home/felipe/work/isi-latecme-quality-donglesetup`
(branch `feat/espnow-provisioning`, PR #2)

Este documento tem duas partes: **(1)** o que já foi feito no app PC e no
contrato de protocolo — contexto que você precisa conhecer; **(2)** os itens
que faltam no firmware, com arquivo, linha e critério de aceitação.

---

## Parte 1 — O que já está pronto (não precisa refazer)

### 1.1 App PC (`pc-app/`, PySide6)

A tabela de dispositivos passou de 7 para 15 colunas:

```
MAC | Nome | DevEUI | Sensor | Temp | Umid | TC | T off | H off | TC off | Novo T | Novo H | Novo TC | Status | Gravar
```

- **Nome**: preenchido a partir do campo `name` do `ANNOUNCE` (ver 1.2). Mostra
  `—` quando o dispositivo não informa nome.
- **T off / H off / TC off**: offsets atualmente gravados no dispositivo, lidos
  dos campos `tcorr`/`hcorr`/`tccorr` do `READING`. Somente leitura.
- **Novo T / Novo H / Novo TC**: editáveis pelo operador (aceita vírgula
  decimal, ex. `0,5`). Valores pendentes ficam em memória no app, indexados por
  MAC.
- **Gravar**: um botão por linha. Envia **SET parcial** — só `mac` + os offsets
  editados — para aquele dispositivo específico.

O botão "Provision all" continua existindo e mandando o SET completo
(nome gerado + SSID/senha + offsets calculados das referências).

Fluxo atual do botão "Gravar" (importante para o item **F3**):
`SET` → aguarda `ACK` → envia `COMMIT` → aguarda `ACK` → status `done`.

### 1.2 Contrato de protocolo (`shared/protocol.md`)

Duas mudanças, ambas retrocompatíveis do lado do PC:

**ANNOUNCE ganhou o campo opcional `name`** (`shared/protocol.md:70`):

```
- `name` (string, optional): Name stored on the device (e.g. "ED015");
  absent or empty if the device was never provisioned
```

**SET: campos do corpo cifrado agora são explicitamente opcionais**
(`shared/protocol.md:211`):

> **Optional plaintext body fields**: `name`, `ssid` and `pass` are optional.
> Field present = write the new value; field absent = keep the device's current
> value. The offsets `tcorr`/`hcorr`/`tccorr` are also optional and follow the
> same rule. A SET carrying only offsets (no name/ssid/pass) MUST NOT change the
> device name or WiFi credentials.

Exemplo do corpo plaintext que o app PC manda ao clicar "Gravar" em um
dispositivo (antes da cifragem AES-128-GCM):

```json
{"mac":"AA:BB:CC:DD:EE:01","tcorr":0.5}
```

Nada mudou no envelope (`m`:`LTCM`, `v`:1), no canal ESP-NOW (1), na
criptografia (AES-128-GCM, `nonce(12)||ciphertext||tag(16)` em base64) nem no
firmware do dongle.

---

## Parte 2 — O que falta fazer no firmware

Cinco itens. **F1** e **F3** são os que destravam a feature do app PC; **F4** e
**F5** são os pedidos de UI no aparelho; **F2** é verificação.

### F1 — `ANNOUNCE` deve incluir o nome gravado no dispositivo

**Por quê:** sem isso a coluna "Nome" do app PC fica sempre `—`.

**Arquivo:** `main/setup/espnow_setup.c`, função `handle_discover()`
(linhas ~201-219).

Hoje o ANNOUNCE monta `mac`, `deveui`, `fw`, `sensor`, `prov` — falta `name`:

```c
    cJSON *root = new_envelope("ANNOUNCE");
    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddStringToObject(root, "deveui", config_get_dev_eui());
    cJSON_AddStringToObject(root, "fw", "setup");
    cJSON_AddStringToObject(root, "sensor", d.sensor_name);
    cJSON_AddStringToObject(root, "name", config_get_device_name());  /* <-- add */
    cJSON_AddBoolToObject(root, "prov", config_get_factory_provisioned());
```

A API já existe: `config_get_device_name()` em
`main/config/config_manager.h:90` (campo `device_name[32]`,
`config_manager.c:39`).

**Atenção — nome default:** o default de fábrica é `"sensor-01"`
(`config_manager.c:157`), não string vazia. Logo, um aparelho nunca provisionado
vai anunciar `name:"sensor-01"` e o app PC mostrará `sensor-01` em vez de `—`.
Isso é aceitável (o campo `prov:false` já diz que não foi provisionado) e é a
opção recomendada, porque é a verdade: esse É o nome gravado. Se preferir que a
coluna fique `—` nesse caso, mande `""` quando
`!config_get_factory_provisioned()`.

**Aceitação:** com o app PC conectado ao dongle, clicar em "Discover" e ver a
coluna "Nome" preenchida com o nome real de cada aparelho. Frame do ANNOUNCE
continua bem abaixo de `MAX_FRAME_LEN` (1024, `espnow_setup.c:60`).

---

### F2 — SET parcial: já funciona, só confirmar e não regredir

**Boa notícia:** o `handle_set()` atual (`main/setup/espnow_setup.c:260-335`) já
trata cada campo como opcional — ele aplica somente o que está presente:

```c
    if (cJSON_IsNumber(tcorr))  { config_set_temp_correction(...); }        /* 309 */
    if (cJSON_IsNumber(hcorr))  { config_set_hum_correction(...); }        /* 312 */
    if (cJSON_IsNumber(tccorr)) { config_set_thermocouple_correction(...); }/* 315 */
    if (cJSON_IsString(name))   { config_set_device_name(...); }           /* 318 */
    if (cJSON_IsString(ssid) && ssid->valuestring[0] != '\0') { ... }      /* 321 */
```

Então um SET com `{"mac":...,"tcorr":0.5}` já: aplica só o offset de
temperatura, **não** toca em nome/SSID/senha, persiste com `config_save()`
(linha 329) e responde `ACK ok:true` (linha 334). Compatível com o contrato
novo sem alteração de código.

**Ação:** nenhuma mudança obrigatória. Dois endurecimentos opcionais:

1. Linha 318: `name` presente mas string vazia gravaria nome vazio. O app PC
   nunca manda isso, mas por simetria com o tratamento de `ssid` (linha 321),
   vale adicionar `&& name->valuestring[0] != '\0'`.
2. Adicionar um teste/log confirmando que um SET só-offsets não altera
   `config_get_device_name()` nem o SSID.

**Aceitação:** gravar offset individual em um aparelho já provisionado (com
nome e Wi-Fi configurados) e confirmar por `Read all`/tela do aparelho que o
nome e o SSID continuam intactos e o offset mudou.

---

### F3 — `COMMIT` reinicia o aparelho e o tira do modo de configuração ⚠️

**Este é o item que precisa de decisão sua antes de codar.**

**Estado atual:** `handle_commit()` (`main/setup/espnow_setup.c:359-382`) faz:

```c
    config_set_factory_provisioned(true);   /* 371 */
    config_save();                          /* 372 */
    send_ack(src, own_mac_str, true, NULL, seq);
    vTaskDelay(pdMS_TO_TICKS(200));         /* deixa o ACK sair pelo ar */
    esp_restart();                          /* 381 <-- reinicia */
```

**Problema:** o app PC, ao gravar offset individual, hoje manda
`SET → COMMIT`. Com o firmware atual isso marca o aparelho como provisionado e
**reinicia**, saindo do modo de configuração — exatamente o que a anotação
pedia para não acontecer ("no modo de configuração não reiniciar ao gravar
offset, ou não sair do modo de configuração"). O operador não consegue medir de
novo e reajustar o offset sem forçar o aparelho a reentrar em setup.

**Ponto-chave:** os offsets **já ficam persistidos pelo próprio SET** — o
`config_save()` da linha 329 grava em NVS antes do ACK. O `COMMIT` não persiste
offset nenhum; ele só marca `factory_provisioned` e reinicia. Ou seja, `COMMIT`
significa "encerrar o provisionamento", não "salvar".

**Opção A (recomendada) — sem mudança de protocolo nem de firmware:**
o app PC deixa de mandar `COMMIT` quando a gravação é só de offset: para no
`ACK` do SET e marca a linha como `done`. `COMMIT` continua com o significado
atual (encerrar provisionamento + reiniciar), usado apenas pelo
"Provision all".
- Firmware: **nada a fazer** neste item.
- App PC: ajustar `_write_offsets`/`_handle_ack` em
  `pc-app/src/provisioner/gui.py` (hoje o commit-ack é o que marca `done` e
  limpa a edição pendente). É uma mudança pequena, mas mexe em código já
  revisado e no PR #2 — combine antes de fazer.
- Doc: registrar em `shared/protocol.md` que SET já persiste e que COMMIT é
  opcional/terminal.

**Opção B — mudança de protocolo explícita:**
`COMMIT` ganha um campo booleano opcional (ex. `"final"`, default `true` para
retrocompatibilidade). Com `final:false` o aparelho responde `ACK ok:true`,
**não** seta `factory_provisioned` e **não** reinicia, seguindo em modo de
setup.
- Firmware: ler o campo em `handle_commit()` e condicionar as linhas 371 e 381.
- App PC: mandar `final:false` na gravação individual de offset.
- Doc: novo campo em `shared/protocol.md` (seção COMMIT + Field Type Summary).

**Recomendação:** Opção A. Menos peças móveis e semanticamente honesta — o
offset já está salvo quando o ACK do SET chega. Escolha a B se você quiser um
"confirmar" explícito por gravação de offset.

**Aceitação (qualquer opção):** gravar offset individual em um aparelho, ver o
`Status` virar `done` no app PC, e o aparelho **continuar** em modo de
configuração (sem reboot), aceitando um segundo ciclo medir → ajustar → gravar
em seguida.

---

### F4 — Tela de informações de rede deve ficar até apertar o botão de novo

**Estado atual:** a página SYSTEM volta sozinha para SENSORS depois de 5
segundos.

**Arquivo:** `main/display/oled_display.c`

```c
static int64_t s_system_page_entered_us = 0;                     /* 48 */
#define SYSTEM_PAGE_TIMEOUT_US  (5 * 1000 * 1000)  // 5 seconds   /* 50 */

void oled_display_next_page(void)                                /* 528 */
{
    current_page = (oled_page_t)((current_page + 1) % OLED_PAGE_MAX);
    if (current_page == OLED_PAGE_SYSTEM) {
        s_system_page_entered_us = esp_timer_get_time();          /* 532 */
    }
}

oled_page_t oled_display_get_page(void)                          /* 536 */
{
    if (current_page == OLED_PAGE_SYSTEM &&
        (esp_timer_get_time() - s_system_page_entered_us) >= SYSTEM_PAGE_TIMEOUT_US) {
        current_page = OLED_PAGE_SENSORS;                        /* 540 <-- auto-volta */
    }
    return current_page;
}
```

**Fix:** remover o auto-retorno — apagar `s_system_page_entered_us` (48),
`SYSTEM_PAGE_TIMEOUT_US` (50), a atribuição em 531-533 e o bloco `if` em
538-541, deixando `oled_display_get_page()` como um simples
`return current_page;`.

Como `OLED_PAGE_MAX == 2` (`main/display/oled_display.h:30-33`:
`OLED_PAGE_SENSORS`, `OLED_PAGE_SYSTEM`), `oled_display_next_page()` já
alterna SENSORS ↔ SYSTEM — ou seja, apertar o botão uma vez entra na tela de
rede e ela **fica**; apertar de novo volta para os sensores. É exatamente o
comportamento pedido.

**Aceitação:** apertar o botão, esperar mais de 30 s e a tela de informações de
rede continuar na tela; apertar de novo e voltar para a tela de sensores.

---

### F5 — Incluir o MAC do aparelho na tela de informações de rede

**Arquivo:** `main/display/oled_display.c`, `oled_display_show_system()`
(linhas 435-491).

Layout atual das 8 linhas do OLED (21 caracteres por linha, `char line[22]`):

| Linha | Conteúdo atual |
|---|---|
| 0 | data/hora UTC |
| 1 | `IP:<ip>` |
| 2 | `Up:<uptime>` |
| 3 | nome do dispositivo |
| 4 | `LoRa:Joined` / `LoRa:No Join` |
| 5 | `Addr:<devaddr>` — só se `lora_joined` |
| 6 | `Up:<n> R:<rssi>dBm` — só se `lora_joined` |
| 7 | `SNR:<snr>dB` — só se `lora_joined` |

O MAC formatado (`AA:BB:CC:DD:EE:FF`) tem 17 caracteres e cabe em uma linha;
sem os dois-pontos, 12. O aperto é que, com LoRa conectado, as 8 linhas já
estão ocupadas. Escolha uma:

- **(a)** MAC na linha 5 e empurrar o bloco LoRa para 6-7, juntando
  `Addr`+`Up` numa linha e abrindo mão do `SNR` (ou juntando `R:`+`SNR:`).
- **(b)** MAC compacto na linha 3 junto do nome, ex. `ED015 DDEEFF`
  (últimos 3 bytes) — cabe nos 21 caracteres e não consome linha nova.
- **(c)** MAC completo na linha 5 e o bloco LoRa só quando `lora_joined`,
  aceitando que nesse caso o `SNR` não aparece.

**Recomendação:** (a) com MAC completo na linha 5, porque em fábrica o MAC é o
identificador que o operador cruza com a tabela do app PC — vale mais que o
`SNR`.

**Como obter o MAC:** mesmo caminho já usado no setup ESP-NOW
(`main/setup/espnow_setup.c:203-206`):

```c
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    /* formatar como AA:BB:CC:DD:EE:FF */
```

`oled_display_show_system()` não tem acesso ao MAC hoje — as duas opções são
buscá-lo dentro da função (precisa de `esp_wifi.h` no display, acopla display a
Wi-Fi) ou **passá-lo como parâmetro** a partir do laço de render em
`main/main.c:141-152` (`case OLED_PAGE_SYSTEM`), que já monta `ip`, `uptime_s`
e as stats de LoRa. **Prefira passar como parâmetro** — mantém o módulo de
display sem dependência de Wi-Fi.

**Aceitação:** na tela de informações de rede, ler o MAC do aparelho e conferir
que é igual ao MAC mostrado na coluna "MAC" do app PC para aquele aparelho.

---

## Parte 3 — Como testar

### Ambiente

- ESP-IDF **v5.5.3**: `. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh`
- **SKU:** o modo de setup de fábrica (`espnow_setup_run()`) só compila com
  `CONFIG_WIFI_ENABLED` (guarda em `main/main.c`). O SKU
  `jvtech_2mb_headless` **não** entra em modo de setup. Use
  `jvtech_4mb_standard` (tem Wi-Fi e OLED).
- Dongle: já flashado, em `/dev/ttyUSB0` (firmware em
  `donglesetup/dongle/`, sem alterações necessárias).
- App PC:
  ```bash
  cd /home/felipe/work/isi-latecme-quality-donglesetup/pc-app
  PYTHONPATH=src .venv/bin/python -m provisioner.app          # hardware real
  PYTHONPATH=src .venv/bin/python -m provisioner.app --demo   # sem hardware
  ```
- Procedimento de bancada completo (2 aparelhos):
  `donglesetup/docs/BENCH_TEST.md`.

### Invariantes que não podem quebrar

- `MAX_FRAME_LEN` (`espnow_setup.c:60`) e `MAX_SET_WIRE`
  (`pc-app/src/provisioner/session.py:18`) valem **1024** e devem continuar
  iguais nos dois lados.
- Chave de fábrica: placeholders em `shared/factory_key.h` / `factory_key.py`
  (16 bytes zerados). A chave real vai em
  `shared/factory_key.local.h` / `factory_key.local.py` (git-ignored) e tem que
  ser a mesma nos dois repos, senão o SET falha com `decrypt failed`.
- O aparelho ignora silenciosamente (sem ACK) um SET cujo `mac` do corpo
  decifrado não seja o seu (`espnow_setup.c:294-300`) — comportamento correto,
  não mexer.
- Toda resposta (`ANNOUNCE`/`READING`/`ACK`) depende do peer ESP-NOW de origem
  estar registrado antes de responder (`espnow_setup.c:425`) — não mexer.

### Ordem sugerida de implementação

1. **F1** (ANNOUNCE com `name`) — pequeno, destrava a coluna Nome.
2. **F3** — decidir Opção A ou B **antes** de codar, porque a A muda o app PC e
   não o firmware.
3. **F4** (remover timeout da tela) — isolado.
4. **F5** (MAC na tela) — depende de decidir o layout das linhas.
5. **F2** — endurecimentos opcionais + teste de não-regressão.

### Referências

- Contrato do protocolo: `donglesetup/shared/protocol.md`
- Spec da feature do app PC:
  `donglesetup/docs/superpowers/specs/2026-08-04-pc-offset-columns-design.md`
- Plano de implementação executado:
  `donglesetup/docs/superpowers/plans/2026-08-04-pc-offset-columns.md`
- PR do app PC:
  https://github.com/felipeosmar/isi-latecme-quality-donglesetup/pull/2
