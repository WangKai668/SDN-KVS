export SDN_KVS_RESULTS_DIR="${SDN_KVS_RESULTS_DIR:-/mnt/nasDisk_ds3617/wangkai/SDN-KVS}"
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

