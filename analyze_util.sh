SDN_KVS_RESULTS_DIR="${SDN_KVS_RESULTS_DIR:-/mnt/nasDisk_ds3617/wangkai/SDN-KVS}"
if [[ ! -d "$SDN_KVS_RESULTS_DIR" ]]; then
    printf '结果目录不存在 %s\n' "$SDN_KVS_RESULTS_DIR" >&2
    exit 1
fi

us=("0.4" "0.5" "0.6" "0.71" "0.8" "0.9" "1")

c=32
s=32
for ((i=2; i<=2; i++)); do
    case $i in
         2) 
            x=0
            y=0
            z=0
            r=1:1:0
            h=true
            k=150000
            q=64
            ;;
    esac
    

    for u in ${us[@]}; do
        (
            if [ $h = true ]; then
                result_file="${SDN_KVS_RESULTS_DIR}/result_util_scene[$i]_c${c}_s${s}_k${k}_r${r}_x${x}_y${y}_z${z}_u${u}_q${q}_h.log"
            else
                result_file="${SDN_KVS_RESULTS_DIR}/result_util_scene[$i]_c${c}_s${s}_k${k}_r${r}_x${x}_y${y}_z${z}_u${u}_q${q}.log"
            fi   
            
            echo "Util变化实验" > "${result_file}" || exit 1
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
                    output_file2="${output_file_root}_q${q}_l600"
                    echo "**********************  q=${q}  ********************** " >> "${result_file}"
                    if [ $h = true ]; then
                        output_file="${output_file2}_h.log"
                        (python3.12 analysis.py "$output_file" >> "${result_file}" )
                    else
                        output_file="${output_file2}.log"
                        (python3.12 analysis.py "$output_file" >> "${result_file}" )
                    fi   
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
                
                echo $i, $u, $output_file
            done
        ) &
        sleep 0.5
    done
done

