# Design: Migração para Kconfig — Variantes Standard e Salt Spray

**Data:** 2026-04-07
**Status:** Aprovado

## Contexto

O projeto mantinha duas branches de produção (`main` e `salt_spray`) para duas variantes de firmware que diferem apenas pela presença do sensor termopar MAX6675. Essa abordagem gera divergência de código ao longo do tempo. Este documento especifica a migração para uma codebase unificada com seleção de variante via Kconfig.

## Objetivo

Unificar o código em uma única branch `main`, com duas variantes buildadas a partir do mesmo commit via opção Kconfig `CONFIG_THERMOCOUPLE_ENABLED`. Eliminar as branches `salt_spray` e `salt_spray_dev` após a migração.

---

## 1. Kconfig

Criar `main/Kconfig.projbuild` com:

```kconfig
menu "Firmware Variant"

config THERMOCOUPLE_ENABLED
    bool "Enable MAX6675 thermocouple sensor"
    default n
    help
        Enables the MAX6675 SPI thermocouple driver, thermocouple config
        fields, and thermocouple alarm thresholds. Disable for the standard
        (temperature + humidity only) variant.

endmenu
```

**`sdkconfig.defaults`** — variante standard (base, termopar desligado):
- Não precisa de entrada explícita para `CONFIG_THERMOCOUPLE_ENABLED`; o default `n` já cobre.

**`sdkconfig.defaults.salt_spray`** — variante salt spray:
```
CONFIG_THERMOCOUPLE_ENABLED=y
```

Comando de build para a variante salt spray:
```bash
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.salt_spray" build
```

---

## 2. Mudanças no código C

### Regra geral

`#ifdef CONFIG_THERMOCOUPLE_ENABLED` apenas onde hardware é tocado. Código de runtime que já verifica `thermocouple_valid` ou `!isnan(tc_temp)` não precisa de guard — se o driver não inicializa, os campos permanecem `false`/`NAN` e o comportamento é correto.

### `CMakeLists.txt`

```cmake
# Antes:
"sensors/max6675_driver.c"

# Depois:
if(CONFIG_THERMOCOUPLE_ENABLED)
    list(APPEND srcs "sensors/max6675_driver.c")
endif()
```

### `sensor_manager.c`

Envolver em `#ifdef CONFIG_THERMOCOUPLE_ENABLED`:
- `#include "max6675_driver.h"`
- Bloco de inicialização do MAX6675 (init dos GPIOs e chamada a `max6675_init`)
- Bloco de leitura (`max6675_read`, store em `data.thermocouple_temp/valid`)

Quando desabilitado, os campos `thermocouple_temp` e `thermocouple_valid` do struct permanecem no valor inicial (`NAN` / `false`) — sem guard no struct.

### `config_manager.c` / `config_manager.h`

Envolver em `#ifdef CONFIG_THERMOCOUPLE_ENABLED`:
- Campos do struct `config_t`: `thermocouple_enabled`, `thermocouple_max_temp`, `thermocouple_sck_pin`, `thermocouple_so_pin`, `thermocouple_cs_pin`, `thermocouple_min_temp`, `thermocouple_correction`
- Campos de alarme: `alarm_tc_enabled`, `alarm_tc_low`, `alarm_tc_high`
- Defaults, serialização JSON (load/save), e todas as funções getter/setter correspondentes

### `api_sensors.c`

Envolver em `#ifdef CONFIG_THERMOCOUPLE_ENABLED`:
- Campos de status na resposta GET (`thermocouple_valid`, `thermocouple_temp`)
- Campos de config na resposta GET e no parser POST (pinos, thresholds, alarm)

Adicionar campo `"thermocouple_hw_enabled": true/false` **fora** do guard na resposta GET, para a UI saber se deve exibir o painel de configuração do termopar.

### Arquivos sem mudança necessária

| Arquivo | Razão |
|---|---|
| `oled_display.c` | Já checa `!isnan(tc_temp)` |
| `main.c` (uplink) | Já checa `data.thermocouple_valid` |
| `alarm_manager.c` | Já checa `data->thermocouple_valid && config_get_alarm_tc_enabled()` |
| `sensor_data_t` | Mantém os campos; ficam em `false`/`NAN` quando driver não inicializa |

---

## 3. UI Web

A UI (`www.bin`) é **idêntica** para as duas variantes. O card do termopar em `sensors.html` permanece; a ausência de dados faz o card mostrar `--`. O painel de configuração de pinos/thresholds do termopar em `config.html` é exibido ou ocultado dinamicamente com base no campo `thermocouple_hw_enabled` retornado pela API.

---

## 4. Pipeline CI/CD

### `ci.yml` — dispara em PR para `main`

Jobs:
1. **`validate-source`**: verifica `github.head_ref == 'main_dev'`; falha com mensagem clara se não for.
2. **`build-standard`**: build com `sdkconfig.defaults` padrão; faz upload de artifacts (retidos 7 dias).
3. **`build-salt_spray`**: build com `sdkconfig.defaults.salt_spray`; faz upload de artifacts separados.

Jobs 2 e 3 dependem do job 1 passar.

### `release.yml` — dispara em push para `main` (merge do PR)

Jobs:
1. **`build-standard`** e **`build-salt_spray`** em paralelo.
2. **`release`**: cria uma única GitHub Release com os seguintes artefatos nomeados:
   - `lorawan-enddevice-rN-standard.bin`
   - `lorawan-enddevice-rN-salt_spray.bin`
   - `www-rN.bin` (único, compartilhado)
   - `bootloader-rN.bin`
   - `partition-table-rN.bin`

Tag de release: `rN` (número sequencial, sempre stable — nunca pré-release neste workflow).

### Branch protection em `main` (configurar via `gh` CLI)

- Push direto bloqueado
- PR obrigatório
- Required status checks: `validate-source`, `build-standard`, `build-salt_spray`

---

## 5. Migração das branches

1. O código atual de `salt_spray` vira a base unificada do `main`.
2. Adicionar guards `#ifdef CONFIG_THERMOCOUPLE_ENABLED` conforme seção 2.
3. Verificar build das duas variantes localmente.
4. Fazer um único commit de migração em `main_dev` e abrir PR.
5. Após merge e CI verde, arquivar as branches `salt_spray` e `salt_spray_dev` no GitHub.

---

## Fora de escopo

- Migração de configurações salvas no flash do dispositivo (userdata partition) — os campos de config do termopar que somem do `config_manager` não causam problemas: o JSON de config existente simplesmente ignora campos desconhecidos no load.
- Mudanças no CayenneLPP payload — o canal 4 já é condicional em runtime via `thermocouple_valid`.
