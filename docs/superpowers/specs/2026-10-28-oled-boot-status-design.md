# OLED como painel de status do boot — Design

**Data:** 2026-10-28
**Branch:** feat/espnow-setup-mode
**Status:** Revisado, aguardando aprovação

---

## Intenção

Hoje, para saber o que um dispositivo está fazendo ao ligar, ou onde ele parou, é preciso abrir o monitor serial. O objetivo é que o OLED responda a isso sozinho, tanto na bancada quanto em campo:

1. **O dispositivo está configurado ou em modo setup?**
2. **Em que etapa do boot ele está, e qual etapa falhou?**
3. **Qual é o status do WiFi e da LoRa?**

Além disso, **temperatura e umidade devem aparecer na tela sempre**, mesmo que o WiFi ou a LoRa não tenham inicializado.

## Objetivos

- O OLED é inicializado logo no início do `app_main`, antes de config, WiFi e LoRa.
- Durante o boot, o OLED mostra uma **lista de etapas** (estilo log) com o estado de cada uma.
- No modo setup ESP-NOW, a tela SETUP ganha uma **linha de estado** com o ponto em que o dispositivo está parado.
- Na operação normal, a tela de sensores mostra **o status de WiFi e LoRa nas linhas de rótulo**, sem reduzir os valores.
- A falha de WiFi, web server ou LoRa **não impede** que as tasks de sensor e display rodem.

## Não-objetivos

- Não mudar a página de sistema, a tela de alarme nem a navegação por botão.
- Não alterar o SKU headless (sem OLED). As etapas continuam só no log serial.
- Não mudar o protocolo ESP-NOW, o join LoRaWAN nem a lógica do auto-update.

## Anti-objetivos (seria falha mesmo funcionando)

- Um dispositivo com sensor funcionando e tela apagada ou travada numa tela de boot, porque WiFi ou LoRa falharam.
- Os valores de temperatura e umidade ficarem menores ou menos legíveis do que hoje na tela de sensores.
- O boot ficar perceptivelmente mais lento só para a tela ser legível. Pausas artificiais longas não são aceitas.

---

## Comportamento decidido

### 1. Tela de boot: lista de etapas (decidido pelo usuário: opção A)

Layout em 8 linhas de 21 caracteres (fonte 6×8):

```
 BOOT          <ver>      ← linha 0 invertida; versão de esp_app_get_description()
ok Config
ok Sensor SHT3x
ER WiFi STA
.. WiFi AP
   LORA-3F2A              ← linha de detalhe (indentada) abaixo da etapa
```

- Prefixos: `ok` (concluída), `..` (em andamento), `ER` (falhou).
- Uma etapa pode ter **uma linha de detalhe** opcional: nome da rede, SSID do AP, IP, nome do sensor detectado, motivo do erro.
- Quando a lista passa das 7 linhas disponíveis, ela **rola**: as etapas mais antigas saem por cima e a etapa atual fica sempre visível.
- Uma etapa com `ER` **não para o boot** (exceção: ver "Falha de config" abaixo). O boot segue para a próxima etapa.

Etapas, na ordem do `app_main` (os nomes exatos e o texto do detalhe ficam com o builder, respeitando 21 colunas):

| Etapa | Detalhe típico | Em falha |
|---|---|---|
| Display | — | sem tela, o boot segue (só log serial) |
| Config | — | ver "Falha de config" |
| Sensor | modelo detectado (`SHT3x`, `SHT20`, `AM2315C`, `nenhum`) e `TC` se houver termopar | `ER`, segue |
| Modo SETUP | motivo: `nao provisionado` ou `botao` | — (troca para a tela SETUP) |
| WiFi STA | SSID | `ER`, cai para WiFi AP |
| WiFi AP | SSID do AP | `ER`, segue sem web |
| Web | IP | `ER`, segue |
| Tasks | — | — |

Ao terminar a etapa "Tasks", a tela de boot **dá lugar à tela de sensores** (operação normal). O join LoRa acontece depois disso e aparece na tela de sensores, não na lista de boot.

### 2. Modo setup: linha de estado (aprovado pelo usuário)

Mantém a tela SETUP que já existe na branch (título 2x, MAC, tabela Lido/Ofs). A **linha 3** (página 3, hoje vazia) passa a mostrar o estado em vídeo invertido:

| Estado | Quando |
|---|---|
| `Aguardando dongle` | entrou no setup e ainda não recebeu nenhum frame |
| `Dongle conectado` | recebeu DISCOVER ou READ |
| `Identificando` | recebeu IDENTIFY endereçado a ele |
| `Config OK, falta CMT` | SET aplicado e salvo com sucesso |
| `ERRO ao salvar cfg` | `config_save()` falhou num SET ou COMMIT |
| `Gravado! Reiniciando` | COMMIT ok, antes do `esp_restart()` |

**Prioridade entre estados (decidido pelo usuário).** O app do dongle manda READ e DISCOVER em broadcast, e o fluxo normal é ler → SET → ler de novo para conferir → COMMIT. Para a leitura de conferência não apagar a informação útil:

- DISCOVER/READ só levam de `Aguardando dongle` para `Dongle conectado`. Em qualquer outro estado, não mudam nada.
- `Config OK, falta CMT` e `ERRO ao salvar cfg` só mudam com um novo SET ou COMMIT.
- `Identificando` fica ~3 s na tela e depois volta ao estado anterior.
- SET, IDENTIFY e COMMIT endereçados a outro MAC não mudam o estado.

O motivo da entrada no setup (`nao provisionado` / `botao`) aparece na lista de boot, antes de a tela SETUP assumir.

**Falha do ESP-NOW (decisão minha).** Hoje `wifi_espnow_init()` usa `ESP_ERROR_CHECK`, e uma falha de NVS, WiFi ou ESP-NOW gera panic e loop de reboot. Em vez disso, a etapa mostra `ER ESP-NOW` com o motivo na linha de detalhe e o dispositivo **fica parado nessa tela, sem reiniciar**.

### 3. Operação normal: status nas linhas de rótulo (decidido pelo usuário: opção A)

A tela de sensores continua igual (rótulo na linha 0, valor 3x nas linhas 1–3, rótulo na linha 4, valor 3x nas linhas 5–7). O status vai **alinhado à direita** nas linhas de rótulo:

```
Temp       WiFi:STA
   23.4C   (3x)
Hum      LoRa:join3
   61.2%   (3x)
```

- Linha 0: status do WiFi. Linha 4: status da LoRa.
- No SKU com termopar, os rótulos são `Int.` e `Ext.`, e o status vai nas mesmas linhas.
- O status cabe em 21 − (rótulo + 1 espaço) colunas.

Vocabulário (sugerido por mim e aceito pelo usuário na conversa):

| Fonte | Texto | Quando |
|---|---|---|
| WiFi | `WiFi:...` | conectando |
| WiFi | `WiFi:STA` | conectado como estação |
| WiFi | `WiFi:AP` | em modo AP |
| WiFi | `WiFi:FALHA` | `init_wifi` falhou ou status de erro |
| LoRa | `LoRa:ERRO` | o rádio SX1276 não inicializou (`lorawan_init` falhou) |
| LoRa | `LoRa:semcred` | DevEUI/JoinEUI/AppKey ausentes ou inválidos |
| LoRa | `LoRa:joinN` | tentando join; N é o número da tentativa atual |
| LoRa | `LoRa:OK` | joined |
| LoRa | `LoRa:...` | antes de a task LoRa começar (primeiros ~3 s) |

A tela de alarme continua com prioridade sobre a de sensores, como hoje.

---

## Mudanças de comportamento no boot

1. **OLED antes de tudo.** O barramento I2C (SDA 21 / SCL 22) é criado logo após `buzzer_init()`, e o OLED é inicializado nele. Hoje o barramento só nasce dentro de `sensor_manager_init()`, que lê pinos do termopar no config. Por isso a **criação do barramento precisa ser separada** da detecção de sensores, para o OLED subir antes do `config_init()`. `sensor_manager_init()` passa a reutilizar esse barramento.
2. **Falhas não abortam o boot.** Hoje `app_main` faz `return` se `init_wifi()` ou `web_server_init()` falham, e nenhuma task é criada. Esses `return` viram `ER` na lista e o boot segue até "Tasks". As tasks de sensor, display e alarme são sempre criadas.
3. **O OLED é inicializado uma única vez.** `display_task` e `espnow_setup_run()` deixam de chamar `oled_display_init()`. Hoje cada um chama por conta própria, e uma segunda chamada adicionaria o dispositivo de novo no barramento. Se o OLED não respondeu no início, as telas viram no-op, como já acontece com `oled_initialized == false`.
4. **`display_task` sem a espera de 5 s.** Hoje ela espera 5 s antes de inicializar o OLED. Como o OLED já está pronto, ela pode desenhar imediatamente. Até a primeira leitura, os valores aparecem como `--`.

### Falha de config

Se `config_init()` falhar (LittleFS do `userdata` não monta), a etapa mostra `ER Config` e o boot **segue com os padrões em memória** (`config_reset_defaults()`). Isso foi decidido pelo usuário.

- **Pula o modo setup.** Os padrões têm `factory_provisioned = false`, mas setup sem flash gravável só produziria `ERRO ao salvar cfg`. Então, com a config falhada, a decisão de setup é ignorada, inclusive com o botão pressionado.
- O resto do boot segue normalmente, e a tela de sensores aparece. Com os padrões não haverá credenciais WiFi nem LoRa, então o normal é ver `WiFi:AP` e `LoRa:semcred`.
- O builder precisa confirmar que `config_init()` deixa os getters utilizáveis (padrões carregados, mutex criado) mesmo quando a montagem falha. Se hoje não deixa, isso deve ser ajustado.

---

## Estado que passa a ser exposto

- **Etapas do boot:** um módulo pequeno que guarda a lista de etapas (nome, estado, detalhe) com proteção por mutex e desenha a lista no OLED a cada mudança. Ele também escreve a etapa no log serial, para o SKU headless ter a mesma informação.
- **Status da LoRa:** `lorawan_handler` passa a expor um estado (`não iniciado / erro de rádio / sem credenciais / joining(tentativa) / joined`) para a `display_task` ler. O `lorawan_stats_t` já tem `joined` e `join_attempts`, mas não distingue "sem credenciais" de "erro de rádio".
- **Status do WiFi:** já existe em `wifi_manager_get_status()` e pode ser usado diretamente.
- **Estado do setup:** `espnow_setup.c` guarda o estado atual e o passa para `oled_display_show_setup()`.

## Restrições

- ESP-IDF v5.5.3. O código deve seguir o estilo atual (C, `ESP_LOG*`, `#if CONFIG_OLED_ENABLED` / `CONFIG_WIFI_ENABLED`, stubs `static inline` no `.h` quando o OLED está desligado).
- Os quatro SKUs (`./build.sh build <sku>`) devem compilar. O `jvtech_2mb_headless` não tem OLED nem WiFi.
- O display tem 128×64, ou seja, 21 colunas × 8 linhas na fonte pequena. Nenhum texto pode passar de 21 colunas.
- O barramento I2C é compartilhado entre sensores e OLED. O OLED passa a ser desenhado por mais de um contexto (`app_main` durante o boot, `espnow_setup_run`, `display_task`, `button_task` na barra de factory reset), então o acesso ao framebuffer precisa ser serializado.
- O projeto não tem suíte de testes. A verificação é por build dos 4 SKUs e teste na placa.
- Partem do trabalho não commitado da branch (`oled_display_show_setup`, `setup_display_refresh`), que já está nos arquivos.

## Deixado para o builder

- Nome e API do módulo de etapas do boot e onde ele mora (`main/display/` ou `main/interface/`).
- O texto exato de cada etapa e detalhe, dentro de 21 colunas.
- Se a tela de boot fica ~1 s visível depois de "Tasks" antes de trocar para sensores (sem ultrapassar isso).
- Como serializar o acesso ao OLED (mutex dentro do `oled_display.c` ou só no chamador).
- Onde a detecção de sensores roda depois que o barramento I2C for separado.
- Como tratar `WIFI_STATUS_DISCONNECTED` no vocabulário (sugestão: `WiFi:...`).
- Credenciais LoRa com hex inválido contam como `LoRa:semcred`.
- Como truncar a versão no cabeçalho para caber em 21 colunas.

## Decisões tomadas por mim (para conferir)

- Vocabulário do status WiFi/LoRa (tabela da seção 3). O usuário viu a proposta e aceitou.
- O join LoRa aparece na tela de sensores, não na lista de boot. A lista termina em "Tasks".
- `display_task` sem a espera de 5 s.
- Falha do ESP-NOW no setup: a tela fica parada em `ER ESP-NOW`, sem loop de reboot.
- A duração de `Identificando` na linha de estado (~3 s).
