export SDN_KVS_RESULTS_DIR="${SDN_KVS_RESULTS_DIR:-/mnt/nasDisk_ds3617/wangkai/SDN-KVS}"
mode="${1:-}"
if [[ "$mode" == --study || "$mode" == --study-buffered ]]; then
    if [[ $# != 2 ]]; then
        printf 'Usage: bash export_latency.sh {--study|--study-buffered} NEW_RESULTS_DIRECTORY\n' >&2
        exit 2
    fi
    mkdir -- "$2" || exit 1
    SDN_KVS_RESULTS_DIR=$(cd -- "$2" && pwd) || exit 1
    export SDN_KVS_CONTROLLER_OVERHEAD=1 SDN_KVS_HANDOVER_STATS=0
    export SDN_KVS_STUDY=latency SDN_KVS_BATCH_SCENE=1 SDN_KVS_OVERHEAD_RUN_ID=1
    export SDN_KVS_STUDY_SEED=1701 SDN_KVS_STUDY_RANK=bp-lpt
    if [[ "$mode" == --study-buffered ]]; then
        export SDN_KVS_DATA_QUEUE_PACKETS=32768 SDN_KVS_DATA_DROP_DIAGNOSTICS=1
    fi
    printf 'study,scene,algorithm,QT_s,latency_ns,run_id,seed,rank,log_file\n' \
        > "$SDN_KVS_RESULTS_DIR/study_manifest.csv" || exit 1
    {
        lscpu
        c++ --version
        git rev-parse HEAD
        cat VERSION
        printf 'data_queue_packets=%s\ndata_drop_diagnostics=%s\n' \
            "${SDN_KVS_DATA_QUEUE_PACKETS:-0}" "${SDN_KVS_DATA_DROP_DIAGNOSTICS:-0}"
    } > "$SDN_KVS_RESULTS_DIR/environment.txt"
    if ! {
        CCACHE_DISABLE=1 ./ns3 configure -d optimized &&
        CCACHE_DISABLE=1 ./ns3 build scratch/sdn-kvs/sdn-kvs-main -j 4
    } > "$SDN_KVS_RESULTS_DIR/build.log" 2>&1; then
        exit 1
    fi
fi
if [[ ! -d "$SDN_KVS_RESULTS_DIR" ]]; then
    printf '结果目录不存在 %s\n' "$SDN_KVS_RESULTS_DIR" >&2
    exit 1
fi

latency=("600" "1000" "2500" "5000" "10000" "20000" "40000" "50000" "100000" "500000" "1000000")
result_file="${SDN_KVS_RESULTS_DIR}/result_latency.log"

echo "小规模场景" > "${result_file}" || exit 1
c=32
s=32
for ((i=1; i<=1; i++)); do
    case $i in
        1) 
            x=0
            y=0
            z=0
            r=1:0:0
            u=0.7
            h=true
            k=150000
            q=64
            ;;
    esac

    for ((a=0; a<=4; a++)); do
        case $a in
            0) echo "[Scene $i] FIFO" >> "${result_file}";;
            1) echo "[Scene $i] SRPT" >> "${result_file}";;
            2) echo "[Scene $i] SBF" >> "${result_file}";;
            3) echo "[Scene $i] DAS" >> "${result_file}";;
            4) echo "[Scene $i] SDN-KVS" >> "${result_file}";;
        esac

        for l in ${latency[@]}; do
            if [[ "$mode" == --study || "$mode" == --study-buffered ]]; then
                run_q=6
                if [[ "$a" == 4 ]]; then run_q="$q"; fi
                printf '[Scene %s] algorithm=%s delay=%s ns\n' "$i" "$a" "$l" | tee -a "$result_file"
                bash run.sh -h -c "$c" -s "$s" -k "$k" -a "$a" -u "$u" -x "$x" -y "$y" \
                    -z "$z" -r "$r" -q "$run_q" -l "$l" >> "$result_file" 2>&1 ||
                    printf 'Failed algorithm=%s delay=%s\n' "$a" "$l" >> "$result_file"
                continue
            fi
            if [ $a = 4 ]; then
                if [ $h = true ]; then
                    (time bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r -q $q -l $l >> "${result_file}") &>> "${result_file}"
                else 
                    (time bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r -q $q -l $l >> "${result_file}") &>> "${result_file}"
                fi
            else
                if [ $h = true ]; then
                    (time bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r -l $l >> "${result_file}") &>> "${result_file}"
                else
                    (time bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r -l $l >> "${result_file}") &>> "${result_file}"
                fi
            fi
        done
    done
    echo
done

