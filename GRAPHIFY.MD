Como aplicar em qualquer repo local
# 1. Ollama rodando como serviço (sistema). Já está. Confirmar com:
systemctl is-active ollama   # → active
# 2. Modelos puxados (qwen3:8b para texto, llava:7b para imagens). Já está.
ollama list
# 3. CLI graphify do nosso fork instalada via uv. Já está.
which graphify   # → /home/felipe/.local/bin/graphify
# 4. Env var de opt-in (precisa estar na sessão ou no ~/.bashrc):
echo 'export GRAPHIFY_OLLAMA_KEY=ollama' >> ~/.bashrc
source ~/.bashrc                   
Por repo (qualquer projeto):
cd /caminho/do/seu/repo          
# (opcional mas recomendado) — criar .graphifyignore para cortar lixo
cp /home/felipe/work/graphify/local-extras/graphifyignore.template .graphifyignore
# ajustar para o repo                                   
$EDITOR .graphifyignore
# rodar extração
graphify extract . --backend ollama
# saída fica em "./graphify-out/"
ls graphify-out/   # graph.json, manifest.json, cache/
# Re-rodar (incremental)         
graphify extract . --backend ollama   # cache + manifest detectam mudanças
# Para outra máquina (não a sua atual): teria que reproduzir o setup — instalar Ollama, puxar modelos, e instalar o CLI do nosso fork:
uv tool install git+https://github.com/felipeosmar/graphify.git@local-ollama-backend

Comece pelo Nível 1 (skill global). Depois, se gostar, ative Nível 2 nos projetos onde você trabalha mais.

# 1× só
graphify install --platform claude

# por repo, sempre que quiser atualizar memória:
cd /seu/projeto
GRAPHIFY_OLLAMA_KEY=ollama graphify extract . --backend ollama

graphify tree  # gera graphify-out/graph.html
xdg-open graphify-out/graph.html       # abre no navegador padrão

O HTML é interativo:                                 
- Click em nó → mostra vizinhos                 
- Filtro por tipo (code/document/image)         
- Busca por nome                            
- Tudo offline, single-file (pode mandar pra alguém abrir)

Alternativas dependendo do que você quer ver: 

# overview textual com god nodes, comunidades, perguntas sugeridas
graphify cluster-only graphify-out/graph.json   # gera/atualiza GRAPH_REPORT.md
cat graphify-out/GRAPH_REPORT.md            

# pergunta livre (BFS no grafo, responde em texto)
graphify query "como funciona o cadastro de equipamento?"

# caminho mais curto entre 2 entidades             
graphify path "EquipmentService" "Production"         

# explica um nó e seus vizinhos                 
graphify explain "BinService"                         

# stats rápidas via jq                      
jq '.nodes | length' graphify-out/graph.json  
jq '[.nodes[].file_type] | group_by(.) | map({type: .[0], count: length})' graphify-out/graph.json

Roda graphify tree && xdg-open graphify-out/graph.html primeiro pra ter a visão geral. Volta pra cá se algo não fizer sentido no grafo.