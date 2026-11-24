"""Plot the per-threshold controller-overhead aggregates as PDF and PNG."""

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def plot_overhead(summary):
    with summary.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    thresholds = [int(row["QT_s"]) for row in rows]
    valid = [row for row in rows if int(row["valid_runs"])]
    x = [thresholds.index(int(row["QT_s"])) for row in valid]

    def metric(name):
        return [float(row[name]) for row in valid]

    plt.rcParams.update({
        "font.family": ["Times New Roman", "Liberation Serif"],
        "font.weight": "bold",
        "axes.labelweight": "bold",
        "pdf.fonttype": 42,
    })
    fig, (time, messages) = plt.subplots(1, 2, figsize=(11.5, 3.5), layout="constrained")
    time.errorbar(
        x, metric("scheduler_total_time_ms_mean"),
        yerr=[float(row["scheduler_total_time_ms_stddev"] or 0) for row in valid],
        fmt="o-", color="#474646", capsize=3, label="Mean ± sample SD",
    )
    signals = metric("overload_signal_count_mean")
    messages.bar(x, signals, color="#a1b6f1", label="Overload signals")
    messages.bar(
        x, metric("flow_mod_count_mean"), bottom=signals,
        color="#474646", label="Flow-table updates",
    )
    time.set_title("(a) Total scheduler execution time (ms)")
    messages.set_title("(b) Total control messages")
    for ax in (time, messages):
        ax.set_xticks(range(len(thresholds)), [str(q) for q in thresholds])
        ax.set_xlabel(r"$QT_s$")
        ax.set_ylim(bottom=0)
        ax.axvline(thresholds.index(64), color="#787878", linestyle="--", linewidth=0.8,
                   zorder=0)
        ax.legend(frameon=False)
    for extension in ("pdf", "png"):
        fig.savefig(summary.with_name(f"controller_overhead.{extension}"), dpi=400)
    plt.close(fig)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path)
    plot_overhead(parser.parse_args().summary)
