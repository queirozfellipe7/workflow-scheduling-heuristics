# Workflows — PolyBench

Scientific workflows derived from the [PolyBench](https://sourceforge.net/projects/polybench/) benchmark suite, formatted as JSON for WRENCH.

## Structure

```
polybench/
├── base_workflows/          # One JSON per PolyBench kernel
│   ├── 2mm_workflow.json
│   ├── 3mm_workflow.json
│   ├── lu_workflow.json
│   └── ... (24 kernels total)
├── gerador_workflow_3.py    # Combines base workflows into trial files
└── trials/
    ├── trials_256/          # 10 × ~256-task combined workflows
    ├── trials_512/          # 10 × ~512-task combined workflows
    ├── trials_1024/         # 10 × ~1024-task combined workflows
    └── trials_2048/         # 10 × ~2048-task combined workflows
```

## Regenerating trial workflows

```bash
cd workflows/polybench
python gerador_workflow_3.py
```
