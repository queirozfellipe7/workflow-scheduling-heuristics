import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

df = pd.read_csv('data/raw/results_raw_sintetico.csv')

schedulers = ['HEFT', 'MOHEFT', 'PEFT', 'IPEFT']
tasks = ['256', '512', '1024', '2048']

means = df.groupby(['heuristica', 'escala'])['energia_kwh'].mean()
energy = {s: [means[s][int(t)] for t in tasks] for s in schedulers}

plt.style.use('seaborn-v0_8-whitegrid')
x = np.arange(len(schedulers))
width = 0.18
colors = ['#4C72B0', '#55A868', '#C44E52', '#8172B3']

fig, ax = plt.subplots(figsize=(12, 7))
for i, t in enumerate(tasks):
    values = [energy[s][i] for s in schedulers]
    bars = ax.bar(x + (i - 1.5) * width, values, width,
                  label=f'{t} tasks', color=colors[i], edgecolor='black')
    for bar in bars:
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width() / 2,
                height + max(values) * 0.02,
                f'{height:.3f}', ha='center', fontsize=30, fontweight='bold',
                bbox=dict(facecolor='white', edgecolor='none', alpha=0.8))

ax.set_xticks(x)
ax.set_xticklabels(schedulers, fontsize=30, fontweight='bold')
ax.set_ylabel('Energy Consumption (kWh)', fontsize=30, fontweight='bold')
ax.legend(title='Number of Tasks', fontsize=16, title_fontsize=15,
          loc='upper left', bbox_to_anchor=(0, 0.9))
plt.tight_layout()
plt.savefig('results/figures/energy_comparison.png', dpi=300)
plt.show()
