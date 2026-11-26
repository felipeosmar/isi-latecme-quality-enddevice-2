# Alarmes sincronizados com a plataforma

Este documento descreve o contrato entre o firmware e a plataforma para os limites de alarme (downlink) e para o evento de alarme (uplink), o roteiro de verificação em bancada e os resultados registrados.

## 1. Visão geral

- A plataforma envia os limites de alarme por **downlink na fPort 11** (18 bytes).
- O dispositivo valida, grava na configuração (LittleFS) e responde com um **evento na fPort 3** (10 bytes, não confirmado) que informa a versão aplicada, o estado de cada canal, o IP local e um código de erro.
- Também é possível aplicar os mesmos limites pelo Wi-Fi, abrindo `limites.html` com um fragmento de URL (seção 5).
- A lógica local do alarme (latch, ACK de todos os canais, sem histerese) não muda.
- Canais, nesta ordem em todos os formatos: 0 = temperatura, 1 = umidade, 2 = termopar.
- Valores sempre em **décimos** (0,1 °C ou 0,1 %UR).

Código: `main/alarm/` (codec puro em `alarm_sync.c`, sinalização em `alarm_events.c`, tarefa de aplicação em `alarm_sync_task.c`), `uplink_task` em `main/main.c`, API em `main/webserver/handlers/api_sensors.c` e `api_lorawan.c`, página em `main/www/limites.html`.

## 2. Downlink fPort 11 (limites)

18 bytes, big-endian.

| Byte(s) | Campo | Descrição |
|---|---|---|
| 0 | tipo | `0x01` |
| 1-2 | versão | u16, válida de 1 a 32767 |
| 3-7 | canal 0 (temperatura) | `flags u8 \| low i16 \| high i16` |
| 8-12 | canal 1 (umidade) | idem |
| 13-17 | canal 2 (termopar) | idem |

Flags de cada canal: bit0 = ativo; bit1 = **manter** (ignora os demais campos do canal e não mexe nele).

Tamanho diferente de 18, primeiro byte diferente de `0x01` ou versão fora de 1-32767 é rejeitado com erro 1.

Vetor de exemplo:

```
01 0007 01 0096 00fa 02 0000 0000 00 0000 0000
```

Hex contínuo: `01000701009600fa02000000000000000000`

- versão = 7
- canal 0: flags `01` (ativo), low `0x0096` = 150 = 15,0 °C, high `0x00fa` = 250 = 25,0 °C
- canal 1: flags `02` (manter), valores ignorados
- canal 2: flags `00` (desligado), valores zero (não são validados, pois o canal não está ativo)

Termopar em SKU sem `CONFIG_THERMOCOUPLE_ENABLED`: o canal 2 é tratado como "manter" e o estado dele vai sempre 0.

O caminho da fPort 11 dentro de `lorawan_send` só copia os bytes para uma fila. O parse, a validação e o `config_save` rodam na tarefa `alarm_sync` (pilha 4096, prioridade 3, core 0). Nunca se chama `lorawan_send` dentro do callback de downlink.

## 3. Uplink fPort 3 (evento)

10 bytes, big-endian, não confirmado.

| Byte(s) | Campo | Descrição |
|---|---|---|
| 0 | tipo | `0x01` |
| 1-2 | versão | u16. Bit 15 = 1 quando a última alteração dos limites foi **local** (web/API). Bits 14-0 = versão |
| 3 | estados | bits 1-0 temperatura, 3-2 umidade, 5-4 termopar. 0 = IDLE, 1 = ACTIVE, 2 = ACKNOWLEDGED |
| 4 | botão | toques que silenciaram o alarme desde o último evento enviado; satura em 255 |
| 5-8 | IPv4 | 4 bytes; `0.0.0.0` sem Wi-Fi ou sem conexão |
| 9 | erro | código da seção 4 |

Exemplo: `01 8005 21 03 c0a80a17 00`

- versão 5 com bit 15 ligado (`0x8005`): última alteração local
- estados `0x21` = `10 0001`: temperatura ACTIVE (1), umidade IDLE (0), termopar ACKNOWLEDGED (2)
- botão = 3
- IP 192.168.10.23
- erro 0 (OK)

## 4. Códigos de erro

| Código | Significado |
|---|---|
| 0 | OK (também confirma a aplicação de limites com a nova versão) |
| 1 | formato (tamanho, tipo ou versão inválidos) |
| 2 | `low >= high` em canal ativo |
| 3 | fora da faixa física |
| 4 | falha ao gravar |

Faixas físicas (valem só para canal **ativo** e não "manter"): temperatura -40,0 a 125,0 °C; umidade 0,0 a 100,0 %UR; termopar 0,0 a 1024,0 °C (MAX6675). A ordem (`low < high`) é verificada antes da faixa.

Um downlink rejeitado não altera a configuração. O código de erro fica guardado e vai no próximo evento, e só é zerado depois de enviado com sucesso (e se não mudou nesse meio tempo).

Logs da tarefa `alarm_sync`: `applied v<versão>` (nível info) ou `rejected err=<código>` (nível warning).

## 5. `limites.html`

Página servida pelo dispositivo (SKUs com Wi-Fi) para a plataforma entregar os limites quando o usuário está na rede local do dispositivo. O endereço é `http://<ip>/limites.html` mais um fragmento de URL:

```
#v=<versao>&c0=<flags>,<low>,<high>&c1=<flags>,<low>,<high>&c2=<flags>,<low>,<high>
```

- Mesmas unidades (décimos) e mesmas flags do downlink; ordem fixa das chaves: `v`, `c0`, `c1`, `c2`.
- Exemplo: `#v=8&c0=1,100,300&c1=2,0,0&c2=2,0,0` (temperatura ativa 10,0 a 30,0 °C; umidade e termopar mantidos).
- O fragmento fica só no navegador (não vai para o servidor). Se faltar algo ou estiver mal formado, a página mostra "Link incompleto. Abra de novo pela plataforma."

Fluxo:

1. A página lê o fragmento, mostra uma tabela (canal, situação, mínimo, máximo) e o botão "Gravar no dispositivo".
2. Ao clicar, faz `POST /api/sensors/config` com `limits_version` igual à versão do fragmento e, para cada canal que não é "manter", `alarm_<canal>_enabled`, `_low` e `_high` (valores divididos por 10). O navegador envia a senha (Basic Auth) quando a autenticação está ativa.
3. Sucesso: "Limites gravados. A plataforma confirma no próximo evento. Pode fechar esta aba."; erro: mostra a mensagem JSON devolvida pelo dispositivo.
4. Como o POST traz `limits_version`, o dispositivo grava essa versão e limpa o bit de origem local; o evento seguinte sai com a versão do fragmento e sem o bit 15.

## 6. Quando o evento é enviado

O evento é enviado quando:

- qualquer canal muda de estado;
- o botão silencia o alarme;
- limites são aplicados ou rejeitados (pelo downlink ou pela API);
- como **batimento a cada 15 min** desde o último evento enviado.

Detalhes da `uplink_task`:

- **Carona no envio periódico:** se o batimento vence em até 60 s, o evento vai logo depois da leitura periódica, em vez de sair como uplink separado segundos depois. Um evento pendente também vai nesse momento.
- **Antes do join:** o evento fica pendente. Depois de (re)entrar, o primeiro evento sai imediatamente.
- **Coalescência de 1,5 s:** rajadas (vários canais mudando, ACK, aplicação de limites) viram **um** uplink com o retrato final. O contador do botão só é descontado depois de o envio dar certo; uma notificação que chega durante o envio gera outro evento.
- **Falha de envio ou sem join:** espera de 30 s antes de tentar de novo (limitada pelo prazo da próxima leitura periódica).
- As leituras periódicas nunca são adiantadas por eventos.

Log por envio: `Alarm event fPort 3: sent|FAILED (v=<versão> st=<t>/<u>/<tc> btn=<n> err=<c>) stack free min <N> B`.

## 7. fPorts reservadas

{3, 11, 200, 201, 202} não podem ser a porta do uplink de leituras (3 = evento, 11 = limites, 200-202 = FUOTA/sincronização de relógio).

- `POST /api/lorawan/config` com `port` reservado responde **400** com `"Porta reservada"` (fora de 1-223 responde `"Porta invalida"`).
- `config_load` com uma porta reservada gravada no arquivo usa a porta 1 (log: `lorawan.port <n> is invalid or reserved, using 1`).

## 8. Mudanças na API

`POST /api/sensors/config`:

- Os campos de alarme são validados **antes** de qualquer gravação. Um limite inválido não grava nada (nem outros campos de alarme, nem intervalo, nem buzzer) e responde 400 com JSON `{"success":false,"message":...}`. Mensagens: "Mínimo deve ser menor que o máximo", "Fora da faixa física", "Versão inválida".
- `limits_version` (opcional, inteiro de 1 a 32767): grava essa versão e marca a origem como **plataforma**. Sem `limits_version`, uma alteração real dos limites marca a origem como **local** e mantém a versão.
- **Só alteração real de alarme marca origem local:** reenviar os mesmos valores, ou alterar apenas intervalo, correções, nome ou buzzer, não muda a origem.
- Falha ao gravar responde 500 `{"success":false,"message":"Falha ao gravar"}`.
- Gravar limites pela API também gera evento (erro 0, ou 4 se falhar a gravação).
- Em SKU sem termopar, os campos `alarm_tc_*` são ignorados.

`GET /api/sensors/config` passa a incluir `alarm_limits_version` (número) e `alarm_limits_origin_local` (booleano).

`POST /api/lorawan/config`: recusa `port` reservado (seção 7).

## 9. Marcas d'água de pilha a ler na bancada

Cada tarefa registra a folga mínima de pilha nos logs. Critério: folga >= 512 B depois dos passos 3 a 7 do roteiro.

| Tarefa | Pilha | Log |
|---|---|---|
| `uplink` | 4096 | `Alarm event fPort 3: ... stack free min <N> B` (ler a 2ª linha ou posterior, depois de um downlink ter passado por `lorawan_send`) |
| `alarm_sync` | 4096 | `alarm_sync stack high water mark: <N> bytes` |
| `clock_sync` | 4096 | `clock_sync stack high water mark: <N> bytes` |

## 10. Teste de host (codec)

Não há suíte de testes no ESP-IDF. O codec (`alarm_sync.c`) é testado no PC:

```bash
make -C test/host     # compila com -Wall -Wextra -Werror, roda e imprime OK
make -C test/host clean
```

## 11. Roteiro de bancada e resultados

Dispositivo de teste já cadastrado na plataforma de desenvolvimento ou na de produção, num tipo com `aceita_limites_remotos` **desligado**.

| # | Passo | Resultado |
|---|---|---|
| 1 | `make -C test/host` -> `OK` | **OK** (saída: `OK`) |
| 2 | Gravar `jvtech_4mb_standard` no dispositivo de teste | Pendente (bancada) |
| 3 | No ChirpStack, enfileirar na fPort 11 o hex `01000701009600fa02000000000000000000`. Conferir: log `alarm_sync: applied v7`; `GET /api/sensors/config` com `alarm_temp_low=15`, `alarm_temp_high=25`, `alarm_limits_version=7`; uplink na fPort 3 com `0100 07...` | Pendente (bancada) |
| 4 | Enfileirar payload com `low >= high` -> evento com `erro=2`, config inalterada | Pendente (bancada) |
| 5 | Esquentar o sensor acima do limite -> evento com estado 1. Apertar o botão -> evento com estado 2 e `botao=1` | Pendente (bancada) |
| 6 | Abrir `http://<ip>/limites.html#v=8&c0=1,100,300&c1=2,0,0&c2=2,0,0`, gravar com a senha: evento com versão 8 sem o bit 15. Gravar pela aba normal de configuração: evento com bit 15 | Pendente (bancada) |
| 7 | Deixar 15 min parado -> chega o batimento | Pendente (bancada) |
| 8 | Marcas d'água de `uplink`, `alarm_sync` e `clock_sync` depois dos passos 3 a 7: folga >= 512 B | Pendente (bancada) |
| 9 | `POST /api/lorawan/config` com `{"port":3}` -> 400 | Pendente (bancada) |
| 10 | Compilar os 4 SKUs | **OK** (tabela abaixo) |

Passo 10, ESP-IDF v5.5.3, `./build.sh build <sku>`:

| SKU | Resultado | Tamanho do app | Folga na partição |
|---|---|---|---|
| `jvtech_4mb_standard` | Build complete | 0x13cb30 | 0x634d0 (24%) |
| `jvtech_4mb_thermocouple` | Build complete | 0x13e000 | 0x62000 (24%) |
| `jvtech_2mb_standard` | Build complete | 0x13c6d0 | 0x13930 (6%) |
| `jvtech_2mb_headless` | Build complete | 0x66740 | 0xe98c0 (70%) |

Observação: no `jvtech_2mb_standard` a folga da partição é de apenas 6%.
