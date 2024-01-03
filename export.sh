us=("0.7")

echo "小规模场景"
c=15
s=15
k=20000
for ((i=1; i<=8; i++)); do
    case $i in
        1) 
            x=0
            y=1
            r=1:0:0
            q=64
            h=false
            ;;
        2)
            x=0
            y=1
            r=1:0:0
            q=64
            h=true
            ;;
        3)
            x=1
            y=0
            r=1:0:0
            q=64
            h=false
            ;;
        4)
            x=1
            y=0
            r=1:0:0
            q=64
            h=true
            ;;
        5)
            x=0
            y=1
            r=5:3:2
            q=64
            h=false
            ;;
        6)
            x=0
            y=1
            r=5:3:2
            q=64
            h=true
            ;;
        7)
            x=1
            y=0
            r=5:3:2
            q=64
            h=false
            ;;
        8)
            x=1
            y=0
            r=5:3:2
            q=64
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
        for u in ${us[@]}; do
            if [ $h = true ]; then
                bash run.sh -h -c $c -s $s -k $k -a $a -u $u -x $x -y $y -r $r -q $q | grep "Request"
            else
                bash run.sh -c $c -s $s -k $k -a $a -u $u -x $x -y $y -r $r -q $q | grep "Request"
            fi
        done
    done
    echo
done

