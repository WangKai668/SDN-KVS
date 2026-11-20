SDN_KVS_RESULTS_DIR="${SDN_KVS_RESULTS_DIR:-/mnt/nasDisk_ds3617/wangkai/SDN-KVS}"
if [[ ! -d "$SDN_KVS_RESULTS_DIR" ]]; then
    printf '结果目录不存在 %s\n' "$SDN_KVS_RESULTS_DIR" >&2
    exit 1
fi

qs=("999" "0" "2" "4" "6" "8" "16" "32" "64" "128" "256" "512" "1024")
qs=("999" "4" "8" "16" "32" "64" "128" "256" "512" "1024")
qs=("8" "64" "256" "1024")
qs=("4" "8" "16" "32" "64" "128" "256" "512" "1024" "2048")

# qs=( "6" "1024")
qs=("64")

c=32
s=32
for ((i=8; i<=15; i++)); do
    case $i in
        8)  # 轻尾   同构
            x=0
            y=0
            z=0
            r=1:0:0
            u=0.7
            h=true
            k=150000
            ;;
        9)  # 轻尾   异构
            x=0
            y=0
            z=0
            r=1:1:0
            u=0.7
            h=true
            k=150000
            ;;
        10)  # 轻尾   同构+
            x=0
            y=0
            z=0
            r=5:2:1
            u=0.7
            h=true
            k=150000
            ;;    
        11)  # 重尾   同构
            x=1
            y=1
            z=0
            r=1:0:0
            u=0.7
            h=true
            k=150000
            ;;
        12)  # 重尾   异构
            x=1
            y=1
            z=0
            r=1:1:0
            u=0.7
            h=true
            k=150000
            ;;
        13)  # 重尾   异构+
            x=1
            y=1
            z=0
            r=5:2:1
            u=0.7
            h=true
            k=150000
            ;;
        14)  # 大规模场景，异构，  【轻尾】
            x=0
            y=0
            z=0
            r=1:1:0
            u=0.7
            h=true
            k=150000
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
            k=150000
            c=128
            s=128
            ;;
    esac

    (
        if [ $h = true ]; then
            result_file="${SDN_KVS_RESULTS_DIR}/result_tqs_scene[$i]_c${c}_s${s}_k${k}_r${r}_x${x}_y${y}_z${z}_u${u}_h.log"
        else
            result_file="${SDN_KVS_RESULTS_DIR}/result_tqs_scene[$i]_c${c}_s${s}_k${k}_r${r}_x${x}_y${y}_z${z}_u${u}.log"
        fi   
        
        echo "TQS变化实验" > "${result_file}" || exit 1
        for ((a=0; a<=4; a++)); do
            echo "" >> "${result_file}"
            output_file_root="${SDN_KVS_RESULTS_DIR}/output_c${c}_s${s}_k${k}_r${r}_x${x}_y${y}_z${z}_u${u}_a${a}"
            case $a in
                0) echo "[Scene $i] FIFO =============================" >> "${result_file}";;
                1) echo "[Scene $i] SRPT =============================" >> "${result_file}";;
                2) echo "[Scene $i] SBF  =============================" >> "${result_file}";;
                3) echo "[Scene $i] DAS  =============================" >> "${result_file}";;
                4) echo "[Scene $i] SDN-KVS ==========================" >> "${result_file}";;
            esac
            if [ $a = 4 ]; then
                for q in ${qs[@]}; do
                    output_file2="${output_file_root}_q${q}_l600"
                    echo "**********************  q=${q}  ********************** " >> "${result_file}"
                    if [ $h = true ]; then
                        output_file="${output_file2}_h.log"
                        (python3.12 analysis.py "$output_file" >> "${result_file}" )
                    else
                        output_file="${output_file2}.log"
                        (python3.12 analysis.py "$output_file" >> "${result_file}" )
                    fi   
                done
                echo $i, $a, $output_file
            else
                output_file="${output_file_root}_q6_l600"
                if [ $h = true ]; then
                    output_file="${output_file}_h.log"
                    (python3.12 analysis.py "$output_file" >> "${result_file}" )
                else
                    output_file="${output_file}.log"
                    (python3.12 analysis.py "$output_file" >> "${result_file}" )
                fi
                echo $i, $a, $output_file
            fi
        done
    ) &
    sleep 0.5
done

