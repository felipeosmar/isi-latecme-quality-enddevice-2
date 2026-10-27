# Compilando as Variações de Firmware

Este documento descreve como compilar cada variação (SKU) do firmware para os diferentes hardwares suportados.

## Pré-requisitos

- ESP-IDF v5.5.3 instalado em `/home/felipe/.espressif/v5.5.3/esp-idf/`
- Ambiente configurado:

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh
```

---

## SKUs disponíveis

| SKU | Flash | OLED | WiFi | Termopar | OTA | Uso |
|---|---|---|---|---|---|---|
| `jvtech_4mb_standard` | 4MB | Sim | Sim | Não | Sim | Padrão — placa JVtech v1.2 |
| `jvtech_4mb_thermocouple` | 4MB | Sim | Sim | Sim (MAX6675) | Sim | JVtech v1.2 + termopar |
| `jvtech_2mb_standard` | 2MB | Sim | Sim | Não | Não | JVtech v1.2 com flash reduzido |
| `jvtech_2mb_headless` | 2MB | Não | Não | Não | Não | Versão mínima (sem display, sem WiFi) |

---

## Como compilar

### Passo 1 — Listar SKUs disponíveis

```bash
./build.sh skus
```

### Passo 2 — Compilar um SKU específico

```bash
./build.sh build <SKU>
```

**Exemplos:**

```bash
./build.sh build                          # compila jvtech_4mb_standard (padrão)
./build.sh build jvtech_4mb_thermocouple
./build.sh build jvtech_2mb_standard
./build.sh build jvtech_2mb_headless
```

O comando automaticamente:
1. Remove o `sdkconfig` anterior
2. Aplica as camadas de configuração corretas para o SKU
3. Compila o firmware

Os binários gerados ficam em `build/`:
- `build/lorawan-enddevice.bin` — firmware principal
- `build/www.bin` — interface web (partição www)
- `build/bootloader/bootloader.bin` — bootloader
- `build/partition_table/partition-table.bin` — tabela de partições

---

## Como gravar (flash)

Após compilar, use os comandos de flash do `build.sh`:

```bash
./build.sh update    # firmware + interface web (preserva config do usuário) ← mais comum
./build.sh app       # somente firmware
./build.sh www       # somente interface web
./build.sh all       # flash completo (apaga config do usuário — factory reset)
```

> O offset da partição `www` é detectado automaticamente para cada SKU (4MB → `0x370000`, 2MB → `0x180000`).

**Porta e baud personalizados:**

```bash
PORT=/dev/ttyACM0 ./build.sh update
PORT=/dev/ttyUSB1 BAUD=115200 ./build.sh all
```

---

## Compilação manual (sem build.sh)

Se preferir invocar o ESP-IDF diretamente:

```bash
# Sempre delete o sdkconfig antes de trocar de SKU
rm -f sdkconfig

# jvtech_4mb_standard
idf.py build

# jvtech_4mb_thermocouple
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.thermocouple" build

# jvtech_2mb_standard
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.2mb" build

# jvtech_2mb_headless
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.2mb;sdkconfig.defaults.no_oled;sdkconfig.defaults.no_wifi" build
```

> **Importante:** `sdkconfig.defaults` só é aplicado quando o arquivo `sdkconfig` não existe.
> `idf.py fullclean` remove apenas `build/`, **não** o `sdkconfig`. Delete manualmente.

---

## Como adicionar um novo SKU

1. **Crie os arquivos de feature** necessários (se a combinação usar features não existentes):

   ```bash
   # Exemplo: novo arquivo de feature
   echo "CONFIG_MINHA_FEATURE=y" > sdkconfig.defaults.minha_feature
   ```

2. **Adicione uma entrada no manifesto** de `build.sh`:

   ```bash
   # Edite build.sh e adicione na seção SKU_DEFAULTS:
   [meu_novo_sku]="sdkconfig.defaults;sdkconfig.defaults.minha_feature"
   ```

3. **Adicione o SKU no CI** (`.github/workflows/release.yml`), na seção `matrix.sku`:

   ```yaml
   - meu_novo_sku
   ```

4. **Adicione o binário** na lista `files:` do passo "Create GitHub Release" no mesmo arquivo.

---

## Estrutura dos arquivos de configuração

```
sdkconfig.defaults              # base comum a todos os SKUs
sdkconfig.defaults.thermocouple # habilita MAX6675 (CONFIG_THERMOCOUPLE_ENABLED=y)
sdkconfig.defaults.2mb          # flash 2MB: partição factory, sem OTA rollback
sdkconfig.defaults.no_oled      # desabilita display (CONFIG_OLED_ENABLED=n)
sdkconfig.defaults.no_wifi      # desabilita WiFi + stack ESP (CONFIG_WIFI_ENABLED=n)
partitions.csv                  # tabela de partições 4MB com dual OTA
partitions.2mb.csv              # tabela de partições 2MB com factory única
```

---

## Monitorar porta serial

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Sair: `Ctrl+]`
