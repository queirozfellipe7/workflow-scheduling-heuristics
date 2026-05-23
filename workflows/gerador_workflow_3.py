import json
import os
import random
import copy
import argparse
import math
import statistics

# ==============================
# CARREGAR WORKFLOWS BASE
# ==============================
def load_workflows(folder):
    workflows = []
    for f in os.listdir(folder):
        if f.endswith(".json") and not f.startswith("large_"):
            with open(os.path.join(folder, f), "r") as file:
                workflows.append(json.load(file))
    if not workflows:
        raise ValueError(f"Nenhum workflow base encontrado em: {folder}")
    return workflows


# ==============================
# NORMALIZAÇÃO DE ESCALA
# ==============================
FLOPS_TARGET_MIN = 1e9    # 1 GFLOP  — tarefa leve
FLOPS_TARGET_MAX = 1e13   # 10 TFLOP — tarefa pesada
MEM_TARGET_MIN   = 1e6    # 1 MB
MEM_TARGET_MAX   = 1e10   # 10 GB
FILE_TARGET_MIN  = 1e4    # 10 KB
FILE_TARGET_MAX  = 1e9    # 1 GB


def log_normalize(value, target_min, target_max):
    if value <= 0:
        return int(target_min)
    log_val = math.log10(value)
    log_min = math.log10(target_min)
    log_max = math.log10(target_max)
    clamped = max(log_min, min(log_max, log_val))
    return int(10 ** clamped)


# ==============================
# VARIAÇÃO DE CUSTO
# ==============================
def vary_job_costs(job, flops_var=0.20, mem_var=0.20, rng=None):
    if rng is None:
        rng = random
    if "flops" in job:
        factor = rng.uniform(1 - flops_var, 1 + flops_var)
        raw = job["flops"] * factor
        job["flops"] = max(int(FLOPS_TARGET_MIN), int(raw))
    if "memory" in job:
        factor = rng.uniform(1 - mem_var, 1 + mem_var)
        raw = job["memory"] * factor
        job["memory"] = max(int(MEM_TARGET_MIN), int(raw))


def vary_file_size(f, size_var=0.25, rng=None):
    if rng is None:
        rng = random
    factor = rng.uniform(1 - size_var, 1 + size_var)
    raw = f["size"] * factor
    f["size"] = max(int(FILE_TARGET_MIN), int(raw))


# ==============================
# NORMALIZAR WORKFLOW BASE
# ==============================
def normalize_base_workflow(wf):
    wf = copy.deepcopy(wf)
    for job in wf["jobs"]:
        if "flops" in job:
            job["flops"] = log_normalize(job["flops"], FLOPS_TARGET_MIN, FLOPS_TARGET_MAX)
        if "memory" in job:
            job["memory"] = log_normalize(job["memory"], MEM_TARGET_MIN, MEM_TARGET_MAX)
    for f in wf.get("files", []):
        if "size" in f:
            f["size"] = log_normalize(f["size"], FILE_TARGET_MIN, FILE_TARGET_MAX)
    return wf


# ==============================
# RENOMEAR + VARIAR WORKFLOW
# ==============================
PROFILES = [
    {"flops_scale": 1.3, "mem_scale": 0.9,  "label": "compute-heavy"},
    {"flops_scale": 0.9, "mem_scale": 1.3,  "label": "memory-heavy"},
    {"flops_scale": 1.0, "mem_scale": 1.0,  "label": "balanced"},
]


def rename_workflow(wf, prefix, rng):
    wf = copy.deepcopy(wf)

    job_map  = {}
    file_map = {}
    profile  = rng.choice(PROFILES)

    for job in wf["jobs"]:
        old_id = job["id"]
        new_id = f"{prefix}_{old_id}"
        job_map[old_id] = new_id
        job["id"] = new_id
        job["_profile"] = profile["label"]

        if "runtime" in job:
            vary_job_costs(job, flops_var=0.20, mem_var=0.20, rng=rng)
            if "flops" in job:
                job["flops"] = int(job["flops"] * profile["flops_scale"])
            if "memory" in job:
                job["memory"] = int(job["memory"] * profile["mem_scale"])

    for f in wf.get("files", []):
        old_id = f["id"]
        new_id = f"{prefix}_{old_id}"
        file_map[old_id] = new_id
        f["id"] = new_id
        vary_file_size(f, size_var=0.25, rng=rng)

    for job in wf["jobs"]:
        for field in ("uses", "file_reads", "file_writes"):
            for item in job.get(field, []):
                if item["id"] in file_map:
                    item["id"] = file_map[item["id"]]

    for dep in wf.get("dependencies", []):
        dep["parents"]  = [job_map[p] for p in dep["parents"]  if p in job_map]
        dep["children"] = [job_map[c] for c in dep["children"] if c in job_map]

    return wf


# ==============================
# ÚLTIMO JOB COMPUTACIONAL
# ==============================
def get_last_compute_job(wf):
    for job in reversed(wf["jobs"]):
        if "runtime" in job:
            return job["id"]
    return wf["jobs"][-1]["id"]


# ==============================
# ENTRY JOBS DO BLOCO
# (jobs que não são filhos de nenhuma dependência interna)
# ==============================
def get_entry_jobs(block):
    all_children = {
        c for d in block.get("dependencies", []) for c in d["children"]
    }
    return [j["id"] for j in block["jobs"] if j["id"] not in all_children]


# ==============================
# VALIDAÇÃO DO DAG
# ==============================
def validate_workflow(wf, verbose=False):
    """
    Verifica: sem IDs duplicados, referências válidas,
    sem ciclos (topological sort), distribuição de FLOPs razoável.
    """
    import collections
    warnings = []
    jobs  = wf["jobs"]
    files = wf.get("files", [])

    job_ids  = [j["id"] for j in jobs]
    file_ids = {f["id"] for f in files}
    job_id_set = set(job_ids)

    # IDs duplicados
    dup_jobs = [jid for jid, cnt in
                collections.Counter(job_ids).items() if cnt > 1]
    if dup_jobs:
        warnings.append(f"IDs de job duplicados: {dup_jobs[:5]}")

    # Referências quebradas
    for job in jobs:
        for field in ("file_reads", "file_writes", "uses"):
            for item in job.get(field, []):
                if item["id"] not in file_ids:
                    warnings.append(f"Referência de arquivo inexistente: {item['id']}")

    # Detecção de ciclos via Kahn (topological sort)
    in_degree  = collections.defaultdict(int)
    adj        = collections.defaultdict(list)
    for dep in wf.get("dependencies", []):
        for parent in dep["parents"]:
            for child in dep["children"]:
                if parent in job_id_set and child in job_id_set:
                    adj[parent].append(child)
                    in_degree[child] += 1
    queue   = collections.deque(jid for jid in job_ids if in_degree[jid] == 0)
    visited = 0
    while queue:
        node = queue.popleft()
        visited += 1
        for neighbor in adj[node]:
            in_degree[neighbor] -= 1
            if in_degree[neighbor] == 0:
                queue.append(neighbor)
    if visited != len(job_ids):
        cycle_count = len(job_ids) - visited
        warnings.append(f"CICLO DETECTADO: {cycle_count} job(s) em dependência circular")

    # Distribuição de FLOPs
    flops = [j["flops"] for j in jobs if "flops" in j]
    if flops:
        cv = statistics.stdev(flops) / statistics.mean(flops) * 100 if len(flops) > 1 else 0
        if cv > 150:
            warnings.append(
                f"CV de FLOPs muito alto: {cv:.0f}% "
                f"(min={min(flops):.2e}, max={max(flops):.2e})"
            )

    if verbose:
        compute = [j for j in jobs if "runtime" in j]
        io_only = [j for j in jobs if "runtime" not in j]
        print(f"  Jobs compute : {len(compute)}")
        print(f"  Jobs I/O     : {len(io_only)}")
        if flops:
            print(f"  FLOPs CV     : {cv:.1f}%")
            print(f"  FLOPs range  : {min(flops):.2e} – {max(flops):.2e}")

    return warnings


# ==============================
# GERAR WORKFLOW GRANDE
# ==============================
# Estrutura: cadeias paralelas independentes
#
#   Lane 0:  bloco0 → bloco{P} → bloco{2P} → ...
#   Lane 1:  bloco1 → bloco{P+1} → ...
#   ...
#   Lane P-1: bloco{P-1} → ...
#
# Onde P = platform_size * oversub_factor (grau de paralelismo).
# Cada lane cresce de forma puramente linear (sem cruzamentos),
# eliminando qualquer possibilidade de ciclo inter-blocos.
# ==============================
def generate_large_workflow(
    folder,
    target_tasks=1024,
    platform_size=32,
    oversub_factor=2,
    seed=42,
    verbose=False,
):
    rng = random.Random(seed)

    base_wfs_raw = load_workflows(folder)
    base_wfs     = [normalize_base_workflow(wf) for wf in base_wfs_raw]

    big_wf = {
        "name": f"wf_{target_tasks}_tasks_{platform_size}_cores",
        "seed": seed,
        "jobs": [],
        "files": [],
        "dependencies": [],
    }

    parallelism = platform_size * oversub_factor

    # tail_job[lane] = ID do último job computacional da lane
    # None significa que a lane ainda não tem nenhum bloco
    tail_job = [None] * parallelism

    total_jobs = 0
    exec_id    = 0

    while total_jobs < target_tasks:

        base  = rng.choice(base_wfs)
        block = rename_workflow(base, f"exec{exec_id}", rng)

        num_jobs_block = len(block["jobs"])
        if total_jobs + num_jobs_block > target_tasks:
            break

        # Seleciona a lane com menos blocos (round-robin simples)
        lane = exec_id % parallelism

        entry_jobs = get_entry_jobs(block)
        last_job   = get_last_compute_job(block)

        # Conecta linearmente ao tail da mesma lane — SEM cruzamentos
        if tail_job[lane] is not None:
            big_wf["dependencies"].append({
                "parents":  [tail_job[lane]],
                "children": entry_jobs,
            })

        tail_job[lane] = last_job

        big_wf["jobs"].extend(block["jobs"])
        big_wf["files"].extend(block["files"])
        big_wf["dependencies"].extend(block.get("dependencies", []))

        total_jobs += num_jobs_block
        exec_id    += 1

    # Validação
    issues = validate_workflow(big_wf, verbose=verbose)

    if verbose:
        print("===================================")
        print(f"Tasks alvo    : {target_tasks}")
        print(f"Tasks geradas : {total_jobs}")
        print(f"Blocos usados : {exec_id}")
        print(f"Seed          : {seed}")
        print(f"Plataforma    : {platform_size} cores")
        print(f"Paralelismo   : {parallelism} lanes")
        if issues:
            print(f"AVISOS ({len(issues)}):")
            for w in issues:
                print(f"  ⚠ {w}")
        else:
            print("Validação: OK (sem ciclos)")
        print("===================================")

    return big_wf


# ==============================
# GERAÇÃO EM LOTE
# ==============================
def generate_batch(
    folder,
    target_tasks,
    platform_size,
    oversub_factor,
    n_trials=30,
    base_seed=1000,
    output_dir=".",
    verbose=False,
):
    os.makedirs(output_dir, exist_ok=True)
    generated = []

    for i in range(n_trials):
        seed = base_seed + i
        wf   = generate_large_workflow(
            folder=folder,
            target_tasks=target_tasks,
            platform_size=platform_size,
            oversub_factor=oversub_factor,
            seed=seed,
            verbose=False,
        )
        fname = f"wf_{target_tasks}_tasks_{platform_size}_cores_trial{i+1:02d}.json"
        fpath = os.path.join(output_dir, fname)
        with open(fpath, "w") as f:
            json.dump(wf, f, indent=2)
        generated.append(fpath)

        if verbose or (i + 1) % 5 == 0:
            n_jobs    = len(wf["jobs"])
            n_compute = sum(1 for j in wf["jobs"] if "runtime" in j)
            print(f"  Trial {i+1:02d}/{n_trials} | seed={seed} | "
                  f"jobs={n_jobs} (compute={n_compute}) → {fname}")

    return generated


# ==============================
# MAIN
# ==============================
if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        description="Gerador de workflows sintéticos PolyBench para avaliação de schedulers."
    )
    parser.add_argument("--folder",   default="./data",
                        help="Pasta com os workflows base PolyBench")
    parser.add_argument("--tasks",    type=int, default=1024,
                        choices=[256, 512, 1024, 2048])
    parser.add_argument("--platform", type=int, default=32,
                        choices=[8, 16, 32, 64])
    parser.add_argument("--oversub",  type=int, default=2)
    parser.add_argument("--seed",     type=int, default=42)
    parser.add_argument("--verbose",  action="store_true")

    parser.add_argument("--batch",      action="store_true",
                        help="Gerar N trials com seeds distintas")
    parser.add_argument("--n-trials",   type=int, default=30)
    parser.add_argument("--base-seed",  type=int, default=1000)
    parser.add_argument("--output-dir", default="./workflows_trials")

    args = parser.parse_args()

    if args.batch:
        print(f"Gerando {args.n_trials} trials para {args.tasks} tasks...")
        files = generate_batch(
            folder=args.folder,
            target_tasks=args.tasks,
            platform_size=args.platform,
            oversub_factor=args.oversub,
            n_trials=args.n_trials,
            base_seed=args.base_seed,
            output_dir=args.output_dir,
            verbose=True,
        )
        print(f"\nConcluído. {len(files)} arquivos em: {args.output_dir}")

    else:
        workflow = generate_large_workflow(
            folder=args.folder,
            target_tasks=args.tasks,
            platform_size=args.platform,
            oversub_factor=args.oversub,
            seed=args.seed,
            verbose=args.verbose,
        )
        output_name = f"wf_{args.tasks}_tasks_{args.platform}_cores.json"
        with open(output_name, "w") as f:
            json.dump(workflow, f, indent=2)
        print(f"Arquivo gerado: {output_name}")
