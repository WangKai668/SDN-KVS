qs=("4" "8" "16" "32" "64" "128" "256" "512" "1024" "2048")
# qs=("8" "64" "256" "1024")
# qs=( "8" "64")
result_file="result_tqs.log"

echo "TQS变化实验" > ${result_file}
c=32
s=32
k=150000
for ((i=8; i<=15; i++)); do
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
    for ((a=0; a<=4; a++)); do
        case $a in
            0) echo "[Scene $i] FIFO" >> ${result_file};;
            1) echo "[Scene $i] SRPT" >> ${result_file};;
            2) echo "[Scene $i] SBF" >> ${result_file};;
            3) echo "[Scene $i] DAS" >> ${result_file};;
            4) echo "[Scene $i] SDN-KVS" >> ${result_file};;
        esac
        if [ $a = 4 ]; then
            for q in ${qs[@]}; do
                if [ $h = true ]; then
                    (time bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r -q $q >> ${result_file}) &>> ${result_file}
                else
                    (time bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r -q $q >> ${result_file}) &>> ${result_file}
                fi
            done
        else
            if [ $h = true ]; then
                (time bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r >> ${result_file}) &>> ${result_file}
            else
                (time bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -z $z -r $r >> ${result_file}) &>> ${result_file}
            fi
        fi
    done
    echo
done

