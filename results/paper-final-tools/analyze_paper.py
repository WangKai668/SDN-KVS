"""Analyze the paper reruns from raw logs and export publication data."""
import argparse
from collections import Counter, defaultdict
import csv
from decimal import Decimal

from pathlib import Path
from statistics import mean, stdev

from analysis import pattern3, pattern4, pattern5, pattern6
from analyze_controller_overhead import read_run, write_csv

ALGORITHMS = ("FIFO", "SRPT", "SBF", "DAS", "SDN-KVS")
PAPER_ORDER = ("SDN-KVS", "FIFO", "SRPT", "SBF", "DAS")
METRICS = (("mean", "mean_ms"), ("95th", "p95_ms"), ("99th", "p99_ms"))
FIGURES = {8: "fig10", 9: "fig11", 11: "fig12a", 12: "fig12b"}


def read_csv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def analyze_runs(batch):
    rows = []
    for item in read_csv(batch / "run_plan.csv"):
        job = batch / "raw" / item["job"]
        manifests = read_csv(job / "study_manifest.csv") if (job / "study_manifest.csv").is_file() else []
        manifest = manifests[0] if len(manifests) == 1 else {}
        path = job / manifest.get("log_file", "missing.log")
        algorithm = int(item["algorithm"])
        stats = read_run(path, int(item["QT_s"]), int(item["run_id"]), algorithm, collect_study=True)
        stats.update(item, log_file=str(path.relative_to(batch)), algorithm_name=ALGORITHMS[algorithm])
        stats.update(data_drop_trace_count=None, observed_drops=None)
        expected_seed = 1 if item["seed"] == "default" else int(item["seed"])
        if stats["valid_run"]:
            command = path.with_suffix(".command").read_text()
            if (stats["rng_seed"] != expected_seed or stats["controller_rank"] != item["rank"]
                    or ("--RngSeed" in command) != (item["seed"] != "default")
                    or "--RngRun" in command or not stats["server_rates_sha256"]):
                stats.update(valid_run=False, status="configuration_mismatch")
        if stats["valid_run"] and item["group"] == "latency":
            traces = {}
            with path.open() as stream:
                for line in stream:
                    if line.startswith("DATA_DROP "):
                        device, kind, count = line.split()[1].split(",")
                        traces[device, kind] = int(count)
            stats["data_drop_trace_count"] = len(traces)
            stats["observed_drops"] = sum(traces.values())
            if len(traces) != 526 or stats["observed_drops"]:
                stats.update(valid_run=False, status="unverified_or_nonzero_drops")
        rows.append(stats)
    paired = defaultdict(list)
    for row in rows:
        # Compare all default Scene 8 runs across figure groups and all algorithms within each other setting.
        paired[row["scene"], row["seed"], row["utilization"]].append(row)
    for candidates in paired.values():
        fingerprints = {(r["workload_sha256"], r["server_rates_sha256"])
                        for r in candidates if r["valid_run"]}
        if len(fingerprints) > 1:
            for row in candidates:
                if row["valid_run"]:
                    row.update(valid_run=False, status="paired_input_mismatch")
    for scene in FIGURES:
        reference = [r for r in rows if r["group"] == "repeat" and int(r["scene"]) == scene
                     and r["algorithm"] == "4" and r["valid_run"]]
        if len(reference) == 5 and len({r["workload_sha256"] for r in reference}) != 5:
            for row in rows:
                if row["group"] == "repeat" and int(row["scene"]) == scene:
                    row.update(valid_run=False, status="duplicate_workload_across_seeds")
    return rows


def wait_data(log):
    ctrl_in, ctrl_out, server_in, server_out = {}, {}, {}, {}
    with log.open() as stream:
        for line in stream:
            if line.startswith("[Controller]"):
                if event := pattern5.match(line):
                    ctrl_in[int(event[2])] = float(event[1]) * 1000
                elif event := pattern6.match(line):
                    ctrl_out[int(event[3])] = float(event[1]) * 1000
            elif line.startswith("[Server]"):
                if event := pattern3.match(line):
                    server_in[int(event[5])] = float(event[1]) * 1000
                elif event := pattern4.match(line):
                    server_out[int(event[5])] = (float(event[1]) * 1000, int(event[7]) / int(event[10]))
    assert ctrl_in.keys() == ctrl_out.keys()
    assert set(server_in) - {0} == set(server_out) - {0}
    ret = []
    for key in sorted(set(server_in) - {0}):
        end, service = server_out[key]
        ret.append(dict(keyset_id=key,
                        controller_wait_ms=ctrl_out[key] - ctrl_in[key] if key in ctrl_in else None,
                        server_wait_ms=end - server_in[key] - service))
    return ret


def handover_row(batch, row):
    log = batch / row["log_file"]
    rounds = read_csv(log.with_name(log.stem + "_handover_rounds.csv"))
    counts = Counter(r["status"] for r in rounds)
    times = {Decimal(r["simulation_end_us"]) for r in rounds}
    assert len(times) == 1 and next(iter(times)) > 0
    end = next(iter(times))
    idle = [Decimal(r["handover_idle_us"]) for r in rounds if r["status"] == "valid"]
    assert all(t >= 0 for t in idle)
    total = sum(idle, Decimal(0)) if idle else None
    return dict(target_utilization_pct=Decimal(row["utilization"]) * 100,
                handover_idle_total_ms=total/1000 if total is not None else None,
                simulation_time_s=end/1000000,
                share_of_simulation_permyriad=10000*total/end if total is not None else None,
                share_of_simulation_pct=100*total/end if total is not None else None,
                total_rounds=len(rounds), valid_rounds=len(idle), nonzero_rounds=sum(t > 0 for t in idle),
                unfinished_rounds=counts["unfinished"], no_forward_rounds=counts["no_forward"],
                no_work_at_drain_rounds=counts["no_work_at_drain"],
                unassociated_rounds=counts["unassociated"], log_file=row["log_file"])


def write_paper_tables(data, table3, table4):
    def fmt(value, digits):
        return "N/A" if value is None else f"{value:.{digits}f}"

    with (data / "tables_03_04.md").open("w") as stream:
        stream.write("# Paper Tables 3 and 4\n\n"
                     "## Table 3\n\n"
                     "| Controller Policy | Mean (ms) | P95 (ms) | P99 (ms) |\n"
                     "|---|---:|---:|---:|\n")
        for row in table3:
            values = [row["controller_policy"], *(fmt(row[key], 2) for _,key in METRICS)]
            stream.write("| " + " | ".join(values) + " |\n")
        stream.write("\n## Table 4\n\n")
        for index,(label,key,digits) in enumerate((
            ("Target Utilization (%)","target_utilization_pct",0),
            ("Total Handover Idle Time (ms)","handover_idle_total_ms",1),
            ("Share of Simulation Time (‱)","share_of_simulation_permyriad",2),
        )):
            stream.write("| " + " | ".join([label,*(fmt(row[key],digits) for row in table4)])+" |\n")
            if index == 0:
                stream.write("|---|" + "---:|"*len(table4) + "\n")


def paper_datasets(batch, rows, data):
    qscan = sorted((row for row in rows if row["group"] == "qscan"), key=lambda r: int(r["QT_s"]))
    fig7 = [{key: r[key] for key in ("QT_s", "mean_ms", "p95_ms", "p99_ms", "controller_waiting_ratio",
                                    "controller_waiting_keysets", "keysets", "scheduler_total_time_ms",
                                    "scheduler_call_count", "overload_signal_count", "flow_mod_count",
                                    "flow_mod_redirect_count", "flow_mod_restore_count", "total_control_messages",
                                    "log_file")} for r in qscan]
    write_csv(data / "fig07_thresholds.csv", fig7)
    baseline, = [row for row in qscan if row["QT_s"] == "64"]
    fig8 = wait_data(batch / baseline["log_file"])
    assert len(fig8) == baseline["keysets"]
    write_csv(data / "fig08_keyset_wait_ms.csv", fig8)

    delays = sorted((row for row in rows if row["group"] == "latency"), key=lambda r: int(r["latency_ns"]))
    fig9 = [dict(link_latency_us=int(r["latency_ns"])/1000, **{k:r[k] for _,k in METRICS},
                 source_log=r["log_file"]) for r in delays]
    write_csv(data / "fig09_latency_origin.csv", fig9)

    repeated = defaultdict(list)
    for row in rows:
        if row["group"] == "repeat":
            repeated[int(row["scene"]), row["algorithm_name"]].append(row)
    repeats = {}
    for scene, figure in FIGURES.items():
        result = []
        for label, metric in METRICS:
            line = {"metric": label}
            for name in PAPER_ORDER:
                runs = repeated[scene, name]
                assert {int(r["seed"]) for r in runs} == set(range(1701,1706)) and len(runs) == 5
                line[name] = mean(r[metric] for r in runs)
            for name in PAPER_ORDER:
                line[f"{name} SD"] = stdev(r[metric] for r in repeated[scene, name])
            result.append(line)
        repeats[figure] = result
        write_csv(data / f"{figure}_origin_mean_sd_ms.csv", result)

    ranks = {r["rank"]: r for r in rows if r["group"] == "ablation"}
    ranks["bp-lpt"] = baseline
    table3 = [dict(controller_policy=label, **{k:ranks[rank][k] for _,k in METRICS},
                   log_file=ranks[rank]["log_file"])
              for rank,label in (("bp-lpt","SDN-KVS"),("central-sbf","Centralized SBF"),("srpt","Centralized SRPT"))]
    write_csv(data / "table03_ablation.csv", table3)
    utilized = {(Decimal(r["utilization"]), r["algorithm_name"]):r for r in rows if r["group"] == "utilization"}
    load_points = sorted({u for u,_ in utilized})
    fig13 = {}
    for metric in ("mean_ms", "p95_ms", "p99_ms"):
        fig13[metric] = [dict(target_utilization=float(u), **{a:utilized[u,a][metric] for a in PAPER_ORDER})
                         for u in load_points if u < 1]
        write_csv(data / f"fig13_{metric}_origin.csv", fig13[metric])
    table4 = [handover_row(batch, utilized[u,"SDN-KVS"]) for u in load_points]
    write_csv(data / "table04_handover.csv", table4)
    write_paper_tables(data, table3, table4)

    large = {(r["scene"],r["algorithm_name"]):r for r in rows if r["group"] == "large"}
    fig14 = {}
    for scene in ("14","15"):
        normalized, raw = [], []
        for label, metric in METRICS:
            raw.append(dict(metric=label,**{a:large[scene,a][metric] for a in PAPER_ORDER}))
            normalized.append(dict(metric=label,**{a:large[scene,a][metric]/large[scene,"SDN-KVS"][metric] for a in PAPER_ORDER}))
        write_csv(data / f"fig14_scene{scene}_raw_ms.csv", raw)
        write_csv(data / f"fig14_scene{scene}_origin_normalized.csv", normalized)
        fig14[scene] = normalized
    return fig7, fig8, fig9, repeats, fig13, fig14


def write_report(batch, rows):
    groups = defaultdict(list)
    for row in rows:
        groups[row["group"]].append(row)
    for group, members in groups.items():
        with (batch / "data" / f"result_{group}.log").open("w") as stream:
            for row in members:
                context = (f'[Scene {row["scene"]}] {row["algorithm_name"]}'
                           f' q={row["QT_s"]} latency={row["latency_ns"]}ns'
                           f' uti={row["utilization"]} seed={row["seed"]} rank={row["rank"]}')
                stream.write(context + "\n")
                stream.write(f'Requests completed: {row["completed_requests"]}/{row["generated_requests"]}\n')
                if row["valid_run"]:
                    stream.write("Request: avg = {:.4f}ms, p95 = {:.4f}ms, p99 = {:.4f}ms\n"
                                 .format(*(row[metric] for _,metric in METRICS)))
                stream.write(f'Status: {row["status"]}\nRaw log: {row["log_file"]}\n\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    batch = args.directory.resolve()
    data = batch / "data"
    data.mkdir(exist_ok=True)
    rows = analyze_runs(batch)
    write_csv(data / "study_runs.csv", rows)
    write_report(batch, rows)
    print(Counter((r["group"], r["status"]) for r in rows))
    invalid = [r["job"] for r in rows if not r["valid_run"]]
    if len(rows) != 161 or invalid:
        raise SystemExit(f"Expected 161 valid runs; found {len(rows)} planned runs, invalid={invalid}")
    from plot_paper import make_figures, write_origin_workbook
    datasets = paper_datasets(batch, rows, data)
    make_figures(batch, *datasets)
    write_origin_workbook(data, datasets[2], datasets[3], datasets[4], datasets[5])


if __name__ == "__main__":
    main()
