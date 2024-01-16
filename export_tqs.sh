qs=("0" "2" "4" "6" "8" "16" "32" "64" "128" "256" "512" "1024")
result_file="result_tqs.log"

echo "TQS变化实验" > ${result_file}
c=50
s=50
for ((i=1; i<=8; i++)); do
    case $i in
        1) 
            x=1
            y=1
            z=1
            r=1:0:0
            u=1.0
            h=false
            k=30000
            ;;
        2)
            x=1
            y=1
            z=1
            r=1:1:0
            u=1.0
            h=false
            k=30000
            ;;
        3)
            x=1
            y=1
            z=1
            r=1:0:0
            u=1.0
            h=true
            k=30000
            ;;
        4)
            x=1
            y=1
            z=1
            r=1:1:0
            u=1.0
            h=true
            k=30000
            ;;
        5) 
            x=0
            y=0
            z=0
            r=1:0:0
            u=0.7
            h=false
            k=150000
            ;;
        6)
            x=0
            y=0
            z=0
            r=1:0:0
            u=0.7
            h=true
            k=150000
            ;;
        7)
            x=1
            y=1
            z=0
            r=1:0:0
            u=0.7
            h=false
            k=150000
            ;;
        8)
            x=1
            y=1
            z=0
            r=1:0:0
            u=0.7
            h=true
            k=150000
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

