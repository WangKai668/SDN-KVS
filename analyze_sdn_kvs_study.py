"""Analyze latency, seeded TQS sensitivity, and paired controller ablations."""

import argparse
from collections import Counter, defaultdict
import csv
from pathlib import Path
from statistics import mean, variance
import sys

from analyze_controller_overhead import THRESHOLDS, read_run, write_csv
from plot_sdn_kvs_study import make_plots, plot_central_sbf

DELAYS = (600, 1000, 2500, 5000, 10000, 20000, 40000, 50000, 100000, 500000, 1000000)
SEEDS = (1701, 1702, 1703, 1704, 1705)
ALGORITHMS = ("FIFO", "SRPT", "SBF", "DAS", "SDN-KVS")
LATENCY_METRICS = ("mean_ms", "p95_ms", "p99_ms")
METRICS = (
    *LATENCY_METRICS, "controller_waiting_ratio", "scheduler_total_time_ms",
    "scheduler_call_count", "overload_signal_count", "flow_mod_redirect_count",
    "flow_mod_restore_count", "flow_mod_count", "total_control_messages",
)
FIELDS = ("study", "scene", "algorithm", "QT_s", "latency_ns", "run_id", "seed", "rank")
CENTRAL_SBF_RANKS = {"bp-lpt": "SDN-KVS", "central-sbf": "Centralized SBF"}


def expected_settings(latency, central_sbf=False):
    if central_sbf:
        for scene in (8, 9):
            for run, seed in enumerate(SEEDS, 1):
                for rank in CENTRAL_SBF_RANKS:
                    yield ("central-sbf", scene, 4, 64, 600, run, seed, rank)
        return
    if latency:
        for algorithm in range(5):
            for delay in DELAYS:
                yield ("latency", 1, algorithm, 64 if algorithm == 4 else 6, delay, 1,
                       SEEDS[0], "bp-lpt")
        return

    for scene in range(8, 14):
        for run, seed in enumerate(SEEDS, 1):
            for algorithm in range(4):
                yield ("tqs", scene, algorithm, 6, 600, run, seed, "bp-lpt")
            for q in THRESHOLDS if scene == 8 else (64,):
                yield ("tqs", scene, 4, q, 600, run, seed, "bp-lpt")
            if scene == 8:
                for rank in ("srpt", "sbf"):
                    yield ("ablation", 8, 4, 64, 600, run, seed, rank)


def summarize(runs, grouping):
    groups = defaultdict(list)
    for row in runs:
        groups[tuple(row[field] for field in grouping)].append(row)
    result = []
    for key, group in groups.items():
        valid = [r for r in group if r["valid_run"]]
        row = dict(zip(grouping, key))
        row.update(expected_runs=len(group), valid_runs=len(valid))
        for metric in METRICS:
            scores = [r[metric] for r in valid if r[metric] is not None]
            row[f"{metric}_mean"] = mean(scores) if scores else None
            row[f"{metric}_variance"] = variance(scores) if len(scores) > 1 else None
        result.append(row)
    return result


def audit_inputs(runs):
    pairs = defaultdict(list)
    for row in runs:
        if row["valid_run"]:
            pairs[row["scene"], row["seed"]].append(row)
    unique = defaultdict(list)
    for (scene, seed), records in pairs.items():
        fingerprints = {(r["workload_sha256"], r["server_rates_sha256"]) for r in records}
        if len(fingerprints) != 1:
            for row in records:
                row.update(valid_run=False, status="paired_input_mismatch")
            continue
        workload, _ = next(iter(fingerprints))
        unique[scene, workload].append((seed, records))
    for records in unique.values():
        if len(records) > 1:
            for _, runs_for_seed in records:
                for row in runs_for_seed:
                    row.update(valid_run=False, status="duplicate_workload_across_seeds")


def ablation_deltas(runs):
    baseline = {r["seed"]: r for r in runs
                if r["scene"] == 8 and r["algorithm"] == 4 and r["QT_s"] == 64
                and r["rank"] == "bp-lpt" and r["valid_run"]}
    rows = []
    for r in runs:
        if r["study"] != "ablation":
            continue
        reference = baseline.get(r["seed"])
        paired = bool(r["valid_run"] and reference)
        row = dict(seed=r["seed"], rank=r["rank"], valid_pair=paired)
        for metric in LATENCY_METRICS:
            row[f"{metric}_delta"] = r[metric] - reference[metric] if paired else None
        rows.append(row)
    return rows


def analyze(directory, central_sbf=False):
    with (directory / "study_manifest.csv").open(newline="") as stream:
        manifest = list(csv.DictReader(stream))
    files = {}
    for r in manifest:
        key = (r["study"], *(int(r[k]) for k in FIELDS[1:-1]), r["rank"])
        if key in files:
            raise ValueError(f"Duplicate experiment in manifest: {key}")
        files[key] = r["log_file"]

    is_latency = any(key[0] == "latency" for key in files)
    expected = list(expected_settings(is_latency, central_sbf))
    if central_sbf and files.keys() - set(expected):
        raise ValueError("Unexpected experiment in Centralized SBF manifest")
    runs = []
    for key in expected:
        meta = dict(zip(FIELDS, key))
        path = files.get(key)
        row = read_run(directory / (path or "_missing.log"), meta["QT_s"],
                       meta["run_id"], meta["algorithm"], collect_study=True)
        row.update(meta, log_file=path or "", algorithm_name=ALGORITHMS[meta["algorithm"]])
        if central_sbf:
            row["algorithm_name"] = CENTRAL_SBF_RANKS[meta["rank"]]
        if row["valid_run"] and (row["rng_seed"] != row["seed"] or
                                 row["controller_rank"] != row["rank"] or
                                 not row["server_rates_sha256"]):
            row.update(valid_run=False, status="unverified_seed_rank_or_rates")
        runs.append(row)
    audit_inputs(runs)
    if central_sbf:
        invalid_pairs = {(r["scene"], r["seed"]) for r in runs if not r["valid_run"]}
        for row in runs:
            if row["valid_run"] and (row["scene"], row["seed"]) in invalid_pairs:
                row.update(valid_run=False, status="invalid_paired_run")
    write_csv(directory / "study_runs.csv", runs)

    if central_sbf:
        grouping = ("scene", "rank", "algorithm_name")
        fields = (*grouping, "expected_runs", "valid_runs",
                  *(f"{metric}_{stat}" for metric in LATENCY_METRICS for stat in ("mean", "variance")))
        summary = [{field: row[field] for field in fields} for row in summarize(runs, grouping)]
        write_csv(directory / "central_sbf_summary.csv", summary)
        if all(row["valid_run"] for row in runs):
            plot_central_sbf(directory, summary)
    elif is_latency:
        summary = summarize(runs, ("algorithm", "algorithm_name", "latency_ns"))
        write_csv(directory / "latency_summary.csv", summary)
        make_plots(directory, latency=summary)
    else:
        tqs = summarize([r for r in runs if r["study"] == "tqs"],
                        ("scene", "algorithm", "algorithm_name", "QT_s"))
        figure7 = [r for r in tqs if r["scene"] == 8 and r["algorithm"] == 4]
        ablation = summarize([r for r in runs if r["scene"] == 8 and r["algorithm"] == 4
                              and r["QT_s"] == 64], ("rank",))
        write_csv(directory / "tqs_summary.csv", tqs)
        write_csv(directory / "figure7_summary.csv", figure7)
        write_csv(directory / "ablation_summary.csv", ablation)
        write_csv(directory / "ablation_paired_deltas.csv", ablation_deltas(runs))
        make_plots(directory, figure7=figure7, ablation=ablation)
    print(f"Run statuses: {dict(Counter(r['status'] for r in runs))}")
    return all(r["valid_run"] for r in runs)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--central-sbf", action="store_true")
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    sys.exit(0 if analyze(args.directory, args.central_sbf) else 1)
