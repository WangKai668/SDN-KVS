"""Generate figures for the seeded SDN-KVS study from aggregate rows."""

from math import nan, sqrt

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

LABELS = (("mean_ms", "Mean"), ("p95_ms", "P95"), ("p99_ms", "P99"))
COLORS = ("#474646", "#a1b6f1", "#649166", "#a86d4a", "#735a9e")


def save(fig, directory, name):
    fig.savefig(directory / (name + ".pdf"))
    fig.savefig(directory / (name + ".png"), dpi=350)
    plt.close(fig)


def scores(rows, metric):
    return [row[metric + "_mean"] if row[metric + "_mean"] is not None else nan for row in rows]


def deviations(rows, metric):
    return [sqrt(row[metric + "_variance"]) if row[metric + "_variance"] is not None else 0
            for row in rows]


def plot_latency(directory, rows, sdn_only):
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.1), layout="constrained")
    for ax, (metric, title) in zip(axes, LABELS):
        for algorithm in ((4,) if sdn_only else range(5)):
            group = sorted((r for r in rows if r["algorithm"] == algorithm),
                           key=lambda row: row["latency_ns"])
            ax.plot([r["latency_ns"] / 1000 for r in group], scores(group, metric),
                    "o-", markersize=3, color=COLORS[algorithm], label=group[0]["algorithm_name"])
        ax.set_xscale("log")
        ax.set_title(f"{title} request response")
        ax.set_xlabel("Link delay (µs)")
        ax.set_ylabel("Response time (ms)")
    axes[-1].legend(frameon=False)
    save(fig, directory, "latency_sdn_kvs" if sdn_only else "latency_all_algorithms")


def plot_figure7(directory, rows):
    rows = sorted(rows, key=lambda r: r["QT_s"])
    fig, ax = plt.subplots(figsize=(7.2, 3.5), layout="constrained")
    right = ax.twinx()
    x = range(len(rows))
    perf = ax.errorbar(x, scores(rows, "mean_ms"), deviations(rows, "mean_ms"),
                       fmt="s-", capsize=3, color=COLORS[0], label="Scheduling performance")
    ratio = right.errorbar(x, scores(rows, "controller_waiting_ratio"),
                           deviations(rows, "controller_waiting_ratio"),
                           fmt="o--", capsize=3, color=COLORS[1], label="Scheduling overhead")
    ax.set_xticks(list(x), [r["QT_s"] for r in rows])
    ax.set_xlabel(r"$QT_s$")
    ax.set_ylabel("Mean response time (ms)")
    right.set_ylabel("Ratio of controller-scheduled keysets")
    ax.legend([perf, ratio], [perf.get_label(), ratio.get_label()],
              title="Mean ± sample SD", frameon=False)
    save(fig, directory, "figure7_seeded")


def plot_ablation(directory, rows):
    names = {"bp-lpt": "BP-LPT", "srpt": "Controller SRPT", "sbf": "Controller SBF"}
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.2), layout="constrained")
    for ax, (metric, title) in zip(axes, LABELS):
        ax.bar([names[r["rank"]] for r in rows], scores(rows, metric),
               yerr=deviations(rows, metric), capsize=3, color=COLORS[:len(rows)])
        ax.set_title(f"{title} request response")
        ax.set_ylabel("Response time (ms)")
    fig.suptitle(r"Controller ranking at $QT_s=64$ (mean ± sample SD)")
    save(fig, directory, "controller_ablation")


def plot_central_sbf(directory, rows):
    with plt.rc_context({
        "font.family": "serif", "font.serif": ["Times New Roman", "Liberation Serif"],
        "font.size": 14, "font.weight": "bold", "axes.labelweight": "bold", "pdf.fonttype": 42,
    }):
        fig, axes = plt.subplots(1, 2, figsize=(8.6, 4.1), sharey=True, layout="constrained")
        x = range(len(LABELS))
        width = 0.34
        by_rank = {(r["scene"], r["rank"]): r for r in rows}
        styles = (("bp-lpt", COLORS[0], ""), ("central-sbf", COLORS[1], "//"))
        for ax, (scene, caption) in zip(axes, ((8, "Homogeneous"), (9, "Heterogeneous"))):
            for index, (rank, color, hatch) in enumerate(styles):
                row = by_rank[scene, rank]
                ax.bar([i + (index - 0.5) * width for i in x],
                       [row[metric + "_mean"] for metric, _ in LABELS],
                       width, color=color, edgecolor=COLORS[0], linewidth=0.65,
                       hatch=hatch, label=row["algorithm_name"])
            ax.set_xticks(x, [label for _, label in LABELS])
            ax.set_xlabel(caption)
            ax.set_axisbelow(True)
            ax.grid(axis="y", linestyle=":", linewidth=0.65)
        axes[0].set_ylabel("Request Response Time (ms)")
        handles, labels = axes[0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="outside upper center", ncol=2, frameon=False)
        save(fig, directory, "central_sbf_ablation")


def make_plots(directory, *, latency=None, figure7=None, ablation=None):
    plt.rcParams.update({"font.family": ["Times New Roman", "Liberation Serif"],
                         "pdf.fonttype": 42})
    if latency and any(r["valid_runs"] for r in latency):
        plot_latency(directory, latency, False)
        plot_latency(directory, latency, True)
    if figure7 and any(r["valid_runs"] for r in figure7):
        plot_figure7(directory, figure7)
    if ablation and any(r["valid_runs"] for r in ablation):
        plot_ablation(directory, ablation)
