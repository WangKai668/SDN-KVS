latency=("600" "1000" "2500" "5000" "10000" "20000" "40000" "50000" "100000" "500000" "1000000")

c=32
s=32
for ((i=1; i<=1; i++)); do
    case $i in
        1) 
            x=0
            y=0
            z=0
            u=0.7
            r=1:0:0
            h=true
            k=150000
            q=64
            ;;
    esac
    

     for l in ${latency[@]}; do
        (
            if [ $h = true ]; then
                result_file="result_util_scene[$i]_c${c}_s${s}_k${k}_r${r}_x${x}_y${y}_z${z}_u${u}_q${q}_l${l}_h.log"
            else
                result_file="result_util_scene[$i]_c${c}_s${s}_k${k}_r${r}_x${x}_y${y}_z${z}_u${u}_q${q}_l${l}.log"
            fi   
            
            echo "Util变化实验" > ${result_file}
            for ((a=0; a<=4; a++)); do
                echo "" >> ${result_file}

                output_file_root="results/output_c${c}_s${s}_k${k}_r${r}_x${x}_y${y}_z${z}_u${u}_a${a}"

                case $a in
                    0) echo "[Scene $i] FIFO =============================" >> ${result_file};;
                    1) echo "[Scene $i] SRPT =============================" >> ${result_file};;
                    2) echo "[Scene $i] SBF  =============================" >> ${result_file};;
                    3) echo "[Scene $i] DAS  =============================" >> ${result_file};;
                    4) echo "[Scene $i] SDN-KVS ==========================" >> ${result_file};;
                esac

                if [ $a = 4 ]; then
                    output_file2="${output_file_root}_q${q}_l${l}"
                    echo "**********************  q=${q}  ********************** " >> ${result_file}
                    if [ $h = true ]; then
                        output_file="${output_file2}_h.log"
                        (python3 analysis.py $output_file >> ${result_file} ) 
                    else
                        output_file="${output_file2}.log"
                        (python3 analysis.py $output_file >> ${result_file} ) 
                    fi   
                echo $i, $a, $output_file
                else
                    output_file="${output_file_root}_q6_l${l}"
                    if [ $h = true ]; then
                        output_file="${output_file}_h.log"
                        (python3 analysis.py $output_file >> ${result_file} ) 
                    else
                        output_file="${output_file}.log"
                        (python3 analysis.py $output_file >> ${result_file} ) 
                    fi
                    echo $i, $a, $output_file
                fi
                
                echo $i, $u, $output_file
            done
        ) &
        sleep 0.5
    done
done

