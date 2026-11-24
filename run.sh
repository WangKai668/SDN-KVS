SDN_KVS_RESULTS_DIR="${SDN_KVS_RESULTS_DIR:-/mnt/nasDisk_ds3617/wangkai/SDN-KVS}"
if [[ ! -d "$SDN_KVS_RESULTS_DIR" ]]; then
    printf '结果目录不存在 %s\n' "$SDN_KVS_RESULTS_DIR" >&2
    exit 1
fi

# Algorithm(0: FIFO, 1: SRPT, 2: SBF, 3: DAS, 4: SDN_KVS)
algorithm=0
# Enable DEBUG log
debug=false
# Whether there is a hotspot server
ifZipfian=true
# The distribution type for generating key(0: POSSION, 1: PARETO)
distribution_type=0
# The keyset size type(0: LIGHT, 1: HEAVY)
keyset_size_type=0
# The send time type(0: DYNAMIC, 1: STATIC)
send_time_type=0
# Utilization
uti=0.7
# Simulator running time(unit: second)
simTime=1000
# Server rate ratio. (Server rate: number of keys processed per millisecond)
server_rate_ratio=1:0:0
# Server TQS
server_tqs=6
# The number of client node
client_num=50
# The number of server node
server_num=50
# The upper bound of the total number of keys(n keys = 1 keyset)
key_num=150000

tqs_dynamic=0
latency=600

while getopts "a:c:s:k:x:y:z:u:t:dq:r:hl:" arg
do
    case $arg in
        a)
            algorithm=$OPTARG
            ;;
        c)
            client_num=$OPTARG
            ;;
        s)
            server_num=$OPTARG
            ;;
        k)
            key_num=$OPTARG
            ;;
        x)
            distribution_type=$OPTARG
            ;;
        y)
            keyset_size_type=$OPTARG
            ;;
        z)
            send_time_type=$OPTARG
            ;;
        u)
            uti=$OPTARG
            ;;
        t)
            simTime=$OPTARG
            ;;
        d)
            debug=true
            ;;
        q)
            server_tqs=$OPTARG
            ;;
        r)
            server_rate_ratio=$OPTARG
            ;;
        h)
            ifZipfian=true
            ;;
        l)   
            latency=$OPTARG
            ;;    
        ?)
            exit 1
            ;;
    esac
done

echo "================================================"
if [ $send_time_type = 1 ]; then
    uti=1.0
fi
if [ $ifZipfian = true ]; then
    echo "c=${client_num}, s=${server_num}, k=${key_num}, r=${server_rate_ratio}, x=${distribution_type}, y=${keyset_size_type}, z=${send_time_type}, h, u=${uti}, a=${algorithm}, q=${server_tqs}, l${latency}"
else
    echo "c=${client_num}, s=${server_num}, k=${key_num}, r=${server_rate_ratio}, x=${distribution_type}, y=${keyset_size_type}, z=${send_time_type}, u=${uti}, a=${algorithm}, q=${server_tqs}, l${latency}"
fi
echo "latency = ${latency}ns"

if [[ "${SDN_KVS_CONTROLLER_OVERHEAD:-}" != 1 ]]; then
    ./ns3 configure -d optimized 2>&1 >/dev/null
    ./ns3 build > output.log 2>&1
fi

output_file="${SDN_KVS_RESULTS_DIR}/output_c${client_num}_s${server_num}_k${key_num}_r${server_rate_ratio}_x${distribution_type}_y${keyset_size_type}_z${send_time_type}_u${uti}_a${algorithm}_q${server_tqs}_l${latency}"

if [ "$ifZipfian" = true ]; then
    output_file="${output_file}_h"
fi
output_file="${output_file}.log"

handover_prefix=""
if [[ "$algorithm" == 4 && "${SDN_KVS_HANDOVER_STATS:-0}" == 1 ]]; then
    handover_prefix="${output_file%.log}_handover"
fi

if [ "$server_tqs" = "999" ]; then
    tqs_dynamic=1
fi

simulation="scratch/sdn-kvs/sdn-kvs-main --algorithm=${algorithm} --debug=${debug} --ifZipfian=${ifZipfian} --distribution_type=${distribution_type} --keyset_size_type=${keyset_size_type} --send_time_type=${send_time_type} --uti=${uti} --simTime=${simTime} --server_rate_ratio=${server_rate_ratio} --server_tqs=${server_tqs} --client_num=${client_num} --server_num=${server_num} --key_num=${key_num} --tqs_dynamic=${tqs_dynamic} --network_latency=${latency}"
if [[ -n "${SDN_KVS_DATA_QUEUE_PACKETS:-}" ]]; then
    if [[ ! "$SDN_KVS_DATA_QUEUE_PACKETS" =~ ^[0-9]+$ ]]; then
        printf 'Invalid packet count in SDN_KVS_DATA_QUEUE_PACKETS\n' >&2
        exit 2
    fi
    simulation+=" --dataQueuePackets=$SDN_KVS_DATA_QUEUE_PACKETS"
fi
if [[ "${SDN_KVS_DATA_DROP_DIAGNOSTICS:-}" == 1 ]]; then
    simulation+=" --dataDropDiagnostics=1"
fi
if [[ "${SDN_KVS_CONTROLLER_OVERHEAD:-}" == 1 ]]; then
    run_id="${SDN_KVS_OVERHEAD_RUN_ID:?Run ID required in controller overhead mode}"
    output_file="${output_file%.log}_run${run_id}.log"
    if [[ -n "${SDN_KVS_STUDY:-}" ]]; then
        seed="${SDN_KVS_STUDY_SEED:?}"
        rank="${SDN_KVS_STUDY_RANK:-bp-lpt}"
        simulation+=" --RngSeed=$seed --controllerRank=$rank"
        output_file="${output_file%.log}_seed${seed}_${rank}.log"
        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$SDN_KVS_STUDY" "$SDN_KVS_BATCH_SCENE" \
            "$algorithm" "$server_tqs" "$latency" "$run_id" "$seed" "$rank" "${output_file##*/}" \
            >> "$SDN_KVS_RESULTS_DIR/study_manifest.csv" || exit 1
    fi
    if [[ "$algorithm" == 4 && "${SDN_KVS_HANDOVER_STATS:-0}" == 1 ]]; then
        handover_prefix="${output_file%.log}_handover"
    fi
    measure=""
    if [[ "$algorithm" == 4 ]]; then
        measure=1
    fi
    if [[ -n "${SDN_KVS_BATCH_ROOT:-}" ]]; then
        printf '%s,%s,%s,%s,scene_%s/%s\n' "$SDN_KVS_BATCH_SCENE" "$algorithm" "$server_tqs" \
            "$run_id" "$SDN_KVS_BATCH_SCENE" "${output_file##*/}" >> "$SDN_KVS_BATCH_ROOT/run_manifest.csv" || exit 1
    fi
    printf 'SDN_KVS_CONTROLLER_OVERHEAD=%q SDN_KVS_HANDOVER_STATS_PREFIX=%q ./ns3 run --no-build %q\n' \
        "$measure" "$handover_prefix" "$simulation" > "${output_file%.log}.command"
    if [[ -z "$measure" ]]; then
        printf 'env -u SDN_KVS_CONTROLLER_OVERHEAD ./ns3 run --no-build %q\n' \
            "$simulation" > "${output_file%.log}.command"
    fi
    if [[ -n "$measure" ]]; then
        SDN_KVS_HANDOVER_STATS_PREFIX="$handover_prefix" ./ns3 run --no-build "$simulation" > "$output_file" 2>&1
        status=$?
    else
        env -u SDN_KVS_CONTROLLER_OVERHEAD ./ns3 run --no-build "$simulation" > "$output_file" 2>&1
        status=$?
    fi
    printf '%s\n' "$status" > "${output_file%.log}.exit"
    exit "$status"
fi

(
    SDN_KVS_HANDOVER_STATS_PREFIX="$handover_prefix" ./ns3 run "$simulation" > "$output_file" 2>&1
    # if [ $? -eq 0 ]; then
    #     python3 ./analysis.py "$output_file"
    # fi
) &

sleep 0.5
