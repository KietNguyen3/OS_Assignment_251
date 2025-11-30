#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OS_BIN="${ROOT}/os"
INPUT_DIR="${ROOT}/input"
OUTPUT_DIR="${ROOT}/output"
ACTUAL_DIR="${OUTPUT_DIR}/paging_actual"

mkdir -p "${ACTUAL_DIR}"

# ---- Paging-related config files ----
PAGING_CFGS=(
  os_0_mlq_paging
  os_1_mlq_paging
  os_1_mlq_paging_small_1K
  os_1_mlq_paging_small_4K
  os_1_singleCPU_mlq_paging
  os_demand_small_5level
  os_swap_fifo
)

# ---- Expected STATS tags the OS must print ----
STATS_TAGS=(
  mem_access      # total page-table lookups
  page_faults     # page-fault count
  swap_in         # number of swap-ins
  swap_out        # number of swap-outs
  pt_bytes        # bytes used for page tables
)

# ---- Colors ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

echo "=== Running paging tests ==="
echo

# ---- Ensure os is built ----
if [[ ! -x "${OS_BIN}" ]]; then
  echo "[BUILD] os not found → running 'make os'..."
  (cd "${ROOT}" && make os)
else
  echo "[BUILD] os already built → skipping 'make os'."
fi

overall_pass=true
logic_fail=false

# ----------------------------------------------------------
# Helper: parse a numeric STATS value from output
#   parse_stat file key  -> echo number or -1 if not found
# ----------------------------------------------------------
parse_stat() {
  local file="$1"
  local key="$2"
  local val

  # Expect format: [STATS] key = value
  val=$(awk -v k="$key" '$1=="[STATS]" && $2==k && $3=="=" {print $4}' "$file" | tail -n 1)

  if [[ -z "$val" ]]; then
    echo "-1"
  else
    echo "$val"
  fi
}

# ==========================================================
# 1) Run each config, capture output, diff, basic STATS check
# ==========================================================
for cfg in "${PAGING_CFGS[@]}"; do
  cfg_path="${INPUT_DIR}/${cfg}"
  expected_file="${OUTPUT_DIR}/${cfg}.output"
  actual_file="${ACTUAL_DIR}/${cfg}.actual"

  echo "------------------------------------------------------------"
  echo "[TEST] ${cfg}"

  if [[ ! -f "${cfg_path}" ]]; then
    echo -e "  ${YELLOW}[SKIP]${NC} Config file '${cfg_path}' not found"
    continue
  fi

  echo "  [RUN ] ${OS_BIN} ${cfg}"

  # Run OS, capture both stdout+stderr
  set +e
  "${OS_BIN}" "${cfg}" >"${actual_file}" 2>&1
  rc=$?
  set -e

  if [[ $rc -ne 0 ]]; then
    overall_pass=false
    echo -e "  ${RED}[FAIL]${NC} Program exited with status ${rc}"
    if grep -qi "segmentation fault" "${actual_file}" 2>/dev/null; then
      echo "         Detected 'Segmentation fault' in output."
    fi
    echo "         See '${actual_file}' for full log."
    echo "  ----- Last 40 lines of ACTUAL output (${cfg}) -----"
    tail -n 40 "${actual_file}" || true
    echo "  ---------------------------------------------------"
    continue
  fi

  # ---- Check that STATS lines are present ----
  missing_stats=0
  for tag in "${STATS_TAGS[@]}"; do
    if ! grep -q "^\[STATS\].*${tag}" "${actual_file}" 2>/dev/null; then
      missing_stats=1
      echo -e "  ${YELLOW}[WARN]${NC} Missing STATS line for '${tag}' in ${cfg}"
    fi
  done

  # ---- Diff against expected output if it exists ----
  if [[ -f "${expected_file}" ]]; then
    if diff -u "${expected_file}" "${actual_file}" >/dev/null 2>&1; then
      if [[ ${missing_stats} -eq 0 ]]; then
        echo -e "  ${GREEN}[PASS]${NC} Output matches and all STATS tags found."
      else
        echo -e "  ${GREEN}[OK  ]${NC} Output matches expected file, STATS incomplete."
      fi
    else
      overall_pass=false
      echo -e "  ${RED}[FAIL]${NC} Output differs from expected."
      echo "         Expected: ${expected_file}"
      echo "         Actual  : ${actual_file}"

      echo "  ----- DIFF (expected vs actual) -----"
      diff -u "${expected_file}" "${actual_file}" | head -n 80 || true
      echo "  -------------------------------------"

      echo "  ----- FIRST 40 lines of EXPECTED (${cfg}) -----"
      head -n 40 "${expected_file}" 2>/dev/null || true
      echo "  ----- FIRST 40 lines of ACTUAL (${cfg}) -----"
      head -n 40 "${actual_file}" 2>/dev/null || true
      echo "  ----------------------------------------------"
    fi
  else
    # No golden file → at least check that it ran and printed stats
    if [[ ${missing_stats} -eq 0 ]]; then
      echo -e "  ${GREEN}[PASS]${NC} Program ran successfully (no expected .output to diff)."
    else
      echo -e "  ${GREEN}[OK  ]${NC} Program ran successfully but STATS tags incomplete."
    fi
  fi

  echo
done

# ==========================================================
# 2) High-level paging LOGIC checks (based on [STATS] tags)
#     - Demand paging
#     - Replacement under pressure
#     - Multi-process behaviour
# ==========================================================

echo "============================================================"
echo "=== High-level paging LOGIC checks (using [STATS] tags) ==="
echo "============================================================"

# ---- 2.1 Demand paging test: os_demand_small_5level ----
logic_check_demand_small() {
  local file="${ACTUAL_DIR}/os_demand_small_5level.actual"
  if [[ ! -f "${file}" ]]; then
    echo -e "  ${YELLOW}[SKIP]${NC} os_demand_small_5level.actual not found"
    return
  fi

  echo "[LOGIC] Checking demand paging on os_demand_small_5level ..."

  local mem_access page_faults swap_in swap_out pt_bytes
  mem_access=$(parse_stat "${file}" "mem_access")
  page_faults=$(parse_stat "${file}" "page_faults")
  swap_in=$(parse_stat "${file}" "swap_in")
  swap_out=$(parse_stat "${file}" "swap_out")
  pt_bytes=$(parse_stat "${file}" "pt_bytes")

  if (( mem_access < 0 || page_faults < 0 || pt_bytes < 0 )); then
    echo -e "  ${YELLOW}[WARN]${NC} Missing some STATS in os_demand_small_5level → cannot fully verify demand paging."
    logic_fail=true
    return
  fi

  # Expect:
  # - some page faults (first touches)
  # - mem_access >= page_faults
  # - pt_bytes significantly less than 64KB (full table)
  if (( mem_access > 0 )); then
    echo -e "  ${GREEN}[LOGIC OK]${NC} mem_access > 0 (paging used)."
  else
    echo -e "  ${RED}[LOGIC FAIL]${NC} mem_access must be > 0 for demand paging."
    logic_fail=true
  fi

  if (( page_faults > 0 )); then
    echo -e "  ${GREEN}[LOGIC OK]${NC} page_faults > 0 (first-touch faults)."
  else
    echo -e "  ${RED}[LOGIC FAIL]${NC} page_faults must be > 0 in demand-small test."
    logic_fail=true
  fi

  if (( mem_access >= page_faults )); then
    echo -e "  ${GREEN}[LOGIC OK]${NC} mem_access >= page_faults."
  else
    echo -e "  ${RED}[LOGIC FAIL]${NC} mem_access should be >= page_faults."
    logic_fail=true
  fi

  if (( pt_bytes > 0 && pt_bytes < 65536 )); then
    echo -e "  ${GREEN}[LOGIC OK]${NC} pt_bytes in (0, 64KB) → tables allocated on demand."
  else
    echo -e "  ${RED}[LOGIC FAIL]${NC} pt_bytes must be >0 and <64KB to show demand paging."
    logic_fail=true
  fi
}

# ---- 2.2 Replacement pressure: small RAM configs ----
logic_check_small_ram() {
  local cfg="$1"
  local file="${ACTUAL_DIR}/${cfg}.actual"
  if [[ ! -f "${file}" ]]; then
    echo -e "  ${YELLOW}[SKIP]${NC} ${cfg}.actual not found"
    return
  fi

  echo "[LOGIC] Checking replacement pressure on ${cfg} ..."

  local swap_in swap_out page_faults
  swap_in=$(parse_stat "${file}" "swap_in")
  swap_out=$(parse_stat "${file}" "swap_out")
  page_faults=$(parse_stat "${file}" "page_faults")

  if (( swap_in < 0 || swap_out < 0 || page_faults < 0 )); then
    echo -e "  ${YELLOW}[WARN]${NC} Missing STATS in ${cfg} → cannot fully verify FIFO/Clock replacement."
    logic_fail=true
    return
  fi

  # Under small RAM, multi-process workload should trigger swapping
  if (( page_faults > 0 )); then
    echo -e "  ${GREEN}[LOGIC OK]${NC} page_faults > 0 under memory pressure in ${cfg}."
  else
    echo -e "  ${RED}[LOGIC FAIL]${NC} page_faults should be >0 in ${cfg} (memory pressure case)."
    logic_fail=true
  fi

  if (( swap_in > 0 )); then
    echo -e "  ${GREEN}[LOGIC OK]${NC} swap_in > 0 (pages brought from SWAP) in ${cfg}."
  else
    echo -e "  ${RED}[LOGIC FAIL]${NC} swap_in should be >0 in ${cfg} when RAM is small."
    logic_fail=true
  fi

  if (( swap_out > 0 )); then
    echo -e "  ${GREEN}[LOGIC OK]${NC} swap_out > 0 (victims pushed to SWAP) in ${cfg}."
  else
    echo -e "  ${RED}[LOGIC FAIL]${NC} swap_out should be >0 in ${cfg} when RAM is small."
    logic_fail=true
  fi
}

# ---- 2.3 Multi-process / normal load: os_1_singleCPU_mlq_paging ----
logic_check_singlecpu_mlq() {
  local file="${ACTUAL_DIR}/os_1_singleCPU_mlq_paging.actual"
  if [[ ! -f "${file}" ]]; then
    echo -e "  ${YELLOW}[SKIP]${NC} os_1_singleCPU_mlq_paging.actual not found"
    return
  fi

  echo "[LOGIC] Checking multi-process stats on os_1_singleCPU_mlq_paging ..."

  local mem_access page_faults swap_in swap_out pt_bytes
  mem_access=$(parse_stat "${file}" "mem_access")
  page_faults=$(parse_stat "${file}" "page_faults")
  swap_in=$(parse_stat "${file}" "swap_in")
  swap_out=$(parse_stat "${file}" "swap_out")
  pt_bytes=$(parse_stat "${file}" "pt_bytes")

  if (( mem_access < 0 || page_faults < 0 || pt_bytes < 0 )); then
    echo -e "  ${YELLOW}[WARN]${NC} Missing STATS in os_1_singleCPU_mlq_paging → cannot fully verify."
    logic_fail=true
    return
  fi

  if (( mem_access >= page_faults )); then
    echo -e "  ${GREEN}[LOGIC OK]${NC} mem_access >= page_faults on os_1_singleCPU_mlq_paging."
  else
    echo -e "  ${RED}[LOGIC FAIL]${NC} mem_access should be >= page_faults on os_1_singleCPU_mlq_paging."
    logic_fail=true
  fi

  if (( pt_bytes > 0 )); then
    echo -e "  ${GREEN}[LOGIC OK]${NC} pt_bytes > 0 (some tables allocated) on os_1_singleCPU_mlq_paging."
  else
    echo -e "  ${RED}[LOGIC FAIL]${NC} pt_bytes must be >0 on os_1_singleCPU_mlq_paging."
    logic_fail=true
  fi
}

# ---- Run logic checks ----
logic_check_demand_small
logic_check_small_ram "os_1_mlq_paging_small_1K"
logic_check_small_ram "os_1_mlq_paging_small_4K"
logic_check_singlecpu_mlq

echo "============================================================"

if ${logic_fail}; then
  overall_pass=false
  echo -e "${RED}Some LOGIC tests failed. Fix your paging implementation or STATS printing.${NC}"
else
  echo -e "${GREEN}All LOGIC tests (based on [STATS]) passed.${NC}"
fi

# ----------------------------------------------------------
# Final summary
# ----------------------------------------------------------
if ${overall_pass}; then
  echo -e "${GREEN}All paging tests completed successfully.${NC}"
else
  echo -e "${RED}Some paging tests FAILED. Check logs above and *.actual files.${NC}"
fi
