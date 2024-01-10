qs=("8" "16" "32" "64" "128" "256" "512" "1024")

echo "TQS变化实验"
c=50
s=50
k=150000
for ((i=1; i<=4; i++)); do
    case $i in
        1) 
            x=0
            y=0
            r=1:0:0
            u=0.7
            h=false
            ;;
        2)
            x=0
            y=0
            r=1:0:0
            u=0.7
            h=true
            ;;
        3)
            x=1
            y=1
            r=1:0:0
            u=0.7
            h=false
            ;;
        4)
            x=1
            y=1
            r=1:0:0
            u=0.7
            h=true
            ;;
    esac
    for ((a=0; a<=4; a++)); do
        case $a in
            0) echo "[Scene $i] FIFO";;
            1) echo "[Scene $i] SRPT";;
            2) echo "[Scene $i] SBF";;
            3) echo "[Scene $i] DAS";;
            4) echo "[Scene $i] SDN-KVS";;
        esac
        if [ $a = 4 ]; then
            for q in ${qs[@]}; do
                if [ $h = true ]; then
                    bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -r $r -q $q
                else
                    bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -r $r -q $q
                fi
            done
        else
            if [ $h = true ]; then
                bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -r $r
            else
                bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -r $r
            fi
        fi
    done
    echo
done

