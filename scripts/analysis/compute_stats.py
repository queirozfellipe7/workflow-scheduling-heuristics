import pandas as pd

df = pd.read_csv('data/raw/results_raw.csv')

stats = df.groupby(['heuristica', 'escala']).agg(
    makespan_mean  = ('makespan',     'mean'),
    makespan_std   = ('makespan',     'std'),
    energia_mean   = ('energia_kwh',  'mean'),
    energia_std    = ('energia_kwh',  'std'),
    carbono_mean   = ('carbono_gco2', 'mean'),
    carbono_std    = ('carbono_gco2', 'std'),
).reset_index()

stats['Makespan (s)']     = stats.apply(lambda r: f"{r.makespan_mean:,.2f} ± {r.makespan_std:.2f}", axis=1)
stats['Energia (kWh)']    = stats.apply(lambda r: f"{r.energia_mean:.3f} ± {r.energia_std:.3f}",    axis=1)
stats['Carbono (gCO₂eq)'] = stats.apply(lambda r: f"{r.carbono_mean:,.1f} ± {r.carbono_std:.1f}",  axis=1)

table = (
    stats[['heuristica', 'escala', 'Makespan (s)', 'Energia (kWh)', 'Carbono (gCO₂eq)']]
    .rename(columns={'heuristica': 'Scheduler', 'escala': 'Tasks'})
    .sort_values(['Scheduler', 'Tasks'])
    .set_index(['Scheduler', 'Tasks'])
)

print(table.to_string())
table.to_csv('data/processed/stats_summary.csv')
print("\nSalvo em data/processed/stats_summary.csv")
