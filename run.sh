# Algorithm(0: FIFO, 1: SRPT, 2: SBF, 3: DAS, 4: SDN_KVS)
algorithm=0
# Enable DEBUG log
debug=false
# Only print client application logs for analysis
mute=false
# Whether there is a hotspot server
ifZipfian=true
# The distribution type for generating key(0: POSSION, 1: PARETO)
distribution_type=0
# The keyset size type(0: HEAVY, 1: LIGHT)
keyset_size_type=0
# The send time type(0: HEAVY, 1: LIGHT)
sendtime_type=0
# Utilization
uti=0.7
# Simulator running time(unit: second)
simTime=1000
# Server handle rate. (Number of keys processed per millisecond)
server_rate=1
# Server TQS
server_tqs=64
# The number of client node
client_num=15
# The number of server node
server_num=15
# The upper bound of the total number of keys(n keys = 1 keyset)
key_num=50000

while getopts "a:c:s:k:x:y:z:u:t:dmq:" arg
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
            sendtime_type=$OPTARG
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
        m)
            mute=true
            ;;
        q)
            server_tqs=$OPTARG
            ;;
        ?)
            exit 1
            ;;
    esac
done

./ns3 run "scratch/sdn-kvs/sdn-kvs-main --algorithm=${algorithm} --debug=${debug} --mute=${mute} --ifZipfian=${ifZipfian} --distribution_type=${distribution_type} --keyset_size_type=${keyset_size_type} --sendtime_type=${sendtime_type} --uti=${uti} --simTime=${simTime} --server_rate=${server_rate} --server_tqs=${server_tqs} --client_num=${client_num} --server_num=${server_num} --key_num=${key_num}" > output.log 2>&1
if [ $? -eq 0 ];then
    bash ./analysis_data.sh
fi