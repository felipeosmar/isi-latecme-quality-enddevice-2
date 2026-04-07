# GitHub Auto-Update Design

**Date:** 2026-04-07  
**Branch:** salt_spray_dev  
**Feature:** Verificação diária de releases no GitHub e auto-atualização OTA

---

## Overview

O dispositivo verifica uma vez por dia se existe uma nova release no GitHub. Se existir, baixa e aplica automaticamente — firmware e partição www — mas somente quando conectado em modo STA (WiFi cliente), nunca em modo AP.

---

## Architecture

Novo módulo `main/update/auto_updater.c` + `auto_updater.h` com uma task FreeRTOS dedicada criada em `main.c` após a inicialização do WiFi.

```
main.c → app_main()
  └── auto_updater_init()   ← cria a task

auto_updater_task (Core 0, Pri 3, Stack 8KB)
  └── loop:
        sleep 24h
        WiFi STA connected?
          └── yes → github_check_and_update()
                      ├── HTTP GET GitHub Releases API
                      ├── parse latest tag for configured branch
                      ├── compare N with stored tag N
                      └── if newer → OTA firmware → save tag → reboot
                                     (www atualizado no próximo ciclo)
```

---

## Config Fields

Quatro novos campos adicionados a `config_t` em `config_manager.c`:

| Campo | Tipo | Default | Descrição |
|---|---|---|---|
| `auto_update_enabled` | `bool` | `false` | Habilita/desabilita a verificação automática |
| `auto_update_branch` | `char[32]` | `"main"` | Branch do GitHub a acompanhar |
| `auto_update_firmware_tag` | `char[32]` | `""` | Tag do firmware atualmente instalado (ex: `"main-r5"`) |
| `auto_update_www_tag` | `char[32]` | `""` | Tag do www atualmente instalado |

Tags separados para firmware e www permitem que cada partição seja atualizada de forma independente e que uma atualização pendente do www sobreviva a reboots.

---

## API Endpoint

Novo endpoint adicionado em `api_ota.c`:

- `GET /api/ota/auto-update` — retorna: `enabled`, `branch`, `firmware_tag`, `www_tag`, `last_check_time` (Unix timestamp, 0 se nunca verificou), `last_check_result` (`"never"` / `"up_to_date"` / `"updated"` / `"error"`)
- `POST /api/ota/auto-update` — aceita `enabled` (bool) e `branch` (string); `firmware_tag` e `www_tag` são gerenciados internamente

---

## GitHub API

**Endpoint:** `GET https://api.github.com/repos/felipeosmar/isi-latecme-quality-enddevice-2/releases`

**Filtragem:** tags com prefixo `{branch}-r` (ex: `salt_spray-r`). Extrai o maior N do campo `tag_name`.

**URLs dos assets:**
```
https://github.com/felipeosmar/isi-latecme-quality-enddevice-2/releases/download/{tag}/lorawan-enddevice-{tag}.bin
https://github.com/felipeosmar/isi-latecme-quality-enddevice-2/releases/download/{tag}/www-{tag}.bin
```

**HTTPS:** usa `esp_crt_bundle_attach` (bundle Mozilla do ESP-IDF) — sem `skip_cert_common_name_check`.

---

## Update Flow

### Verificação diária
1. Task acorda após 24h de sleep
2. Verifica se WiFi está em modo STA e conectado — se não, volta a dormir
3. `auto_update_enabled == false` → volta a dormir
4. Consulta GitHub API, filtra releases do branch configurado
5. Encontra tag com maior N

### Primeiro boot sem tag armazenado
- Armazena o tag mais recente sem atualizar
- Assume que o firmware atual já corresponde à release mais recente

### Lógica de decisão por ciclo
Em cada ciclo de 24h, o dispositivo executa em ordem:

1. Busca o tag mais recente do branch configurado via GitHub API
2. Se `firmware_tag != latest_tag` → atualiza firmware:
   - Flash com `esp_ota_begin/write/end/set_boot_partition`
   - Salva `firmware_tag = latest_tag` no config **antes** de reiniciar
   - `esp_restart()`
3. Se `firmware_tag == latest_tag` e `www_tag != latest_tag` → atualiza www:
   - Desmonta LittleFS www, apaga, escreve novo binário
   - Salva `www_tag = latest_tag` no config **antes** de reiniciar
   - `esp_restart()`
4. Se ambos iguais ao latest → nada a fazer

> **Por que dois reboots separados?**  
> O flash do www desmonta o LittleFS (onde o config é salvo). Gravar o tag antes de reiniciar e só então atualizar o www no próximo ciclo garante que o registro da versão nunca se perde, mesmo se o dispositivo perder energia durante a operação.

---

## Error Handling

| Situação | Comportamento |
|---|---|
| WiFi desconectado / modo AP | Pula silenciosamente, tenta no próximo ciclo de 24h |
| GitHub API inacessível / timeout | Log warning, `last_tag` não alterado, tenta no próximo ciclo |
| Download corrompido (`esp_ota_end` falha) | `esp_ota_abort`, tag não salvo, tenta novamente no próximo ciclo |
| Firmware inválido após reboot | Rollback automático pelo mecanismo do ESP-IDF (já habilitado) |
| `auto_update_enabled = false` | Task dorme indefinidamente, nenhuma ação de rede |
| Sem releases para o branch configurado | Log info, nenhuma ação |

---

## Files to Create/Modify

| Arquivo | Ação |
|---|---|
| `main/update/auto_updater.c` | Criar — lógica principal da task |
| `main/update/auto_updater.h` | Criar — API pública |
| `main/CMakeLists.txt` | Modificar — adicionar `update/auto_updater.c` ao SRCS |
| `main/config/config_manager.c` | Modificar — 3 novos campos + getters/setters |
| `main/config/config_manager.h` | Modificar — declarar getters/setters |
| `main/webserver/handlers/api_ota.c` | Modificar — 2 novos handlers para `/api/ota/auto-update` |
| `main/webserver/handlers/handlers.h` | Modificar — declarar novos handlers |
| `main/webserver/web_server.c` | Modificar — registrar novos endpoints |
| `main/main.c` | Modificar — chamar `auto_updater_init()` |
