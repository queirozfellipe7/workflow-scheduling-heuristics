#!/usr/bin/env bash
# =============================================================================
# run_experiments.sh
# Executa cada combinação heurística × workflow × trial e coleta resultados.
#
# INTEGRAÇÃO COM gerador_workflow_3.py
# ------------------------------------
# Antes de rodar este script, gere os workflows por escala:
#
#   cd ~/Polybench_Workflows
#   for tasks in 256 512 1024 2048; do
#       python3 gerador_workflow_3.py \
#           --folder ./data \
#           --tasks $tasks \
#           --platform 8 \
#           --batch \
#           --n-trials 10 \
#           --base-seed 1000 \
#           --output-dir ./trials_${tasks}
#   done
#
# Isso cria:
#   Polybench_Workflows/trials_256/wf_256_tasks_8_cores_trial01.json  ... trial10.json
#   Polybench_Workflows/trials_512/wf_512_tasks_8_cores_trial01.json  ... trial10.json
#   Polybench_Workflows/trials_1024/wf_1024_tasks_8_cores_trial01.json ... trial10.json
#   Polybench_Workflows/trials_2048/wf_2048_tasks_8_cores_trial01.json ... trial10.json
#
# Uso:
#   chmod +x run_experiments.sh
#   ./run_experiments.sh
#
# Saída:
#   results/results_raw.csv  — uma linha por trial (dados principais)
#   results/results_gpu.csv  — tempo de uso por GPU por trial
#   results/run_experiments.log — log completo de execução
# =============================================================================

set -euo pipefail

# ─────────────────────────────────────────────────────────────────────────────
# CONFIGURAÇÃO — ajuste apenas esta seção se necessário
# ─────────────────────────────────────────────────────────────────────────────

# Diretório raiz onde ficam as pastas CarbonPolywrench_*
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Número de repetições por cenário (deve bater com --n-trials do gerador)
N_TRIALS=10

# Heurísticas e seus diretórios
declare -A HEURISTICS=(
    ["HEFT"]="CarbonPolywrench_HEFT"
    ["MOHEFT"]="CarbonPolywrench_MOHEFT"
    ["IPEFT"]="CarbonPolywrench_IPEFT"
    ["PEFT"]="CarbonPolywrench_PEFT"
)

# Escalas de workflow (número de tarefas)
# Valor = número de cores usado no nome do arquivo (deve bater com --platform do gerador)
declare -A SCALES=(
    ["256"]="8"
    ["512"]="8"
    ["1024"]="8"
    ["2048"]="8"
)

# Pasta base dos workflows (relativa ao ROOT_DIR)
# O gerador salva em Polybench_Workflows/trials_<scale>/
WORKFLOW_BASE_DIR="${ROOT_DIR}/Polybench_Workflows"

# Caminhos relativos ao build/ de cada heurística
PLATFORM_FILE="../data/platform.xml"
SIMULATOR_BIN="./my-wrench-simulator"

# Arquivos de saída
OUTPUT_DIR="${ROOT_DIR}/results"
RAW_CSV="${OUTPUT_DIR}/results_raw.csv"
GPU_CSV="${OUTPUT_DIR}/results_gpu.csv"
LOG_FILE="${OUTPUT_DIR}/run_experiments.log"

# ─────────────────────────────────────────────────────────────────────────────
# INICIALIZAÇÃO
# ─────────────────────────────────────────────────────────────────────────────

mkdir -p "${OUTPUT_DIR}"

# Cabeçalho do CSV principal
echo "heuristica,escala,trial,workflow_file,makespan,energia_kwh,carbono_gco2,utilizacao_media" \
    > "${RAW_CSV}"

# Cabeçalho do CSV de GPUs
echo "heuristica,escala,trial,host,tempo_uso_s" \
    > "${GPU_CSV}"

# Inicializa log
exec > >(tee -a "${LOG_FILE}") 2>&1
echo "============================================================"
echo "  Iniciando experimentos — $(date '+%Y-%m-%d %H:%M:%S')"
echo "  Root dir : ${ROOT_DIR}"
echo "  Trials   : ${N_TRIALS}"
echo "  Saída    : ${OUTPUT_DIR}"
echo "============================================================"

# Contadores globais
TOTAL_RUNS=$(( ${#HEURISTICS[@]} * ${#SCALES[@]} * N_TRIALS ))
COMPLETED=0
FAILED=0

# ─────────────────────────────────────────────────────────────────────────────
# FUNÇÕES
# ─────────────────────────────────────────────────────────────────────────────

# Monta o caminho do workflow para um dado (scale, trial).
# Convenção de nome gerada pelo gerador_workflow_3.py --batch:
#   Polybench_Workflows/trials_<scale>/wf_<scale>_tasks_<cores>_cores_trial<NN>.json
get_workflow_path() {
    local scale="$1"
    local cores="$2"
    local trial="$3"
    local trial_padded
    trial_padded=$(printf '%02d' "${trial}")
    echo "${WORKFLOW_BASE_DIR}/trials_${scale}/wf_${scale}_tasks_${cores}_cores_trial${trial_padded}.json"
}

# Parse completo da saída do simulador.
# Preenche variáveis globais: MAKESPAN, ENERGIA_KWH, CARBONO_GCO2, UTIL_MEDIA
# e array GPU_TIMES[host]=tempo
parse_output() {
    local raw="$1"

    # Makespan — linha: "Makespan Total: 973.8540 s"
    MAKESPAN=$(echo "${raw}" \
        | grep -m1 "Makespan Total:" \
        | grep -oP '[0-9]+\.[0-9]+' \
        | head -1)

    # Energia total kWh — linha: "Energia total consumida:  0.529 kWh"
    ENERGIA_KWH=$(echo "${raw}" \
        | grep -m1 "Energia total consumida:" \
        | grep -oP '[0-9]+\.[0-9]+' \
        | head -1)

    # Carbono total gCO2 — linha: "Pegada de carbono total:  238.0 gCO₂"
    CARBONO_GCO2=$(echo "${raw}" \
        | grep -m1 "Pegada de carbono total:" \
        | grep -oP '[0-9]+\.[0-9]+' \
        | head -1)

    # Utilização média — linha: "Utilização média (aprox.):  42.5 %"
    UTIL_MEDIA=$(echo "${raw}" \
        | grep -m1 "Utilização média" \
        | grep -oP '[0-9]+\.[0-9]+' \
        | head -1)

    # Tempos por host — formato: "GPU1  | 336.6479 s"
    declare -gA GPU_TIMES=()
    while IFS= read -r line; do
        if echo "${line}" | grep -qP '^\s*\w+\s*\|\s*[0-9]+\.[0-9]+\s*s'; then
            local host tempo
            host=$(echo "${line}"  | awk -F'|' '{print $1}' | tr -d ' ')
            tempo=$(echo "${line}" | awk -F'|' '{print $2}' | grep -oP '[0-9]+\.[0-9]+')
            GPU_TIMES["${host}"]="${tempo}"
        fi
    done <<< "${raw}"

    # Validação dos campos críticos
    if [[ -z "${MAKESPAN}" || -z "${ENERGIA_KWH}" || -z "${CARBONO_GCO2}" ]]; then
        return 1
    fi
    return 0
}

# ─────────────────────────────────────────────────────────────────────────────
# PRÉ-VERIFICAÇÃO: checa se todos os workflow files existem antes de começar
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo "  Verificando arquivos de workflow..."
MISSING_FILES=0
for SCALE in "${!SCALES[@]}"; do
    CORES="${SCALES[$SCALE]}"
    for (( T=1; T<=N_TRIALS; T++ )); do
        WF_PATH=$(get_workflow_path "${SCALE}" "${CORES}" "${T}")
        if [[ ! -f "${WF_PATH}" ]]; then
            echo "  [AUSENTE] ${WF_PATH}"
            (( MISSING_FILES++ )) || true
        fi
    done
done

if [[ "${MISSING_FILES}" -gt 0 ]]; then
    echo ""
    echo "  [ERRO] ${MISSING_FILES} arquivo(s) de workflow não encontrado(s)."
    echo "  Execute o gerador primeiro (veja comentário no topo deste script):"
    echo ""
    echo "    cd ~/Polybench_Workflows"
    echo "    for tasks in 256 512 1024 2048; do"
    echo "        python3 gerador_workflow_3.py \\"
    echo "            --folder ./data --tasks \$tasks --platform 8 \\"
    echo "            --batch --n-trials ${N_TRIALS} --base-seed 1000 \\"
    echo "            --output-dir ./trials_\${tasks}"
    echo "    done"
    echo ""
    exit 1
fi
echo "  Todos os arquivos de workflow presentes. Iniciando simulações..."
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# LOOP PRINCIPAL
# ─────────────────────────────────────────────────────────────────────────────

for HEURISTIC in "${!HEURISTICS[@]}"; do
    HEURISTIC_DIR="${ROOT_DIR}/${HEURISTICS[$HEURISTIC]}"
    BUILD_DIR="${HEURISTIC_DIR}/build"

    if [[ ! -d "${BUILD_DIR}" ]]; then
        echo "[ERRO] Diretório não encontrado: ${BUILD_DIR}"
        echo "       Verifique se o projeto foi compilado (cmake + make)."
        (( FAILED++ )) || true
        continue
    fi

    if [[ ! -x "${BUILD_DIR}/my-wrench-simulator" ]]; then
        echo "[AVISO] Binário não encontrado ou sem permissão: ${BUILD_DIR}/my-wrench-simulator"
        echo "        Tentando prosseguir mesmo assim..."
    fi

    for SCALE in "${!SCALES[@]}"; do
        CORES="${SCALES[$SCALE]}"

        echo ""
        echo "────────────────────────────────────────────────────────"
        echo "  Heurística : ${HEURISTIC}"
        echo "  Escala     : ${SCALE} tarefas  |  Plataforma: ${CORES} cores"
        echo "────────────────────────────────────────────────────────"

        for (( TRIAL=1; TRIAL<=N_TRIALS; TRIAL++ )); do

            # ── Caminho do workflow específico deste trial ──────────
            WORKFLOW_FILE=$(get_workflow_path "${SCALE}" "${CORES}" "${TRIAL}")
            WORKFLOW_FNAME=$(basename "${WORKFLOW_FILE}")

            printf "  Trial %02d/%02d [%s] ... " "${TRIAL}" "${N_TRIALS}" "${WORKFLOW_FNAME}"

            # Arquivo não existe (não deveria chegar aqui após a pré-verificação)
            if [[ ! -f "${WORKFLOW_FILE}" ]]; then
                echo "[AUSENTE] — workflow não encontrado"
                (( FAILED++ )) || true
                continue
            fi

            # ── Execução do simulador ───────────────────────────────
            SIM_OUTPUT=$(
                cd "${BUILD_DIR}" && \
                ${SIMULATOR_BIN} "${PLATFORM_FILE}" "${WORKFLOW_FILE}" 2>&1
            ) || {
                echo "[FALHOU] — erro de execução"
                echo "[ERRO] Trial ${TRIAL} falhou: ${HEURISTIC} × ${SCALE} tasks" >&2
                (( FAILED++ )) || true
                continue
            }

            # ── Parse ───────────────────────────────────────────────
            MAKESPAN=""
            ENERGIA_KWH=""
            CARBONO_GCO2=""
            UTIL_MEDIA=""
            declare -A GPU_TIMES=()

            if ! parse_output "${SIM_OUTPUT}"; then
                echo "[PARSE FALHOU] — valores não extraídos"
                echo "[ERRO] Parse falhou: ${HEURISTIC} × ${SCALE} × trial ${TRIAL}" >&2
                echo "${SIM_OUTPUT}" > "${OUTPUT_DIR}/debug_${HEURISTIC}_${SCALE}_trial${TRIAL}.txt"
                (( FAILED++ )) || true
                continue
            fi

            # ── Grava CSV principal ─────────────────────────────────
            echo "${HEURISTIC},${SCALE},${TRIAL},${WORKFLOW_FNAME},${MAKESPAN},${ENERGIA_KWH},${CARBONO_GCO2},${UTIL_MEDIA}" \
                >> "${RAW_CSV}"

            # ── Grava CSV de GPUs ───────────────────────────────────
            for HOST in "${!GPU_TIMES[@]}"; do
                echo "${HEURISTIC},${SCALE},${TRIAL},${HOST},${GPU_TIMES[$HOST]}" \
                    >> "${GPU_CSV}"
            done

            (( COMPLETED++ )) || true
            printf "makespan=%-10s energia=%-8s carbono=%-8s [OK]\n" \
                "${MAKESPAN}s" "${ENERGIA_KWH}kWh" "${CARBONO_GCO2}gCO2"
        done

        # Progresso geral
        PCT=$(( COMPLETED * 100 / TOTAL_RUNS ))
        echo "  Progresso global: ${COMPLETED}/${TOTAL_RUNS} (${PCT}%)"
    done
done

# ─────────────────────────────────────────────────────────────────────────────
# RESUMO FINAL
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo "============================================================"
echo "  EXECUÇÃO CONCLUÍDA — $(date '+%Y-%m-%d %H:%M:%S')"
echo "============================================================"
echo "  Simulações completas : ${COMPLETED}"
echo "  Falhas               : ${FAILED}"
echo "  Arquivo principal    : ${RAW_CSV}"
echo "  Arquivo GPUs         : ${GPU_CSV}"
echo "  Log completo         : ${LOG_FILE}"
echo "============================================================"

# Valida o CSV gerado
LINE_COUNT=$(( $(wc -l < "${RAW_CSV}") - 1 ))
EXPECTED=$(( ${#HEURISTICS[@]} * ${#SCALES[@]} * N_TRIALS ))
echo ""
if [[ "${LINE_COUNT}" -eq "${EXPECTED}" ]]; then
    echo "  [OK] CSV completo: ${LINE_COUNT} linhas (esperado: ${EXPECTED})"
else
    echo "  [AVISO] CSV incompleto: ${LINE_COUNT} linhas (esperado: ${EXPECTED})"
    echo "          Verifique o log para identificar falhas."
fi
echo ""

exit 0
