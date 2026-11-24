"""Validate and aggregate the controller-overhead mode of analyze_tqs.sh."""

import argparse
from collections import Counter
import csv
import hashlib
import math
import re
from pathlib import Path
from statistics import mean, stdev
import sys

import numpy as np

from analysis import pattern1, pattern2, pattern5, pattern6, pattern7, pattern8, pattern11
from plot_controller_overhead import plot_overhead

THRESHOLDS = (4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048)
REPETITIONS = (1, 2, 3)
PREFIX = "output_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_a4"
RECORD_PREFIX = "CONTROLLER_OVERHEAD "
COUNTS = (
    "scheduler_call_count",
    "overload_signal_count",
    "flow_mod_redirect_count",
    "flow_mod_restore_count",
)
METRICS = ("scheduler_total_time_ms", *COUNTS, "flow_mod_count", "total_control_messages")


def read_run(log, threshold, run_id, algorithm=4, *, collect_study=False):
    row = dict.fromkeys(METRICS)
    row.update(
        QT_s=threshold, run_id=run_id, generated_requests=None, completed_requests=None,
        workload_sha256="", valid_run=False, status="missing_log", mean_ms=None,
        p95_ms=None, p99_ms=None, log_file=log.name,
    )
    if collect_study:
        row.update(rng_seed=None, controller_rank="", server_rates_sha256="",
                   keysets=None, controller_waiting_keysets=None, controller_waiting_ratio=None)
    if not log.is_file():
        return row

    records, totals = [], []
    table, sent, received = {}, {}, {}
    rates, queued, forwarded = {}, {}, {}
    mismatch = False
    with log.open() as stream:
        for line in stream:
            if line.startswith(RECORD_PREFIX):
                records.append(line[len(RECORD_PREFIX):].strip().split(","))
            elif line.startswith("tot_key_num = ") and (match := pattern8.match(line)):
                totals.append(tuple(map(int, match.groups())))
            elif collect_study and line.startswith(" - rng_seed = "):
                row["rng_seed"] = int(line.split("=", 1)[1])
            elif collect_study and line.startswith(" - controller_rank = "):
                row["controller_rank"] = line.split("=", 1)[1].strip()
            elif collect_study and (match := pattern11.match(line)):
                server, rate = map(int, match.group(1, 2))
                rates[server] = rate
            elif collect_study and line.startswith("[Controller]"):
                if match := pattern5.match(line):
                    queued[int(match.group(2))] = float(match.group(1))
                elif match := pattern6.match(line):
                    forwarded[int(match.group(3))] = float(match.group(1))
            elif line.startswith("[Client]"):
                if match := pattern2.match(line):
                    at, source, keyset, request, size = match.group(1, 2, 3, 4, 5)
                    keyset = int(keyset)
                    entry = table.get(keyset)
                    if (keyset in sent or entry is None or
                        (source, request, size) != (entry[2], entry[5], entry[6]) or
                        match.group(12) != entry[3]):
                        mismatch = True
                    sent[keyset] = float(at)
                elif match := pattern7.match(line):
                    at, keyset, request, size = match.group(1, 2, 3, 4)
                    keyset = int(keyset)
                    if keyset == 0:
                        continue
                    entry = table.get(keyset)
                    if keyset in received or entry is None or (request, size) != entry[5:7]:
                        mismatch = True
                    received[keyset] = float(at)
            elif match := pattern1.match(line):
                keyset = int(match.group(1))
                if keyset in table:
                    mismatch = True
                table[keyset] = match.groups()

    planned = {}
    for keyset, entry in table.items():
        planned.setdefault(entry[5], []).append(keyset)
    row["generated_requests"] = len(planned) if table else None
    latencies = []
    for keysets in planned.values():
        if all(key in received and key in sent for key in keysets):
            start = (min(float(table[key][1]) for key in keysets) if algorithm == 3
                     else min(sent[key] for key in keysets))
            latencies.append((max(received[key] for key in keysets) - start) * 1000)
    row["completed_requests"] = len(latencies) if table else None
    if table:
        fingerprint = hashlib.sha256()
        for key in sorted(table):
            fingerprint.update((",".join(table[key]) + "\n").encode("ascii"))
        row["workload_sha256"] = fingerprint.hexdigest()

    if collect_study and rates:
        row["server_rates_sha256"] = hashlib.sha256(
            repr(sorted(rates.items())).encode("ascii")
        ).hexdigest()
    if collect_study and table:
        row["keysets"] = len(table)
        if algorithm == 4:
            waited = sum(forwarded[key] != arrival for key, arrival in queued.items()
                         if key in forwarded)
            row["controller_waiting_keysets"] = waited
            row["controller_waiting_ratio"] = waited / len(table)

    exit_file = log.with_suffix(".exit")
    if not exit_file.is_file() or exit_file.read_text().strip() != "0":
        row["status"] = "missing_or_failed_exit"
        return row
    if mismatch or len(totals) != 1 or totals[0][1:] != (len(planned), len(table)):
        row["status"] = "invalid_events_or_duplicate_run"
        return row
    if not planned or len(latencies) != len(planned):
        row["status"] = "incomplete_requests"
        return row
    row.update(mean_ms=mean(latencies), p95_ms=float(np.percentile(latencies, 95)),
               p99_ms=float(np.percentile(latencies, 99)))
    if algorithm != 4:
        row.update(valid_run=True, status="ok")
        return row
    if len(records) != 1:
        row["status"] = "missing_statistics" if not records else "duplicate_statistics"
        return row
    try:
        q, dynamic, elapsed, calls, overload, redirect, restore, generated = records[0]
        elapsed = float(elapsed)
        q, dynamic, calls, overload, redirect, restore, generated = map(
            int, (q, dynamic, calls, overload, redirect, restore, generated)
        )
    except ValueError:
        row["status"] = "malformed_statistics"
        return row
    if not math.isfinite(elapsed) or min(elapsed, calls, overload, redirect, restore, generated) < 0:
        row["status"] = "malformed_statistics"
        return row
    row.update(
        scheduler_total_time_ms=elapsed, scheduler_call_count=calls,
        overload_signal_count=overload, flow_mod_redirect_count=redirect,
        flow_mod_restore_count=restore, flow_mod_count=redirect + restore,
        total_control_messages=overload + redirect + restore,
    )
    if (q, dynamic) != (threshold, 0):
        row["status"] = "incorrect_threshold"
    elif generated != len(planned):
        row["status"] = "invalid_generated_total"
    else:
        row.update(valid_run=True, status="ok")
    return row


def write_csv(path, rows):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def summarize(runs, thresholds):
    summary = []
    for threshold in thresholds:
        valid = [row for row in runs if row["QT_s"] == threshold and row["valid_run"]]
        row = {"QT_s": threshold, "valid_runs": len(valid), "expected_runs": len(REPETITIONS)}
        for metric in METRICS:
            values = [r[metric] for r in valid]
            row[f"{metric}_mean"] = mean(values) if values else None
            row[f"{metric}_stddev"] = stdev(values) if len(values) > 1 else None
            row[f"{metric}_min"] = min(values) if values else None
            row[f"{metric}_max"] = max(values) if values else None
        summary.append(row)

    return summary


def aggregate(directory):
    runs = [
        read_run(directory / f"{PREFIX}_q{q}_l600_h_run{run}.log", q, run)
        for q in THRESHOLDS for run in REPETITIONS
    ]
    hashes = {row["workload_sha256"] for row in runs if row["valid_run"]}
    if len(hashes) > 1:
        for row in runs:
            if row["valid_run"]:
                row.update(valid_run=False, status="workload_mismatch")
    write_csv(directory / "controller_overhead_runs.csv", runs)

    summary = summarize(runs, THRESHOLDS)

    for row in summary:
        if row["valid_runs"] > 1 and any(
            row[f"{metric}_min"] != row[f"{metric}_max"] for metric in COUNTS[1:]
        ):
            print(f"Control message counts vary at QT_s={row['QT_s']}", file=sys.stderr)

    output = directory / "controller_overhead_summary.csv"
    write_csv(output, summary)
    if any(row["valid_runs"] for row in summary):
        plot_overhead(output)
    print(f"Valid runs: {sum(row['valid_run'] for row in runs)}/{len(runs)}")
    return all(row["valid_run"] for row in runs)


def load_reference(path):
    lines = path.read_text().splitlines()
    return {int(match.group(1)): [
        list(map(float, lines[index + offset].split())) for offset in (1, 2, 3)
    ] for index, line in enumerate(lines) if (match := re.search(r"scene\[(\d+)\]", line))}


def configured_simulation_ms(log):
    with log.open() as stream:
        for line in stream:
            if line.startswith(" - simTime = "):
                return float(line.split("=", 1)[1]) * 1000
            if line.startswith("==== Server information"):
                break
    return None


def handover_metrics(directory, runs):
    result = []
    for scene in range(8, 16):
        for q in THRESHOLDS if scene == 8 else (64,):
            samples = []
            statuses = Counter()
            elapsed = []
            used = 0
            group = [r for r in runs if r["scene"] == scene and r["algorithm"] == 4
                     and r["QT_s"] == q and r["valid_run"]]
            for row in group:
                log = directory / row["log_file"]
                rounds = log.with_name(log.stem + "_handover_rounds.csv")
                end_ms = None
                if rounds.is_file():
                    with rounds.open(newline="") as stream:
                        for record in csv.DictReader(stream):
                            end_ms = float(record["simulation_end_us"]) / 1000
                            statuses[record["status"]] += 1
                            if record["status"] == "valid":
                                samples.append(float(record["handover_idle_us"]))
                    used += 1
                # A run without any handover rows still has a scheduled stop event.
                elapsed.append(end_ms if end_ms is not None else configured_simulation_ms(log))
            simulation_ms = math.fsum(elapsed) if elapsed and None not in elapsed else None
            scheduler_ms = math.fsum(r["scheduler_total_time_ms"] for r in group) if group else None
            idle_ms = math.fsum(samples) / 1000 if samples and used == len(group) else None
            valid = len(samples)
            nonzero = sum(value > 0 for value in samples)
            result.append(dict(
                scene=scene, QT_s=q, handover_runs=used, valid_rounds=valid,
                nonzero_rounds=nonzero, nonzero_fraction=nonzero / valid if valid else None,
                simulation_time_total_ms=simulation_ms,
                scheduler_total_time_sum_ms=scheduler_ms,
                scheduler_to_simulation_pct=100 * scheduler_ms / simulation_ms
                if simulation_ms and scheduler_ms is not None else None,
                handover_idle_total_ms=idle_ms,
                handover_idle_to_simulation_pct=100 * idle_ms / simulation_ms
                if simulation_ms and idle_ms is not None else None,
                excluded_rounds=sum(statuses.values()) - valid,
                unfinished_rounds=statuses["unfinished"], no_forward=statuses["no_forward"],
                no_work_at_drain=statuses["no_work_at_drain"],
                unassociated=statuses["unassociated"],
            ))
    return result


def aggregate_batch(directory):
    with (directory / "run_manifest.csv").open(newline="") as stream:
        manifest = list(csv.DictReader(stream))
    entries = {}
    for record in manifest:
        key = tuple(int(record[field]) for field in ("scene", "algorithm", "QT_s", "run_id"))
        if key in entries:
            raise ValueError(f"Duplicate manifest entry {key}")
        entries[key] = record["log_file"]
    runs = []
    for scene in range(8, 16):
        settings = [(algorithm, 6, 1) for algorithm in range(4)]
        settings += [(4, q, run) for q in (THRESHOLDS if scene == 8 else (64,))
                     for run in REPETITIONS]
        scene_runs = []
        for algorithm, q, run in settings:
            path = entries.get((scene, algorithm, q, run))
            row = read_run(directory / (path or "_missing.log"), q, run, algorithm)
            row.update(scene=scene, algorithm=algorithm, log_file=path or "")
            scene_runs.append(row)
        if len({row["workload_sha256"] for row in scene_runs if row["valid_run"]}) > 1:
            for row in scene_runs:
                if row["valid_run"]:
                    row.update(valid_run=False, status="workload_mismatch")
        runs.extend(scene_runs)
    write_csv(directory / "statistics_runs.csv", runs)

    overhead = []
    for scene in range(8, 16):
        thresholds = THRESHOLDS if scene == 8 else (64,)
        rows = summarize([r for r in runs if r["scene"] == scene and r["algorithm"] == 4],
                         thresholds)
        overhead.extend(dict(scene=scene, **row) for row in rows)
    write_csv(directory / "controller_overhead_summary.csv", overhead)
    plot_rows = [r for r in overhead if r["scene"] == 8]
    plot_input = directory / "scene8_controller_overhead_summary.csv"
    write_csv(plot_input, plot_rows)
    if any(r["valid_runs"] for r in plot_rows):
        plot_overhead(plot_input)
    write_csv(directory / "handover_idle_summary.csv", handover_metrics(directory, runs))

    comparisons = []
    for label in ("current", "paper"):
        reference = load_reference(directory / f"reference_{label}.txt")
        for row in runs:
            scene, algorithm, q = (row[field] for field in ("scene", "algorithm", "QT_s"))
            if scene not in reference or (algorithm == 4 and q != 64):
                continue
            column = (1, 2, 3, 4, 0)[algorithm]
            for index, metric in enumerate(("mean_ms", "p95_ms", "p99_ms")):
                expected = reference[scene][index][column]
                measured = round(row[metric], 4) if row["valid_run"] else None
                comparisons.append(dict(
                    reference=label, scene=scene, algorithm=algorithm, QT_s=q,
                    run_id=row["run_id"], metric=metric, reference_ms=expected,
                    measured_ms=measured, delta_ms=round(measured - expected, 4)
                    if measured is not None else None,
                    matches=measured == expected if measured is not None else None,
                    comparable_configuration=label == "current",
                ))
    write_csv(directory / "performance_comparison.csv", comparisons)
    valid = sum(row["valid_run"] for row in runs)
    print(f"Valid runs: {valid}/{len(runs)}")
    return valid == len(runs)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--statistics", action="store_true")
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    analyze = aggregate_batch if args.statistics else aggregate
    sys.exit(0 if analyze(args.directory) else 1)
