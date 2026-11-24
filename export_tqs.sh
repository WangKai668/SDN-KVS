export SDN_KVS_HANDOVER_STATS=1
export SDN_KVS_RESULTS_DIR="${SDN_KVS_RESULTS_DIR:-/mnt/nasDisk_ds3617/wangkai/SDN-KVS}"

qs=("4" "8" "16" "32" "64" "128" "256" "512" "1024" "2048")
# qs=("8" "64" "256" "1024")
qs=("64")
scenes=(14 15)
mode="${1:-}"
if [[ "$mode" == --controller-overhead || "$mode" == --statistics || "$mode" == --study ||
      "$mode" == --central-sbf ]]; then
    if [[ $# != 2 ]]; then
        printf 'Usage: bash export_tqs.sh {--controller-overhead|--statistics|--study|--central-sbf} NEW_RESULTS_DIRECTORY\n' >&2
        exit 2
    fi
    mkdir -- "$2" || exit 1
    export SDN_KVS_RESULTS_DIR
    SDN_KVS_RESULTS_DIR=$(cd -- "$2" && pwd) || exit 1
    export SDN_KVS_CONTROLLER_OVERHEAD=1
    export SDN_KVS_HANDOVER_STATS=0
    scenes=(8)
    if [[ "$mode" == --statistics ]]; then
        scenes=(8 9 10 11 12 13 14 15)
        export SDN_KVS_HANDOVER_STATS=1
        export SDN_KVS_BATCH_ROOT="$SDN_KVS_RESULTS_DIR"
        printf 'scene,algorithm,QT_s,run_id,log_file\n' > "$SDN_KVS_BATCH_ROOT/run_manifest.csv"
        cp -- results_tqs_origin.txt "$SDN_KVS_BATCH_ROOT/reference_current.txt" || exit 1
        cp -- data/results_tqs_origin.txt "$SDN_KVS_BATCH_ROOT/reference_paper.txt" || exit 1
    fi
    if [[ "$mode" == --study || "$mode" == --central-sbf ]]; then
        scenes=(8 9 10 11 12 13)
        if [[ "$mode" == --central-sbf ]]; then
            scenes=(8 9)
            unset SDN_KVS_BATCH_ROOT SDN_KVS_DATA_QUEUE_PACKETS SDN_KVS_DATA_DROP_DIAGNOSTICS
        fi
        printf 'study,scene,algorithm,QT_s,latency_ns,run_id,seed,rank,log_file\n' \
            > "$SDN_KVS_RESULTS_DIR/study_manifest.csv" || exit 1
    fi
    qs=(4 8 16 32 64 128 256 512 1024 2048)
    {
        printf 'Export command: bash export_tqs.sh %q %q\n' "$mode" "$SDN_KVS_RESULTS_DIR"
        printf 'Started: '
        date -Iseconds
        lscpu
        c++ --version
        git rev-parse HEAD
        git -C contrib/ofswitch13 rev-parse HEAD
        cat VERSION
        printf 'Build profile: optimized\n'
    } > "$SDN_KVS_RESULTS_DIR/environment.txt"
    git diff HEAD > "$SDN_KVS_RESULTS_DIR/workspace.diff"
    git -C contrib/ofswitch13 diff HEAD > "$SDN_KVS_RESULTS_DIR/ofswitch13.diff"
    sha256sum scratch/sdn-kvs/controller-overhead.h > "$SDN_KVS_RESULTS_DIR/additional-source.sha256"
    if ! {
        CCACHE_DISABLE=1 ./ns3 configure -d optimized &&
        CCACHE_DISABLE=1 ./ns3 build scratch/sdn-kvs/sdn-kvs-main -j 4
    } > "$SDN_KVS_RESULTS_DIR/build.log" 2>&1; then
        printf 'Build failed. See %s/build.log\n' "$SDN_KVS_RESULTS_DIR" >&2
        exit 1
    fi
    export SDN_KVS_RESULTS_DIR
fi
if [[ ! -d "$SDN_KVS_RESULTS_DIR" ]]; then
    printf '结果目录不存在 %s\n' "$SDN_KVS_RESULTS_DIR" >&2
    exit 1
fi

result_file="${SDN_KVS_RESULTS_DIR}/result_tqs.log"

echo "TQS变化实验" > "${result_file}" || exit 1
c=32
s=32
k=150000
for i in "${scenes[@]}"; do
    case $i in
        8)  # 轻尾   同构
            x=0
            y=0
            z=0
            r=1:0:0
            u=0.7
            h=true
            ;;
        9)  # 轻尾   异构
            x=0
            y=0
            z=0
            r=1:1:0
            u=0.7
            h=true
            ;;
        10)  # 轻尾   同构+
            x=0
            y=0
            z=0
            r=5:2:1
            u=0.7
            h=true
            ;;    
        11)  # 重尾   同构
            x=1
            y=1
            z=0
            r=1:0:0
            u=0.7
            h=true
            ;;
        12)  # 重尾   异构
            x=1
            y=1
            z=0
            r=1:1:0
            u=0.7
            h=true
            ;;
        13)  # 重尾   异构+
            x=1
            y=1
            z=0
            r=5:2:1
            u=0.7
            h=true
            ;;
        14)  # 大规模场景，异构，  【轻尾】
            x=0
            y=0
            z=0
            r=1:1:0
            u=0.7
            h=true
            c=128
            s=128
            ;;
        15)  # 大规模场景，异构，  【重尾】
            x=1
            y=1
            z=0
            r=1:1:0
            u=0.7
            h=true
            c=128
            s=128
            ;;
    esac
    if [[ "$mode" == --study || "$mode" == --central-sbf ]]; then
        export SDN_KVS_BATCH_SCENE="$i"
        scene_qs=(64)
        if [[ "$i" == 8 ]]; then scene_qs=("${qs[@]}"); fi
        for run_id in 1 2 3 4 5; do
            export SDN_KVS_OVERHEAD_RUN_ID="$run_id"
            export SDN_KVS_STUDY_SEED="$((1700 + run_id))"
            if [[ "$mode" == --central-sbf ]]; then
                export SDN_KVS_STUDY=central-sbf
                for rank in bp-lpt central-sbf; do
                    export SDN_KVS_STUDY_RANK="$rank"
                    printf '[Scene %s] q=64 rank=%s run=%s\n' "$i" "$rank" "$run_id" | tee -a "$result_file"
                    bash run.sh -h -c "$c" -s "$s" -k "$k" -a 4 -u "$u" -x "$x" -y "$y" \
                        -z "$z" -r "$r" -q 64 >> "$result_file" 2>&1 || exit 1
                done
                continue
            fi
            export SDN_KVS_STUDY_RANK=bp-lpt
            export SDN_KVS_STUDY=tqs
            for a in 0 1 2 3; do
                printf '[Scene %s] algorithm=%s run=%s\n' "$i" "$a" "$run_id" | tee -a "$result_file"
                bash run.sh -h -c "$c" -s "$s" -k "$k" -a "$a" -u "$u" -x "$x" -y "$y" \
                    -z "$z" -r "$r" >> "$result_file" 2>&1 ||
                    printf 'Failed scene=%s algorithm=%s run=%s\n' "$i" "$a" "$run_id" >> "$result_file"
            done
            for q in "${scene_qs[@]}"; do
                printf '[Scene %s] q=%s run=%s\n' "$i" "$q" "$run_id" | tee -a "$result_file"
                bash run.sh -h -c "$c" -s "$s" -k "$k" -a 4 -u "$u" -x "$x" -y "$y" \
                    -z "$z" -r "$r" -q "$q" >> "$result_file" 2>&1 ||
                    printf 'Failed scene=%s q=%s run=%s\n' "$i" "$q" "$run_id" >> "$result_file"
            done
            if [[ "$i" == 8 ]]; then
                export SDN_KVS_STUDY=ablation
                for rank in srpt sbf; do
                    export SDN_KVS_STUDY_RANK="$rank"
                    printf '[Scene 8] q=64 rank=%s run=%s\n' "$rank" "$run_id" | tee -a "$result_file"
                    bash run.sh -h -c "$c" -s "$s" -k "$k" -a 4 -u "$u" -x "$x" -y "$y" \
                        -z "$z" -r "$r" -q 64 >> "$result_file" 2>&1 ||
                        printf 'Failed rank=%s run=%s\n' "$rank" "$run_id" >> "$result_file"
                done
            fi
        done
        continue
    fi
    if [[ "${SDN_KVS_CONTROLLER_OVERHEAD:-}" == 1 ]]; then
        scene_qs=("${qs[@]}")
        if [[ "$mode" == --statistics ]]; then
            export SDN_KVS_BATCH_SCENE="$i"
            export SDN_KVS_RESULTS_DIR="$SDN_KVS_BATCH_ROOT/scene_$i"
            mkdir -- "$SDN_KVS_RESULTS_DIR" || exit 1
            if [[ "$i" != 8 ]]; then
                scene_qs=(64)
            fi
            for a in 0 1 2 3; do
                export SDN_KVS_OVERHEAD_RUN_ID=1
                printf '[Scene %d] algorithm=%d q=6 run=1\n' "$i" "$a" | tee -a "$result_file"
                bash run.sh -h -c "$c" -s "$s" -k "$k" -a "$a" -u "$u" -x "$x" -y "$y" \
                    -z "$z" -r "$r" >> "$result_file" 2>&1 ||
                    printf 'Run failed for scene=%s algorithm=%s\n' "$i" "$a" >> "$result_file"
            done
        fi
        for q in "${scene_qs[@]}"; do
            for run_id in 1 2 3; do
                export SDN_KVS_OVERHEAD_RUN_ID="$run_id"
                printf '[Scene %d] q=%d run=%d\n' "$i" "$q" "$run_id" | tee -a "$result_file"
                bash run.sh -h -c "$c" -s "$s" -k "$k" -a 4 -u "$u" -x "$x" -y "$y" \
                    -z "$z" -r "$r" -q "$q" >> "$result_file" 2>&1 ||
                    printf 'Run failed for q=%s run=%s\n' "$q" "$run_id" >> "$result_file"
            done
        done
        continue
    fi
    for ((a=0; a<=4; a++)); do
        case $a in
            0) echo "[Scene $i] FIFO" >> "${result_file}";;
            1) echo "[Scene $i] SRPT" >> "${result_file}";;
            2) echo "[Scene $i] SBF" >> "${result_file}";;
            3) echo "[Scene $i] DAS" >> "${result_file}";;
            4) echo "[Scene $i] SDN-KVS" >> "${result_file}";;
        esac
        if [ $a = 4 ]; then
            for q in ${qs[@]}; do
                if [ $h = true ]; then
                    (time bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r -q $q >> "${result_file}") &>> "${result_file}"
                else
                    (time bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r -q $q >> "${result_file}") &>> "${result_file}"
                fi
            done
        else
            if [ $h = true ]; then
                (time bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r >> "${result_file}") &>> "${result_file}"
            else
                (time bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r >> "${result_file}") &>> "${result_file}"
            fi
        fi
    done
    echo
done

