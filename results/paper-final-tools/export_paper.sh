#!/usr/bin/env bash
# Run from the SDN-KVS project root. Each job has its own log and manifest directory.
set -euo pipefail
if [[ $# != 1 ]]; then
    printf 'Usage: bash %s NEW_RESULTS_DIRECTORY\n' "$0" >&2
    exit 2
fi
if [[ ! -f VERSION || ! -f run.sh ]]; then
    printf 'Run from the SDN-KVS project root.\n' >&2
    exit 2
fi
tool_dir=$(cd -- "$(dirname -- "$0")" && pwd)
mkdir -- "$1"
batch=$(cd -- "$1" && pwd)
mkdir "$batch"/{raw,provenance}
cp "$tool_dir/export_paper.sh" "$batch/provenance/"
export SDN_KVS_CONTROLLER_OVERHEAD=1
unset SDN_KVS_BATCH_ROOT SDN_KVS_OVERHEAD_TRACE NS_GLOBAL_VALUE
runner="$batch/provenance/run.paper.sh"
# Keep the normal run.sh execution path and pass --RngSeed only for repeated runs.
sed 's/simulation+=" --RngSeed=\$seed --controllerRank=\$rank"/if [[ "$seed" != default ]]; then simulation+=" --RngSeed=$seed"; fi\n        simulation+=" --controllerRank=$rank"/' run.sh > "$runner"
bash -n "$runner"
rg -Fq 'if [[ "$seed" != default ]]' "$runner"
{
    printf 'Started: '
    date -Iseconds
    lscpu
    c++ --version
    python3.12 --version
    printf 'Project HEAD\n'
    git rev-parse HEAD
    printf 'OFSwitch13 HEAD\n'
    git -C contrib/ofswitch13 rev-parse HEAD
    cat VERSION
    printf 'Build profile: optimized\n'
} > "$batch/provenance/environment.txt"
git diff HEAD > "$batch/provenance/workspace.diff"
git -C contrib/ofswitch13 diff HEAD > "$batch/provenance/ofswitch13.diff"
cp -- export_tqs.sh export_latency.sh export_uti.sh analyze_tqs.sh analyze_latency.sh analyze_util.sh \
    analyze_controller_overhead.py analyze_sdn_kvs_study.py analysis.py plot_controller_overhead.py \
    plot_sdn_kvs_study.py draw_wait_time.py analyze_parseToOriginUtilLatency.py \
    analyze_parseToOriginTqs.py "$batch/provenance/"
tar -czf "$batch/provenance/project-simulation-source.tar.gz" scratch/sdn-kvs
{
    CCACHE_DISABLE=1 ./ns3 configure -d optimized
    CCACHE_DISABLE=1 ./ns3 build scratch/sdn-kvs/sdn-kvs-main -j 4
} > "$batch/provenance/build.log" 2>&1
sha256sum build/scratch/sdn-kvs/ns3.39-sdn-kvs-main-optimized > "$batch/provenance/binary.sha256"

plan="$batch/run_plan.csv"
printf 'group,scene,algorithm,QT_s,latency_ns,utilization,run_id,seed,rank,buffer_packets,job\n' > "$plan"
add_job() {
    local group=$1 scene=$2 algorithm=$3 q=$4 delay=$5 util=$6 run=$7 seed=$8 rank=$9 buffer=${10}
    local job="$group/s${scene}_a${algorithm}_q${q}_l${delay}_u${util}_run${run}_${rank}"
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$group" "$scene" "$algorithm" "$q" "$delay" "$util" "$run" "$seed" "$rank" "$buffer" "$job" >> "$plan"
}
for q in 4 8 16 32 64 128 256 512 1024 2048; do
    add_job qscan 8 4 "$q" 600 0.7 1 default bp-lpt 0
done
# Highest-load SDN-KVS cases start first once the sequential timer scan is finished.
for u in 1 0.9 0.8 0.71 0.6 0.5 0.4; do
    add_job utilization 9 4 64 600 "$u" 1 default bp-lpt 0
done
for delay in 600 1000 2500 5000 10000 20000 40000 50000; do
    add_job latency 8 4 64 "$delay" 0.7 1 default bp-lpt 32768
done
for rank in central-sbf srpt; do
    add_job ablation 8 4 64 600 0.7 1 default "$rank" 0
done
for scene in 14 15; do
    for a in 0 1 2 3 4; do
        q=6
        if [[ "$a" == 4 ]]; then q=64; fi
        add_job large "$scene" "$a" "$q" 600 0.7 1 default bp-lpt 0
    done
done
for scene in 8 9 11 12; do
    for run in 1 2 3 4 5; do
        for a in 0 1 2 3 4; do
            q=6
            if [[ "$a" == 4 ]]; then q=64; fi
            add_job repeat "$scene" "$a" "$q" 600 0.7 "$run" "$((1700+run))" bp-lpt 0
        done
    done
done
for u in 0.4 0.5 0.6 0.71 0.8 0.9; do
    for a in 0 1 2 3; do
        add_job utilization 9 "$a" 6 600 "$u" 1 default bp-lpt 0
    done
done

run_job() {
    local group=$1 scene=$2 algorithm=$3 q=$4 delay=$5 util=$6 run=$7 seed=$8 rank=$9 buffer=${10} job=${11}
    local clients=32 servers=32 dist=0 ratio=1:0:0
    case "$scene" in
        9|12|14|15) ratio=1:1:0 ;;
    esac
    case "$scene" in
        11|12|15) dist=1 ;;
    esac
    case "$scene" in
        14|15) clients=128; servers=128 ;;
    esac
    (
        export SDN_KVS_RESULTS_DIR="$batch/raw/$job"
        export SDN_KVS_STUDY="$group" SDN_KVS_BATCH_SCENE="$scene" SDN_KVS_OVERHEAD_RUN_ID="$run"
        export SDN_KVS_STUDY_SEED="$seed" SDN_KVS_STUDY_RANK="$rank" SDN_KVS_DATA_QUEUE_PACKETS="$buffer"
        export SDN_KVS_HANDOVER_STATS=0 SDN_KVS_DATA_DROP_DIAGNOSTICS=0
        if [[ "$group" == utilization && "$algorithm" == 4 ]]; then SDN_KVS_HANDOVER_STATS=1; fi
        if [[ "$group" == latency ]]; then SDN_KVS_DATA_DROP_DIAGNOSTICS=1; fi
        mkdir -p "$SDN_KVS_RESULTS_DIR"
        printf 'study,scene,algorithm,QT_s,latency_ns,run_id,seed,rank,log_file\n' > "$SDN_KVS_RESULTS_DIR/study_manifest.csv"
        if bash "$runner" -h -c "$clients" -s "$servers" -k 150000 -a "$algorithm" -u "$util" \
                -x "$dist" -y "$dist" -z 0 -r "$ratio" -q "$q" -l "$delay" \
                > "$SDN_KVS_RESULTS_DIR/export.log" 2>&1; then
            printf 'DONE %s\n' "$job"
        else
            printf 'FAILED %s\n' "$job"
        fi
    )
}
# Keep the wall-clock measurements in Figure 7b free of competing runs from this batch.
while IFS=, read -r group scene algorithm q delay util run seed rank buffer job; do
    if [[ "$group" == qscan ]]; then
        run_job "$group" "$scene" "$algorithm" "$q" "$delay" "$util" "$run" "$seed" "$rank" "$buffer" "$job"
    fi
done < "$plan"

workers=${PAPER_SIM_JOBS:-8}
if [[ ! "$workers" =~ ^[1-9][0-9]*$ ]]; then exit 2; fi
active=0
while IFS=, read -r group scene algorithm q delay util run seed rank buffer job; do
    if [[ "$group" == group || "$group" == qscan ]]; then continue; fi
    run_job "$group" "$scene" "$algorithm" "$q" "$delay" "$util" "$run" "$seed" "$rank" "$buffer" "$job" &
    active=$((active + 1))
    if ((active >= workers)); then
        wait -n
        active=$((active - 1))
    fi
done < "$plan"
wait
