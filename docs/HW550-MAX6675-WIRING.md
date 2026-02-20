# HW-550 (MAX6675) — Instruções de Conexão com Placa JVTECH v1.2

## Visão Geral

Módulo **HW-550** = MAX6675 + Termopar Tipo K  
- Interface: SPI (read-only, 3 fios: SCK, SO, CS)  
- Range: 0°C a 1024°C (resolução 0.25°C)  
- Uso neste projeto: medição de temperatura 0–200°C  

## Mapa de GPIOs Ocupados na Placa JVTECH v1.2

Baseado no esquemático `Schematic_SPECIAL-LINE-LA-T-H-JVTECH-v1.2_2026-01-13.pdf`:

| GPIO | Uso na placa | Disponível? |
|------|-------------|-------------|
| 0 | Header H2 (boot strap — GPIO0) | ❌ Evitar |
| 1 | UART0 TXD (console/programação) | ❌ |
| 2 | Header H2 (boot strap — LED interno) | ❌ Evitar |
| 3 | UART0 RXD (console/programação) | ❌ |
| 4 | Buzzer (BC817 via R5 1k) | ❌ |
| 5 | SX1276 NSS (CS LoRa) | ❌ |
| 12 | Livre no MIJ (boot strap — evitar) | ⚠️ |
| 13 | SX1276 DIO1 | ❌ |
| 14 | SX1276 RST | ❌ |
| 15 | Livre no MIJ (boot strap — evitar) | ⚠️ |
| 16 | Livre (RXD2) | ✅ |
| 17 | Livre (TXD2) | ✅ |
| 18 | SX1276 SCLK (SPI) | ❌ |
| 19 | SX1276 MISO (SPI) | ❌ |
| 21 | I2C SDA → Sensor I2C (U3) + Display OLED (U4) | ❌ |
| 22 | I2C SCL → Sensor I2C (U3) + Display OLED (U4) | ❌ |
| 23 | SX1276 MOSI (SPI) | ❌ |
| 25 | Botão (TC-1109DE, pull-up R4 10k) | ❌ |
| 26 | SX1276 DIO0 | ❌ |
| 27 | Livre | ✅ |
| 32 | Livre | ✅ |
| 33 | Livre | ✅ |
| 34 | Livre (input only) | ✅ (só input) |
| 35 | Livre (input only) | ✅ (só input) |
| 36 | Livre (input only) | ✅ (só input) |
| 39 | Livre (input only) | ✅ (só input) |

## Pinagem Escolhida para o HW-550

O MAX6675 usa SPI simplificado (somente leitura). Como o SPI principal (HSPI) já está
dedicado ao SX1276 (LoRa), usaremos **SPI por software (bit-bang)** em GPIOs livres.

| Pino HW-550 | Função | GPIO ESP32 | Pino MIJ | Justificativa |
|-------------|--------|-----------|----------|---------------|
| **VCC** | Alimentação 3.3V | 3V3 (ESP32) | Pino 2, 21, 22 ou 32 | 3.3V direto do MIJ |
| **GND** | Terra | GND | Pino 1, 15, 17 ou 18 | Qualquer GND |
| **SCK** | SPI Clock | **GPIO 16** | Pino 27 (RXD2) | Livre, I/O, sem conflito |
| **SO** | SPI Data Out (MISO) | **GPIO 36** | Pino 4 (VP, input-only) | Input-only — OK para SO |
| **CS** | Chip Select | **GPIO 17** | Pino 28 (TXD2) | Livre, I/O, sem conflito |

### Por que esses GPIOs?

- **GPIO 33, 27, 32** — Todos livres, sem função na placa v1.2, sem restrições de boot
- **GPIO 27** como SO (MISO) — Data out do MAX6675, precisa ser I/O (leitura)
- **GPIO 33** como SCK — Clock gerado pelo ESP32, precisa ser output
- **GPIO 32** como CS — Chip select, precisa ser output
- Nenhum conflita com LoRa (SPI HSPI), I2C (sensores/OLED), UART0, buzzer ou botão

### Alternativas (se algum pino estiver fisicamente inacessível)

| Função | Alternativa 1 | Alternativa 2 |
|--------|--------------|--------------|
| SCK | GPIO 16 (pino 27) | GPIO 17 (pino 28) |
| SO | GPIO 34 (pino 6, input-only — OK pra SO) | GPIO 36 (pino 4, input-only) |
| CS | GPIO 16 (pino 27) | GPIO 17 (pino 28) |

## Diagrama de Conexão

```
    Placa JVTECH v1.2 (MIJ)              HW-550 (MAX6675)
    ┌─────────────────────┐              ┌──────────────┐
    │                     │              │              │
    │  3V3 (Pino 2/21)  ─┼──────────────┼─ VCC         │
    │                     │              │              │
    │  GND (Pino 1/15)  ─┼──────────────┼─ GND         │
    │                     │              │              │
    │  GPIO33 (Pino 9)  ─┼──────────────┼─ SCK         │
    │                     │              │              │     ┌─────────────┐
    │  GPIO27 (Pino 12) ─┼──────────────┼─ SO          │     │ Termopar    │
    │                     │              │          T+  ┼─────┤ Tipo K      │
    │  GPIO32 (Pino 8)  ─┼──────────────┼─ CS      T-  ┼─────┤ 0-200°C     │
    │                     │              │              │     └─────────────┘
    └─────────────────────┘              └──────────────┘
```

## Onde Soldar na Placa

Os pinos do MIJ são pads de 1.27mm de espaçamento. Referência:

1. **GPIO 33 (SCK)** → Pino 9 do MIJ (lado esquerdo, fileira superior)
2. **GPIO 27 (SO)** → Pino 12 do MIJ (lado esquerdo, fileira superior)
3. **GPIO 32 (CS)** → Pino 8 do MIJ (lado esquerdo, fileira superior)
4. **3V3** → Pino 2 ou 21/22/32 do MIJ (alimentação ESP32)
5. **GND** → Pino 1, 15, 17 ou 18 do MIJ

### Dica de soldagem

- Use fio 30 AWG (wire-wrap) para conectar os pads do MIJ ao módulo HW-550
- Os pads são de 1.27mm — ponta de ferro fina e flux ajudam muito
- Alternativamente, se a placa base tem headers de 2.54mm expostos (H1/H2), verifique se GPIO 27/32/33 estão acessíveis por ali

### Headers H1 e H2 (da placa base, conforme esquemático)

**H1** (HDR-F-2.54 1x10):
- Pino 4 = **GPIO25** (ocupado — botão)
- Pino 10 = GND
- Pino 1 = 3V3
- Demais: não mapeados no esquemático (podem ser livres)

**H2** (HDR-F-2.54 1x10):
- Pino 2 = GPIO02
- Pino 3 = GPIO00
- Pino 4 = GPIO04 (ocupado — buzzer)
- Pino 5 = 3V3
- Pino 6 = GPIO21 (ocupado — I2C SDA)
- Pino 7 = RXD0
- Pino 8 = TXD0
- Pino 9 = GPIO22 (ocupado — I2C SCL)
- Pino 10 = GND

**Header RECORD** (HDR-M-2.54 1x6):
- 3V3, EN, GND, GPIO00, RXD0, TXD0

> ⚠️ GPIO 27, 32 e 33 **NÃO estão expostos em nenhum header da placa base**.
> Será necessário soldar diretamente nos pads do módulo MIJ.

## Protocolo SPI do MAX6675

O MAX6675 tem um protocolo SPI simplificado (somente leitura):

1. Puxar CS para LOW
2. Gerar 16 pulsos de clock em SCK
3. Ler 16 bits em SO (MSB first)
4. Puxar CS de volta para HIGH

```
Bit 15: Dummy (sempre 0)
Bit 14-3: Temperatura (12 bits, 0.25°C/LSB)
Bit 2: Open thermocouple flag (1 = termopar desconectado)
Bit 1: Device ID (0)
Bit 0: Three-state
```

Temperatura = (bits 14-3) × 0.25 °C

### Timing

- Tempo mínimo entre leituras: **220ms** (conversão interna)
- Clock máximo: 4.3 MHz (bit-bang será bem abaixo disso — sem problemas)

## Configuração no Firmware

No `config.json`, será adicionada a seção:

```json
{
  "sensors": {
    "thermocouple_enabled": true,
    "thermocouple_max_temp": 200.0,
    "thermocouple_sck_pin": 33,
    "thermocouple_so_pin": 27,
    "thermocouple_cs_pin": 32
  }
}
```

## Checklist de Montagem

- [ ] Soldar fio no pad GPIO 33 (pino 9 MIJ) → SCK do HW-550
- [ ] Soldar fio no pad GPIO 27 (pino 12 MIJ) → SO do HW-550
- [ ] Soldar fio no pad GPIO 32 (pino 8 MIJ) → CS do HW-550
- [ ] Conectar 3V3 do MIJ → VCC do HW-550
- [ ] Conectar GND do MIJ → GND do HW-550
- [ ] Conectar termopar tipo K nos terminais T+/T- do HW-550
- [ ] Verificar continuidade com multímetro antes de energizar
- [ ] Testar com firmware atualizado
