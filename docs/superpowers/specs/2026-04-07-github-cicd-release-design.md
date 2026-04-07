# GitHub CI/CD & Release Pipeline — Design Spec

**Date:** 2026-04-07
**Status:** Approved

## Overview

Organizar o repositório com dois firmwares mantidos em paralelo (`main` e `salt_spray`), com branches de desenvolvimento dedicadas (`main_dev` e `salt_spray_dev`). Toda integração acontece via Pull Request. Quando um PR é mergeado em `main` ou `salt_spray`, um pipeline compila o firmware e publica uma GitHub Release com os binários.

---

## Branch Strategy

```
main_dev       ──→ PR ──→ main         (release main-rN)
salt_spray_dev ──→ PR ──→ salt_spray   (release salt_spray-rN)
```

- `main` e `salt_spray` são branches protegidas — nenhum push direto permitido
- O único caminho de integração é via PR a partir dos branches `_dev`
- O build CI deve passar antes do merge ser liberado (status check obrigatório)

### Branch Protection Rules (configurado no GitHub UI)

Para `main` e `salt_spray`:
- **Require a pull request before merging** — sem push direto
- **Require status checks to pass** — o job `build` do workflow deve ser verde
- **Do not allow bypassing the above settings** — nem admins podem fazer push direto

---

## CI/CD Pipeline

**Arquivo:** `.github/workflows/release.yml`

### Gatilhos

| Evento | Branches alvo | Jobs executados |
|--------|--------------|-----------------|
| `pull_request` | `main`, `salt_spray` | `build` |
| `push` (merge de PR) | `main`, `salt_spray` | `build` + `release` |

### Job 1: `build`

- **Runner:** `ubuntu-latest` com container `espressif/idf:v5.5.3`
- **Passos:**
  1. Checkout do código
  2. `idf.py build`
  3. Upload dos artefatos: `lorawan-enddevice.bin` e `www.bin`
- **Papel no fluxo:** Status check obrigatório — bloqueia merge se falhar

### Job 2: `release`

- **Condição:** Só roda em `push` (após merge), nunca em PRs
- **Depende de:** `build` (reusa os artefatos)
- **Passos:**
  1. Download dos artefatos do job `build`
  2. Calcular próximo número sequencial (`N`) contando releases existentes com o prefixo da branch (`main-r*` ou `salt_spray-r*`)
  3. Renomear arquivos:
     - `lorawan-enddevice-{branch}-r{N}.bin`
     - `www-{branch}-r{N}.bin`
  4. Criar GitHub Release com tag `{branch}-r{N}`, título `[{branch}] Release #{N}`
  5. Anexar os dois `.bin` à release

### Unificação via variável de branch

O workflow usa `github.ref_name` para determinar em qual branch está rodando e derivar o prefixo da release (`main` ou `salt_spray`). Não é necessário matrix strategy — o workflow dispara uma única vez por push, já no contexto da branch correta. A unificação vem de lógica condicional compartilhada num único arquivo.

---

## Artefatos de Release

Cada release publica dois arquivos:

| Arquivo | Conteúdo |
|---------|----------|
| `lorawan-enddevice-{branch}-r{N}.bin` | Firmware principal (app) |
| `www-{branch}-r{N}.bin` | Web UI (partição www, LittleFS) |

Exemplo para `main`, terceiro release: `lorawan-enddevice-main-r3.bin` e `www-main-r3.bin`

---

## Versionamento Sequencial

- O número `N` é calculado dinamicamente contando as releases existentes com o prefixo `{branch}-r`
- Histórico completo é preservado — nenhuma release é sobrescrita ou deletada
- Cada merge em `main` ou `salt_spray` gera uma nova release numerada

---

## Estrutura de Arquivos a Criar

```
.github/
└── workflows/
    └── release.yml
```

Branch protection rules são configuradas via GitHub UI (não são arquivo — são settings do repositório).

---

## Fora do Escopo

- Releases automáticas de branches `_dev` (apenas `main` e `salt_spray` geram releases)
- Versionamento semântico (semver) — usar numeração sequencial simples
- OTA (over-the-air update) automático a partir da release
- Merge automático de `main` → `salt_spray` ou vice-versa
