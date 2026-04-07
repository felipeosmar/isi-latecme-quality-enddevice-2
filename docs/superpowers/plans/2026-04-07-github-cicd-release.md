# GitHub CI/CD Release Pipeline — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Criar um pipeline GitHub Actions que compila o firmware ESP32 e publica GitHub Releases com os binários ao mergear PRs nas branches `main` e `salt_spray`.

**Architecture:** Um único arquivo de workflow `.github/workflows/release.yml` com dois jobs: `build` (roda em PR e push) e `release` (roda apenas em push após merge). O job `build` usa o container Docker `espressif/idf:v5.5.3` e faz upload dos binários como artefatos. O job `release` baixa os artefatos, calcula o próximo número sequencial via GitHub API, renomeia os arquivos e publica a release.

**Tech Stack:** GitHub Actions, Docker (`espressif/idf:v5.5.3`), ESP-IDF v5.5.3, `gh` CLI, `softprops/action-gh-release@v2`

---

## Arquivos

| Ação | Arquivo |
|------|---------|
| Criar | `.github/workflows/release.yml` |

Branch protection rules são configuradas na UI do GitHub (sem arquivo).

---

### Task 1: Criar o job `build` no workflow

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Passo 1: Criar a estrutura de diretórios e o arquivo de workflow**

```bash
mkdir -p .github/workflows
```

Criar `.github/workflows/release.yml` com o conteúdo:

```yaml
name: Build and Release

on:
  push:
    branches:
      - main
      - salt_spray
  pull_request:
    branches:
      - main
      - salt_spray

jobs:
  build:
    runs-on: ubuntu-latest
    container:
      image: espressif/idf:v5.5.3
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Build firmware
        shell: bash
        run: |
          . $IDF_PATH/export.sh
          idf.py build

      - name: Upload firmware binaries
        uses: actions/upload-artifact@v4
        with:
          name: firmware-binaries
          path: |
            build/lorawan-enddevice.bin
            build/www.bin
          retention-days: 1
```

- [ ] **Passo 2: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: add build job to release workflow"
```

---

### Task 2: Adicionar o job `release` ao workflow

**Files:**
- Modify: `.github/workflows/release.yml`

- [ ] **Passo 1: Adicionar o job `release` ao final do arquivo**

Adicionar após o job `build` (mantendo o restante do arquivo intacto):

```yaml
  release:
    needs: build
    runs-on: ubuntu-latest
    if: github.event_name == 'push'
    permissions:
      contents: write
    steps:
      - name: Download firmware binaries
        uses: actions/download-artifact@v4
        with:
          name: firmware-binaries
          path: artifacts/

      - name: Calculate next release number
        id: release_info
        run: |
          BRANCH="${{ github.ref_name }}"
          COUNT=$(gh api repos/${{ github.repository }}/releases \
            --paginate \
            --jq "[.[] | select(.tag_name | startswith(\"${BRANCH}-r\"))] | length")
          NEXT=$((COUNT + 1))
          echo "branch=${BRANCH}" >> $GITHUB_OUTPUT
          echo "number=${NEXT}" >> $GITHUB_OUTPUT
          echo "tag=${BRANCH}-r${NEXT}" >> $GITHUB_OUTPUT
        env:
          GH_TOKEN: ${{ github.token }}

      - name: Rename binaries with branch and release number
        run: |
          BRANCH="${{ steps.release_info.outputs.branch }}"
          N="${{ steps.release_info.outputs.number }}"
          mv artifacts/lorawan-enddevice.bin \
             artifacts/lorawan-enddevice-${BRANCH}-r${N}.bin
          mv artifacts/www.bin \
             artifacts/www-${BRANCH}-r${N}.bin

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          tag_name: ${{ steps.release_info.outputs.tag }}
          name: "[${{ steps.release_info.outputs.branch }}] Release #${{ steps.release_info.outputs.number }}"
          generate_release_notes: true
          files: |
            artifacts/lorawan-enddevice-${{ steps.release_info.outputs.branch }}-r${{ steps.release_info.outputs.number }}.bin
            artifacts/www-${{ steps.release_info.outputs.branch }}-r${{ steps.release_info.outputs.number }}.bin
```

- [ ] **Passo 2: Verificar o arquivo completo resultante**

O arquivo final `.github/workflows/release.yml` deve ter esta estrutura completa:

```yaml
name: Build and Release

on:
  push:
    branches:
      - main
      - salt_spray
  pull_request:
    branches:
      - main
      - salt_spray

jobs:
  build:
    runs-on: ubuntu-latest
    container:
      image: espressif/idf:v5.5.3
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Build firmware
        shell: bash
        run: |
          . $IDF_PATH/export.sh
          idf.py build

      - name: Upload firmware binaries
        uses: actions/upload-artifact@v4
        with:
          name: firmware-binaries
          path: |
            build/lorawan-enddevice.bin
            build/www.bin
          retention-days: 1

  release:
    needs: build
    runs-on: ubuntu-latest
    if: github.event_name == 'push'
    permissions:
      contents: write
    steps:
      - name: Download firmware binaries
        uses: actions/download-artifact@v4
        with:
          name: firmware-binaries
          path: artifacts/

      - name: Calculate next release number
        id: release_info
        run: |
          BRANCH="${{ github.ref_name }}"
          COUNT=$(gh api repos/${{ github.repository }}/releases \
            --paginate \
            --jq "[.[] | select(.tag_name | startswith(\"${BRANCH}-r\"))] | length")
          NEXT=$((COUNT + 1))
          echo "branch=${BRANCH}" >> $GITHUB_OUTPUT
          echo "number=${NEXT}" >> $GITHUB_OUTPUT
          echo "tag=${BRANCH}-r${NEXT}" >> $GITHUB_OUTPUT
        env:
          GH_TOKEN: ${{ github.token }}

      - name: Rename binaries with branch and release number
        run: |
          BRANCH="${{ steps.release_info.outputs.branch }}"
          N="${{ steps.release_info.outputs.number }}"
          mv artifacts/lorawan-enddevice.bin \
             artifacts/lorawan-enddevice-${BRANCH}-r${N}.bin
          mv artifacts/www.bin \
             artifacts/www-${BRANCH}-r${N}.bin

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          tag_name: ${{ steps.release_info.outputs.tag }}
          name: "[${{ steps.release_info.outputs.branch }}] Release #${{ steps.release_info.outputs.number }}"
          generate_release_notes: true
          files: |
            artifacts/lorawan-enddevice-${{ steps.release_info.outputs.branch }}-r${{ steps.release_info.outputs.number }}.bin
            artifacts/www-${{ steps.release_info.outputs.branch }}-r${{ steps.release_info.outputs.number }}.bin
```

- [ ] **Passo 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: add release job with sequential numbering"
```

---

### Task 3: Fazer push do workflow e validar sintaxe no GitHub

**Files:**
- Nenhum arquivo novo

- [ ] **Passo 1: Push da branch atual para o remote**

```bash
git push origin salt_spray
```

- [ ] **Passo 2: Verificar que o GitHub Actions reconhece o workflow**

Acessar `https://github.com/felipeosmar/isi-latecme-quality-enddevice-2/actions` e confirmar que o workflow "Build and Release" aparece na lista (mesmo sem ter rodado ainda).

Se aparecer um erro de sintaxe YAML, o GitHub mostra um banner de erro na aba Actions — corrigir antes de continuar.

---

### Task 4: Configurar Branch Protection Rules no GitHub

**Files:**
- Nenhum arquivo — configuração feita na UI do GitHub

Para a branch `main`:

- [ ] **Passo 1:** Acessar `Settings → Branches → Add branch protection rule`
- [ ] **Passo 2:** Em "Branch name pattern" digitar `main`
- [ ] **Passo 3:** Marcar **"Require a pull request before merging"**
- [ ] **Passo 4:** Marcar **"Require status checks to pass before merging"**
- [ ] **Passo 5:** No campo de busca de status checks, digitar `build` e selecionar o check `build` do workflow "Build and Release"
  - _Nota: o check só aparece na busca após ter rodado pelo menos uma vez. Se ainda não rodou, salvar a regra sem o check e adicionar depois._
- [ ] **Passo 6:** Marcar **"Do not allow bypassing the above settings"**
- [ ] **Passo 7:** Clicar em **"Create"**

Repetir os mesmos passos para a branch `salt_spray` (Passo 2: digitar `salt_spray`).

---

### Task 5: Validar o pipeline de ponta a ponta

**Files:**
- Nenhum arquivo novo

- [ ] **Passo 1: Criar um commit de teste em `salt_spray_dev`**

```bash
git checkout salt_spray_dev
# Fazer qualquer alteração mínima, ex:
echo "# CI test" >> /tmp/citest.tmp && git add . || true
git commit --allow-empty -m "test: trigger CI validation"
git push origin salt_spray_dev
```

- [ ] **Passo 2: Abrir um PR de `salt_spray_dev` → `salt_spray` no GitHub**

Acessar `https://github.com/felipeosmar/isi-latecme-quality-enddevice-2/compare/salt_spray...salt_spray_dev`

Criar o PR e aguardar o check `build` aparecer e ficar verde (~3-5 min com Docker).

- [ ] **Passo 3: Mergear o PR e verificar a release**

Após o build passar, mergear o PR. Aguardar o job `release` completar (~1 min após o merge).

Verificar em `https://github.com/felipeosmar/isi-latecme-quality-enddevice-2/releases` que:
- Uma release com tag `salt_spray-r1` foi criada
- O título é `[salt_spray] Release #1`
- Os arquivos `lorawan-enddevice-salt_spray-r1.bin` e `www-salt_spray-r1.bin` estão anexados
- Os arquivos são baixáveis e têm tamanho > 0

- [ ] **Passo 4: Repetir o teste para `main`**

Criar PR de `main_dev` → `main`, mergear, confirmar que a release `main-r1` é criada corretamente.
