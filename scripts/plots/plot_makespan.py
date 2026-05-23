import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

df = pd.read_csv('data/raw/results_raw_sintetico.csv')

schedulers = ['HEFT', 'MOHEFT', 'PEFT', 'IPEFT']
tasks = ['256', '512', '1024', '2048']

means = df.groupby(['heuristica', 'escala'])['makespan'].mean()
makespan = {s: [means[s][int(t)] for t in tasks] for s in schedulers}

plt.style.use('seaborn-v0_8-whitegrid')
x = np.arange(len(schedulers))
width = 0.18
colors = ['#4C72B0', '#55A868', '#C44E52', '#8172B3']

fig, ax = plt.subplots(figsize=(12, 7))
for i, t in enumerate(tasks):
    values = [makespan[s][i] for s in schedulers]
    bars = ax.bar(x + (i - 1.5) * width, values, width,
                  label=f'{t} tasks', color=colors[i], edgecolor='black')
    for bar in bars:
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width() / 2,
                height + max(values) * 0.01,
                f'{height:,.0f}', ha='center', fontsize=30, fontweight='bold',
                bbox=dict(facecolor='white', edgecolor='none', alpha=0.8))

ax.set_xticks(x)
ax.set_xticklabels(schedulers, fontsize=20, fontweight='bold')
ax.set_ylabel('Makespan (seconds)', fontsize=20, fontweight='bold')
ax.set_title('Makespan Comparison', fontsize=20, fontweight='bold')
ax.legend(title='Number of Tasks', fontsize=15, title_fontsize=15)
plt.tight_layout()
plt.savefig('results/figures/makespan_comparison.png', dpi=300)
plt.show()
