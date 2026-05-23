# Workflow Scheduling Heuristics: A Comparative Study

> Comparative evaluation of scheduling heuristics (**HEFT, MOHEFT, PEFT, IPEFT**) for scientific workflows (PolyBench), simulated via **WRENCH/SimGrid**, analyzing makespan, energy consumption (kWh), and carbon footprint (gCO₂eq) across workflow scales of 256, 512, 1024, and 2048 tasks.

---

## 📁 Repository Structure

```
.
├── simulators/
│   ├── CarbonPolywrench_HEFT/        # WRENCH simulator — HEFT heuristic
│   │   ├── src/                      # C++ source files (Simulator.cpp, Controller.cpp, ...)
│   │   ├── include/                  # Headers (.h, json.hpp)
│   │   ├── data/                     # Platform configuration (platform.xml)
│   │   ├── CMakeModules/             # CMake find modules (FindWRENCH, FindSimGrid, ...)
│   │   └── CMakeLists.txt
│   ├── CarbonPolywrench_MOHEFT/      # WRENCH simulator — MOHEFT heuristic
│   ├── CarbonPolywrench_PEFT/        # WRENCH simulator — PEFT heuristic
│   └── CarbonPolywrench_IPEFT/       # WRENCH simulator — IPEFT heuristic
│
├── workflows/
│   └── polybench/
│       ├── base_workflows/           # Individual PolyBench workflow JSONs (2mm, 3mm, lu, ...)
│       ├── gerador_workflow_3.py     # Script to generate combined trial workflows
│       └── trials/
│           ├── trials_256/           # 10 trial JSONs for scale 256
│           ├── trials_512/           # 10 trial JSONs for scale 512
│           ├── trials_1024/          # 10 trial JSONs for scale 1024
│           └── trials_2048/          # 10 trial JSONs for scale 2048
│
├── data/
│   ├── raw/                          # Raw CSV results from simulations (never edited)
│   └── processed/                    # Aggregated stats.
│
├── scripts/
│   ├── analysis/
│   │   └── compute_stats.py          # Computes mean ± std table from raw CSV
│   └── plots/
│       ├── plot_makespan.py          # Bar chart — Makespan comparison
│       ├── plot_energy.py            # Bar chart — Energy consumption
│       └── plot_carbon.py            # Bar chart — Carbon footprint
│
├── results/
│   ├── figures/                      # Generated plots (PNG, 300 dpi)
│   └── tables/                       # Generated table
│
├── notebooks/                        # Colab notebooks for exploration
├── docs/                             # Extra notes
├── run_experiments.sh                # Shell script to run all simulations
├── .gitignore
├── requirements.txt
└── README.md
```

---

## 🔬 Heuristics Evaluated

| Heuristic | Description |
|-----------|-------------|
| **HEFT**   | Heterogeneous Earliest Finish Time |
| **MOHEFT** | Multi-Objective HEFT |
| **PEFT**   | Predict Earliest Finish Time |
| **IPEFT**  | Improved PEFT |

---

## 📊 Metrics

| Metric | Unit | Description |
|--------|------|-------------|
| Makespan | seconds | Total workflow execution time |
| Energy | kWh | Total energy consumed |
| Carbon footprint | gCO₂eq | Estimated CO₂ emissions |

Experiments: **4 heuristics × 4 scales × 10 trials = 160 runs**

---

## ⚙️ Dependencies

### C++ Simulators (WRENCH)
- [WRENCH](https://wrench-project.org/) ≥ 2.x
- [SimGrid](https://simgrid.org/) ≥ 3.30
- CMake ≥ 3.10
- C++17 compiler (g++ or clang++)

### Python Analysis
```bash
pip install -r requirements.txt
```

---

## 🚀 Reproducing the Results

### 1. Clone the repository
```bash
git clone https://github.com/<your-username>/workflow-scheduling-heuristics.git
cd workflow-scheduling-heuristics
```

### 2. Build a simulator (example: HEFT)
```bash
cd simulators/CarbonPolywrench_HEFT
mkdir build && cd build
cmake ..
make
```

### 3. Run all experiments
```bash
bash run_experiments.sh
```

### 4. Analyse results
```bash
python scripts/analysis/compute_stats.py
```

### 5. Generate figures
```bash
python scripts/plots/plot_makespan.py
python scripts/plots/plot_energy.py
python scripts/plots/plot_carbon.py
```

---

## 📄 Citation

If you use this code or data, please cite:

```bibtex
@article{yourname2025,
  title   = {Comparative Evaluation of Workflow Scheduling Heuristics},
  author  = {Your Name},
  journal = {Journal Name},
  year    = {2025}
}
```

---

## 📬 Contact

**Author:** Your Name — your@email.com  
**Institution:** Your University
