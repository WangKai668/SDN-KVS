us=("0.4" "0.5" "0.6" "0.7" "0.8" "0.9" "1")
result_file="result_uti.log"

echo "小规模场景" > ${result_file}
c=10
s=10
k=150000
for ((i=1; i<=8; i++)); do
    case $i in
        1) 
            x=0
            y=0
            r=1:0:0
            q=64
            h=false
            ;;
        2)
            x=0
            y=0
            r=1:0:0
            q=64
            h=true
            ;;
        3)
            x=1
            y=1
            r=1:0:0
            q=64
            h=false
            ;;
        4)
            x=1
            y=1
            r=1:0:0
            q=64
            h=true
            ;;
        5)
            x=0
            y=0
            r=5:3:2
            q=64
            h=false
            ;;
        6)
            x=0
            y=0
            r=5:3:2
            q=64
            h=true
            ;;
        7)
            x=1
            y=1
            r=5:3:2
            q=64
            h=false
            ;;
        8)
            x=1
            y=1
            r=5:3:2
            q=64
            h=true
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
        for u in ${us[@]}; do
            if [ $h = true ]; then
                (time bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -r $r -q $q >> ${result_file}) &>> ${result_file}
            else
                (time bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -r $r -q $q >> ${result_file}) &>> ${result_file}
            fi
        done
    done
    echo
done

