# Pendências do firmware

Lista de trabalho para uma sessão futura. Cada item diz o problema, onde está e quando está pronto. Ao concluir um item, remova-o daqui no mesmo PR da correção.

---

## 1. Segurança: `/api/files/*` não exige senha

**Prioridade:** alta. **Base:** `main_dev`. **Branch sugerida:** `fix/api-files-auth`.

### Problema
Os handlers de `main/webserver/handlers/api_files.c` não chamam `check_auth()`. Qualquer aparelho na mesma rede (Wi-Fi do laboratório ou o AP do dispositivo) consegue, sem senha:

- ler `/userdata/config.json` (`/api/files/read`, `/download`, `/view`) — contém a senha da página web, a senha do Wi-Fi e a AppKey LoRaWAN;
- regravar ou apagar esse arquivo (`/api/files/write`, `/upload`, `/delete`, `/mkdir`) — inclusive pôr `"auth_enabled": false`, desligando a senha das demais rotas, ou trocar a AppKey.

As outras rotas (`/api/lorawan/*`, `/api/sensors/*`, `/api/ota/*`) já usam `check_auth()` de `handlers/auth.c`.

### O que fazer
1. Em todos os handlers de `api_files.c` (`list`, `info`, `download`, `view`, `read`, `write`, `delete`, `mkdir`, `upload`), começar com `if (!check_auth(req)) return send_unauthorized(req);`, igual a `api_lorawan.c`. Handlers `OPTIONS` (preflight), se houver, continuam sem senha.
2. Conferir que a aba "Arquivos" da página (`/tabs/files.html` / `files.js`) envia as credenciais como as outras abas e trata o 401 (pede a senha de novo em vez de falhar calada).
3. Antes de fechar, confirmar com o responsável que nenhuma ferramenta de fábrica/instalação usa `/api/files/*` sem senha (se usar, ela passa a precisar mandar Basic auth).

### Fora do escopo deste item (registrar, não corrigir junto)
- Credenciais de fábrica `admin/admin` iguais em todos os dispositivos.
- `Access-Control-Allow-Origin: *` nas rotas da API: qualquer site aberto no navegador de quem está na rede pode chamar a API se o usuário já tiver digitado a senha na sessão.
- `GET /api/lorawan/config` devolve a AppKey em texto (protegido por senha, mas exposto a quem tem a senha da página).

### Pronto quando
- `curl -s -o /dev/null -w '%{http_code}' http://<ip>/api/files/read?path=/userdata/config.json` → `401`.
- Com `-u admin:<senha>` → `200` com o conteúdo.
- O mesmo para `write`, `delete`, `upload`, `mkdir`, `list`, `info`, `download`, `view`.
- A aba "Arquivos" da página continua funcionando depois do login.
- Teste em bancada registrado no PR (o projeto não tem teste de host para o servidor HTTP).

### Contexto
Encontrado ao desenhar a gestão de alarmes da plataforma ISI IoT (`isi-iot-platform`, `docs/superpowers/specs/2026-11-25-gestao-alarmes-design.md`, §11). A branch de alarmes do firmware (`feat/alarmes-plataforma`) é separada e não depende desta correção, mas a nova página `limites.html` dela usa o mesmo `check_auth()`.
