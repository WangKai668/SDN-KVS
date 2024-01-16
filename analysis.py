import os
import re
import sys
import numpy as np
from matplotlib import pyplot as plt

pattern1 = re.compile(r"\[[0-9]+\] \(id=(\d+), st=(.*?), src_client=(\d+), dst_server=(\d+), dst_ip=(\d+\.\d+\.\d+\.\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_size=(\d+), is_heavy=(\d+)\)")
pattern2 = re.compile(r"\[Client\]\[\+(.*?)s\] Client (\d+) Sent SDN-KVS packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\) to server (\d+)\(ip=(\d+\.\d+\.\d+\.\d+), port=(\d+)\)")
pattern3 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+)\(queue_siz=(\d+), update_time=\+(\d+[\.]{0,1}\d*)s\) received SDN-KVS packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\) from (\d+\.\d+\.\d+\.\d+) port (\d+)")
pattern4 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+)\(queue_siz=(\d+), update_time=\+(\d+[\.]{0,1}\d*)s\) sent response packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\) to (\d+\.\d+\.\d+\.\d+) port (\d+)")
pattern5 = re.compile(r"\[Controller\]\[\+(.*?)s\] Schedule client packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\)\(ctrl_queue_siz=(\d+)\)")
pattern6 = re.compile(r"\[Controller\]\[\+(.*?)s\] Forward packet\(pkt_id=(\d+)\)\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\) to the server (\d+) \(ctrl_queue_siz=(\d+)\)")
pattern7 = re.compile(r"\[Client\]\[\+(.*?)s\] Received SDN-KVS packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\) from (\d+\.\d+\.\d+\.\d+) port (\d+)")
pattern8 = re.compile(r"tot_key_num = (\d+), tot_request_num = (\d+), tot_ks_num = (\d+)")
pattern9 = re.compile(r" - algorithm = ([A-Z-]+)\(server_algorithm: ([A-Z_]+), client_algorithm: ([A-Z]+), controller_algorithm: ([A-Z_]+)\)")
pattern10 = re.compile(r"\[Controller\]\[\+(.*?)s\] Do BP-LPT for array\(size=(.*?), tot_key_num=(.*?), ctrl_queue_siz=(.*?)\): \[")
pattern11 = re.compile(r"\[\d+\] \(server_id=(\d+), server_rate=(\d+), tqs=(\d+), srpt_enabled=(\d+), sbf_enabled=(\d+)\)")

algorithm: str

req_st_send_time: dict[int, float] = {}
ks_st_send_time: dict[int, float] = {}
client_send_time: dict[int, float] = {}
client_recv_time: dict[int, float] = {}
ctrl_recv_time: dict[int, float] = {}
ctrl_send_time: dict[int, float] = {}
server_recv_time: dict[int, float] = {}
server_send_time: dict[int, float] = {}
server_rate: dict[int, float] = {}
server_serve_time: dict[int, float] = {}
server_tot_time: dict[int, float] = {}
req_send_time: dict[int, float] = {}
req_recv_time: dict[int, float] = {}

req_response_time: dict[int, float] = {}
req_st_send_time_dt: list[float] = []
req_avg_response_time: float
req_p99: np.floating
req_p95: np.floating

heavy_ks_dst_server_cnt: dict[int, int] = {}
ks_dst_server: dict[int, int] = {}
ks_response_time: dict[int, float] = {}
ks_ctrl_wait_time: dict[int, float] = {}
ks_avg_response_time: float
ks_p99: np.floating
ks_p95: np.floating
ks_avg_ctrl_wait_time: float
ks_ctrl_wait_time_p95: np.floating
ks_ctrl_wait_time_p99: np.floating
ks_ctrl_wait_cnt: int

client_send_cnt: int = 0
client_recv_cnt: int = 0
ctrl_recv_cnt: int = 0
ctrl_send_cnt: int = 0
server_recv_cnt: int = 0
server_send_cnt: int = 0

tot_key_num: int = 0
tot_request_num: int = 0
tot_ks_num: int = 0

req_siz: dict[int, int] = {}
dst_server_cnt: dict[int, int] = {}

ctrl_queue_size = {}
all_server_queue_size = {}

def print_result():
    print("Request: avg = {:.4f}ms, p95 = {:.4f}ms, p99 = {:.4f}ms".format(req_avg_response_time, req_p95, req_p99))
    print("Keyset: avg = {:.4f}ms, p95 = {:.4f}ms, p99 = {:.4f}ms".format(ks_avg_response_time, ks_p95, ks_p99))
    
    if algorithm == "SDN-KVS":
        print("Keyset wait in controller: avg = {:.4f}ms, p95 = {:.4f}ms, p99 = {:.4f}ms, wait_cnt = {}" \
              .format(ks_avg_ctrl_wait_time, ks_ctrl_wait_time_p95, ks_ctrl_wait_time_p99, ks_ctrl_wait_cnt))
        # Draw keyset wait time in controller
        ks_ctrl_wait_time_sorted = sorted(list(ks_ctrl_wait_time.values()))
        mx = int(ks_ctrl_wait_time_sorted[len(ks_ctrl_wait_time_sorted) - 1])
        count, bins_count = np.histogram(ks_ctrl_wait_time_sorted, bins=mx if mx != 0 else 1)
        pdf = count / sum(count)
        cdf = np.cumsum(pdf)
        plt.xlabel("Ratio")
        plt.ylabel("Time(ms)")
        plt.plot(cdf, bins_count[1:], label="Wait time in controller")
        plt.legend()
        plt.savefig('sdn_kvs.png')
        plt.clf()
    
    req_siz_list = list(req_siz.values())
    plt.hist(req_siz_list, bins = list(range(min(req_siz_list), max(req_siz_list) + 1, 1)))
    plt.xlabel("Request size")
    plt.ylabel("Count")
    plt.title("req_siz") 
    plt.savefig('req_siz.png')
    plt.clf()

    server_num = len(dst_server_cnt)
    dst_server_x = list(dst_server_cnt.keys())
    dst_server_y = list(dst_server_cnt.values())
    plt.bar(dst_server_x, dst_server_y)
    plt.xlabel("Server")
    plt.ylabel("Count")
    plt.title("dst_server") 
    plt.savefig('dst_server.png')
    plt.clf()
    
    server_uti: dict[int, float] = {}
    for i in range(0, server_num):
        server_uti[i] = server_serve_time[i] / server_tot_time[i]
    server_uti_x = list(server_uti.keys())
    server_uti_y = list(server_uti.values())
    print("Utilization: {}".format(sum(server_uti_y) / len(server_uti_x)))
    plt.bar(server_uti_x, server_uti_y)
    plt.xlabel("Server")
    plt.ylabel("Uti")
    plt.title("server_uti")
    plt.savefig("server_uti.png")
    plt.clf()

    for server_id in all_server_queue_size.keys():
        plt.plot(all_server_queue_size[server_id].keys(), all_server_queue_size[server_id].values(), alpha=0.5, label="server {}".format(server_id))
    plt.plot(ctrl_queue_size.keys(), ctrl_queue_size.values(), label="ctrl")
    plt.title('queue size in server for {}'.format(algorithm))
    plt.xlabel('time(ms)')
    plt.ylabel('queue size')
    plt.legend()
    plt.savefig('queue_len.png')
    plt.clf()

    # plt.hist(req_st_send_time_dt, bins = list(np.arange(min(req_st_send_time_dt), max(req_st_send_time_dt) + 0.1, 0.1)))
    plt.hist(req_st_send_time_dt)
    plt.xlabel("Delta send time")
    plt.ylabel("Count")
    plt.title("delta_send_time")
    plt.savefig("delta_send_time.png")
    plt.clf()
    
    heavy_ks_dst_server_x = list(heavy_ks_dst_server_cnt.keys())
    heavy_ks_dst_server_y = list(heavy_ks_dst_server_cnt.values())
    plt.bar(heavy_ks_dst_server_x, heavy_ks_dst_server_y)
    plt.xlabel("Server")
    plt.ylabel("Count")
    plt.title("heavy_ks_dst_server_cnt") 
    plt.savefig('heavy_ks_dst_server_cnt.png')
    plt.clf()

def analyze():
    global req_st_send_time_dt
    global req_avg_response_time
    global req_p99
    global req_p95
    global ks_avg_response_time
    global ks_p99
    global ks_p95
    global ks_avg_ctrl_wait_time
    global ks_ctrl_wait_time_p99
    global ks_ctrl_wait_time_p95
    global ks_ctrl_wait_cnt
    for req_id in range(1, tot_request_num + 1):
        if algorithm == "DAS":
            req_response_time[req_id] = req_recv_time[req_id] - req_st_send_time[req_id]
        else:
            req_response_time[req_id] = req_recv_time[req_id] - req_send_time[req_id]
    req_response_time_sorted = sorted(list(req_response_time.values()))
    req_avg_response_time = sum(req_response_time_sorted) / tot_request_num
    req_p99 = np.percentile(req_response_time_sorted, 99)
    req_p95 = np.percentile(req_response_time_sorted, 95)
    for id in range(1, tot_ks_num + 1):
        if algorithm == "DAS":
            ks_response_time[id] = client_recv_time[id] - ks_st_send_time[id]
        else:
            ks_response_time[id] = client_recv_time[id] - client_send_time[id]
        ks_ctrl_wait_time[id] = ctrl_send_time[id] - ctrl_recv_time[id]
    ks_response_time_sorted = sorted(list(ks_response_time.values()))
    ks_avg_response_time = sum(ks_response_time_sorted) / tot_ks_num
    ks_p99 = np.percentile(ks_response_time_sorted, 99)
    ks_p95 = np.percentile(ks_response_time_sorted, 95)
    ks_ctrl_wait_time_sorted = sorted(list(ks_ctrl_wait_time.values()))
    ks_ctrl_wait_time_sorted_unique = [item for item in ks_ctrl_wait_time_sorted if item != 0]
    ks_ctrl_wait_cnt = len(ks_ctrl_wait_time_sorted_unique)
    ks_avg_ctrl_wait_time = sum(ks_ctrl_wait_time_sorted) / len(ks_ctrl_wait_time_sorted)
    ks_ctrl_wait_time_p99 = np.percentile(ks_ctrl_wait_time_sorted, 99)
    ks_ctrl_wait_time_p95 = np.percentile(ks_ctrl_wait_time_sorted, 95)

    for req_id in range(2, tot_request_num + 1):
        req_st_send_time_dt.append(req_st_send_time[req_id] - req_st_send_time[req_id - 1])

    server_min_recv_time: dict[int, float] = {}
    server_max_send_time: dict[int, float] = {}
    for id, recv_time in server_recv_time.items():
        dst_server = ks_dst_server[id]
        send_time = server_send_time[id]
        if dst_server not in server_min_recv_time:
            server_min_recv_time[dst_server] = recv_time
        else:
            server_min_recv_time[dst_server] = min(server_min_recv_time[dst_server], recv_time)
        if dst_server not in server_max_send_time:
            server_max_send_time[dst_server] = send_time
        else:
            server_max_send_time[dst_server] = max(server_max_send_time[dst_server], send_time)
    for server_id, recv_time in server_min_recv_time.items():
        send_time = server_max_send_time[server_id]
        server_serve_time[server_id] = dst_server_cnt[server_id] / server_rate[server_id]
        server_tot_time[server_id] = round(send_time - recv_time, 0)

def parse_line(line: str):
    global algorithm
    global client_send_cnt
    global client_recv_cnt
    global ctrl_recv_cnt
    global ctrl_send_cnt
    global server_recv_cnt
    global server_send_cnt
    global tot_key_num
    global tot_request_num
    global tot_ks_num
    global req_siz
    global dst_server_cnt
    global heavy_ks_dst_server_cnt
    match = pattern1.search(line)
    if match:
        id = int(match.group(1))
        st = float(match.group(2)) * 1000
        dst_server = int(match.group(4))
        req_id = int(match.group(6))
        ks_siz = int(match.group(7))
        is_heavy = int(match.group(9))
        req_st_send_time[req_id] = st
        ks_st_send_time[id] = st
        ctrl_recv_time[id] = 0
        ctrl_send_time[id] = 0
        req_recv_time[req_id] = 0
        ks_dst_server[id] = dst_server
        if dst_server not in server_serve_time:
            server_serve_time[dst_server] = 0
        if dst_server not in dst_server_cnt:
            dst_server_cnt[dst_server] = 0
        dst_server_cnt[dst_server] += ks_siz
        if req_id not in req_siz:
            req_siz[req_id] = 0
        req_siz[req_id] += ks_siz
        if dst_server not in heavy_ks_dst_server_cnt:
            heavy_ks_dst_server_cnt[dst_server] = 0
        if is_heavy == 1:
            heavy_ks_dst_server_cnt[dst_server] += 1
        return
    match = pattern2.search(line)
    if match:
        time = float(match.group(1)) * 1000
        id = int(match.group(3))
        req_id = int(match.group(4))
        client_send_time[id] = time
        client_send_cnt += 1
        req_send_time[req_id] = time
        return
    match = pattern3.search(line)
    if match:
        time = float(match.group(1)) * 1000
        server_id = int(match.group(2))
        queue_siz = int(match.group(3))
        id = int(match.group(5))
        server_recv_time[id] = time
        server_recv_cnt += 1
        if server_id not in all_server_queue_size.keys():
            all_server_queue_size[server_id] = {}
        all_server_queue_size[server_id][time] = queue_siz
        return
    match = pattern4.search(line)
    if match:
        time = float(match.group(1)) * 1000
        server_id = int(match.group(2))
        queue_siz = int(match.group(3))
        id = int(match.group(5))
        server_send_time[id] = time
        server_send_cnt += 1
        if server_id not in all_server_queue_size.keys():
            all_server_queue_size[server_id] = {}
        all_server_queue_size[server_id][time] = queue_siz
        return
    match = pattern5.search(line)
    if match:
        time = float(match.group(1)) * 1000
        id = int(match.group(2))
        ctrl_recv_time[id] = time
        ctrl_recv_cnt += 1
        ctrl_queue_size[time] = int(match.group(11))
        return
    match = pattern6.search(line)
    if match:
        time = float(match.group(1)) * 1000
        id = int(match.group(3))
        ctrl_send_time[id] = time
        ctrl_send_cnt += 1
        ctrl_queue_size[time] = int(match.group(13))
        return
    match = pattern7.search(line)
    if match:
        time = float(match.group(1)) * 1000
        id = int(match.group(2))
        req_id = int(match.group(3))
        client_recv_time[id] = time
        client_recv_cnt += 1
        req_recv_time[req_id] = max(req_recv_time[req_id], time)
        return
    match = pattern8.search(line)
    if match:
        tot_key_num = int(match.group(1))
        tot_request_num = int(match.group(2))
        tot_ks_num = int(match.group(3))
        return
    match = pattern9.search(line)
    if match:
        algorithm = match.group(1)
        return
    match = pattern10.search(line)
    if match:
        time = float(match.group(1)) * 1000
        ctrl_queue_size[time] = int(match.group(4))
        return
    match = pattern11.search(line)
    if match:
        id = int(match.group(1))
        rate = float(match.group(2))
        server_rate[id] = rate
        return

def parse(file_path: str):
    if not os.path.exists(file_path):
        print("File not exists!")
        return
    with open(file_path, 'r') as file:
        for line in file:
            parse_line(line)
    if (client_send_cnt != tot_ks_num or client_recv_cnt != tot_ks_num or 
        server_send_cnt != tot_ks_num or server_recv_cnt != tot_ks_num or
        len(req_send_time) != tot_request_num or len(req_recv_time) != tot_request_num):
        print("Error! There is packet loss")
        print(client_send_cnt, client_recv_cnt, server_send_cnt, server_recv_cnt, tot_ks_num)
        print(len(req_send_time), len(req_recv_time), tot_request_num)
        return
    analyze()
    print_result()

parse(sys.argv[1])