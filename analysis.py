import os
import re
import sys
import numpy as np
import matplotlib
matplotlib.use('Agg')  # 在导入 pyplot 之前设置
import matplotlib.pyplot as plt

# =========================
# 正则表达式模式定义，用于解析日志文件中的不同类型的行
# =========================
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
pattern12 = re.compile(r"tot_key_num = (\d+), tot_request_num = (\d+), tot_ks_num = (\d+)")
pattern13 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+) is overloaded, set heavy field to 1\(queue_siz = (\d+), que_avg = (.*?), que_max = (.*?), tqs_dynamic = (.*?), tqs = (\d+), util_avg_long = (.*?), util_avg_short = (.*?)\)")
pattern14 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+) is overloaded, set heavy field to 1\(queue_siz = (\d+), que_avg = (.*?), tqs = (\d+)\)")
pattern15 = re.compile(r"\[Controller\]\[\+(.*?)s\] Schedule completed\(size=\d+\): \[.*?\]")  # 新增调度完成日志的模式
algorithm: str

# =========================
# 全局变量定义
# =========================
# 各种时间戳和计数器，用于记录请求、keyset、客户端、控制器、服务器等各阶段的时间和数量
req_st_send_time: dict[int, float] = {}      # 请求起始发送时间
ks_st_send_time: dict[int, float] = {}       # keyset起始发送时间
client_send_time: dict[int, float] = {}      # 客户端发送时间
client_recv_time: dict[int, float] = {}      # 客户端接收时间
ctrl_recv_time: dict[int, float] = {}        # 控制器接收时间
ctrl_send_time: dict[int, float] = {}        # 控制器发送时间
server_recv_time: dict[int, float] = {}      # 服务器接收时间
server_send_time: dict[int, float] = {}      # 服务器发送时间
server_rate: dict[int, float] = {}           # 服务器速率
server_serve_time: dict[int, float] = {}     # 服务器服务时间
server_tot_time: dict[int, float] = {}       # 服务器总处理时间
req_send_time: dict[int, float] = {}         # 请求发送时间
req_recv_time: dict[int, float] = {}         # 请求接收时间

# 新增变量用于记录tqs_dynamic和queue_avg
tqs_dynamic_values: list[float] = []         # 所有tqs_dynamic值
queue_avg_values: list[float] = []           # 所有queue_avg值
queue_max_values: list[int] = []             # 所有queue_max值
util_avg_short_values: list[float] = []      # 所有短期利用率值
util_avg_long_values: list[float] = []       # 所有长期利用率值
avg_tqs_dynamic: float = 0.0                 # tqs_dynamic平均值
avg_queue_avg: float = 0.0                   # queue_avg平均值
max_queue_max: int = 0                       # queue_max最大值
avg_util_short: float = 0.0                  # 短期利用率平均值
avg_util_long: float = 0.0                   # 长期利用率平均值

# 请求和keyset的响应时间、统计量
req_response_time: dict[int, float] = {}     # 请求响应时间
req_st_send_time_dt: list[float] = []        # 相邻请求发送时间差
req_avg_response_time: float                 # 请求平均响应时间
req_p99: np.floating                         # 请求99分位响应时间
req_p95: np.floating                         # 请求95分位响应时间

heavy_ks_dst_server_cnt: dict[int, int] = {} # 每台服务器收到的heavy keyset数量
ks_dst_server: dict[int, int] = {}           # keyset对应的目标服务器
ks_response_time: dict[int, float] = {}      # keyset响应时间
ks_ctrl_wait_time: dict[int, float] = {}     # keyset在控制器的等待时间
ks_avg_response_time: float                  # keyset平均响应时间
ks_p99: np.floating                          # keyset 99分位响应时间
ks_p95: np.floating                          # keyset 95分位响应时间
ks_avg_ctrl_wait_time: float                 # keyset在控制器平均等待时间
ks_ctrl_wait_time_p95: np.floating           # keyset在控制器95分位等待时间
ks_ctrl_wait_time_p99: np.floating           # keyset在控制器99分位等待时间
ks_ctrl_wait_cnt: int                        # keyset在控制器有等待的数量

# 各阶段计数器
client_send_cnt: int = 0
client_recv_cnt: int = 0
ctrl_recv_cnt: int = 0
ctrl_send_cnt: int = 0
server_recv_cnt: int = 0
server_send_cnt: int = 0

# 总量统计
tot_key_num: int = 0
tot_request_num: int = 0
tot_ks_num: int = 0

req_siz: dict[int, int] = {}                 # 每个请求的keyset数量
dst_server_cnt: dict[int, int] = {}          # 每台服务器收到的keyset总数

ctrl_queue_size = {}                         # 控制器队列长度随时间变化
all_server_queue_size = {}                   # 所有服务器队列长度随时间变化

# =========================
# 结果打印与可视化
# =========================
def print_result():
    # 打印基本统计信息
    avg_req_size = tot_key_num / tot_request_num if tot_request_num > 0 else 0
    avg_ks_size = tot_key_num / tot_ks_num if tot_ks_num > 0 else 0
    print("tot_key_num = {}, tot_request_num = {}, tot_ks_num = {}, avg_req_size = {:.4f}, avg_ks_size = {:.4f}".format(
        tot_key_num, tot_request_num, tot_ks_num, avg_req_size, avg_ks_size))
    
    if not req_response_time:
        print("Warning: No valid request response times found!")
        return
        
    req_response_time_sorted = sorted(list(req_response_time.values()))
    req_avg_response_time = sum(req_response_time_sorted) / len(req_response_time_sorted)
    req_p99 = np.percentile(req_response_time_sorted, 99)
    req_p95 = np.percentile(req_response_time_sorted, 95)
    print("Request: avg = {:.4f}ms, p95 = {:.4f}ms, p99 = {:.4f}ms".format(req_avg_response_time, req_p95, req_p99))
    
    # 计算并打印tqs_dynamic和queue_avg的平均值
    if tqs_dynamic_values:
        avg_tqs_dynamic = sum(tqs_dynamic_values) / len(tqs_dynamic_values)
        print("TQS Dynamic: avg = {:.4f}".format(avg_tqs_dynamic))
    if queue_avg_values:
        avg_queue_avg = sum(queue_avg_values) / len(queue_avg_values)
        print("Queue Average: avg = {:.4f}".format(avg_queue_avg))
    if queue_max_values:
        max_queue_max = max(queue_max_values)
        print("Queue Maximum: max = {}".format(max_queue_max))
    if util_avg_short_values:
        avg_util_short = sum(util_avg_short_values) / len(util_avg_short_values)
        print("Short-term Utilization: avg = {:.4f}".format(avg_util_short))
    if util_avg_long_values:
        avg_util_long = sum(util_avg_long_values) / len(util_avg_long_values)
        print("Long-term Utilization: avg = {:.4f}".format(avg_util_long))
    
    # SDN-KVS算法下，打印keyset在控制器的等待时间，并画出分布图
    if algorithm == "SDN-KVS":
        print("Keyset wait in controller: avg = {:.4f}ms, p95 = {:.4f}ms, p99 = {:.4f}ms, wait_cnt = {}" \
              .format(ks_avg_ctrl_wait_time, ks_ctrl_wait_time_p95, ks_ctrl_wait_time_p99, ks_ctrl_wait_cnt))
        # 绘制keyset在控制器等待时间的CDF
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
    
    # 绘制请求大小分布直方图
    req_siz_list = list(req_siz.values())
    plt.hist(req_siz_list, bins=list(range(min(req_siz_list), max(req_siz_list) + 1, 1)))
    plt.xlabel("Request size")
    plt.ylabel("Count")
    plt.title("req_siz") 
    plt.savefig('req_siz.png')
    plt.clf()

    # 绘制每台服务器收到的keyset数量
    server_num = len(dst_server_cnt)
    dst_server_x = list(dst_server_cnt.keys())
    dst_server_y = list(dst_server_cnt.values())
    plt.bar(dst_server_x, dst_server_y)
    plt.xlabel("Server")
    plt.ylabel("Count")
    plt.title("dst_server") 
    plt.savefig('dst_server.png')
    plt.clf()
    
    # 计算并绘制服务器利用率
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

    # 绘制各服务器和控制器队列长度随时间变化曲线
    for server_id in all_server_queue_size.keys():
        plt.plot(all_server_queue_size[server_id].keys(), all_server_queue_size[server_id].values(), alpha=0.5, label="server {}".format(server_id))
    plt.plot(ctrl_queue_size.keys(), ctrl_queue_size.values(), label="ctrl")
    plt.title('queue size in server for {}'.format(algorithm))
    plt.xlabel('time(ms)')
    plt.ylabel('queue size')
    plt.legend()
    plt.savefig('queue_len.png')
    plt.clf()

    # 绘制请求发送时间间隔分布
    plt.hist(req_st_send_time_dt)
    plt.xlabel("Delta send time")
    plt.ylabel("Count")
    plt.title("delta_send_time")
    plt.savefig("delta_send_time.png")
    plt.clf()
    
    # 绘制每台服务器收到的heavy keyset数量
    heavy_ks_dst_server_x = list(heavy_ks_dst_server_cnt.keys())
    heavy_ks_dst_server_y = list(heavy_ks_dst_server_cnt.values())
    plt.bar(heavy_ks_dst_server_x, heavy_ks_dst_server_y)
    plt.xlabel("Server")
    plt.ylabel("Count")
    plt.title("heavy_ks_dst_server_cnt") 
    plt.savefig('heavy_ks_dst_server_cnt.png')
    plt.clf()

# =========================
# 统计分析函数
# =========================
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

    # 计算每个请求的响应时间
    for req_id in range(1, tot_request_num + 1):
        try:
            if algorithm == "DAS":
                if req_id not in req_recv_time or req_id not in req_st_send_time:
                    continue
                req_response_time[req_id] = req_recv_time[req_id] - req_st_send_time[req_id]
            else:
                if req_id not in req_recv_time or req_id not in req_send_time:
                    continue
                # if req_recv_time[req_id] > 2 * req_send_time[req_id]:
                #     # print(req_recv_time[req_id]/req_send_time[req_id])
                #     continue
                req_response_time[req_id] = req_recv_time[req_id] - req_send_time[req_id]
        except KeyError:
            continue
    
    if not req_response_time:
        print("Warning: No valid request response times found!")
        return
        
    req_response_time_sorted = sorted(list(req_response_time.values()))
    req_avg_response_time = sum(req_response_time_sorted) / len(req_response_time_sorted)
    req_p99 = np.percentile(req_response_time_sorted, 99)
    req_p95 = np.percentile(req_response_time_sorted, 95)

    # 计算每个keyset的响应时间和在控制器的等待时间
    for id in range(1, tot_ks_num + 1):
        try:
            if algorithm == "DAS":
                if id not in client_recv_time or id not in ks_st_send_time:
                    continue
                ks_response_time[id] = client_recv_time[id] - ks_st_send_time[id]
            else:
                if id not in client_recv_time or id not in client_send_time:
                    continue
                ks_response_time[id] = client_recv_time[id] - client_send_time[id]
            if id not in ctrl_send_time or id not in ctrl_recv_time:
                continue
            ks_ctrl_wait_time[id] = ctrl_send_time[id] - ctrl_recv_time[id]
        except KeyError:
            continue
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

    # 计算相邻请求的发送时间间隔
    for req_id in range(2, tot_request_num + 1):
        req_st_send_time_dt.append(req_st_send_time[req_id] - req_st_send_time[req_id - 1])

    # 计算每台服务器的最早接收时间和最晚发送时间，用于后续利用率计算
    server_min_recv_time: dict[int, float] = {}
    server_max_send_time: dict[int, float] = {}
    for id, recv_time in server_recv_time.items():
        dst_server = ks_dst_server[id]
        if id not in server_send_time:
            continue  # Skip if send time is missing
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
        if server_id not in server_max_send_time:
            continue  # Skip if no send time records for this server
        send_time = server_max_send_time[server_id]
        server_serve_time[server_id] = dst_server_cnt[server_id] / server_rate[server_id]
        server_tot_time[server_id] = round(send_time - recv_time, 0)

# =========================
# 日志行解析函数
# =========================
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
    global tqs_dynamic_values
    global queue_avg_values
    global queue_max_values
    global util_avg_short_values
    global util_avg_long_values

    # 首先检查是否是调度完成日志
    if pattern15.search(line):
        return  # 如果是调度完成日志，直接返回

    # 依次尝试匹配每种日志格式，并提取相关字段
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
        if req_id not in req_send_time:
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
    match = pattern12.search(line)
    if match:
        tot_key_num = int(match.group(1))
        tot_request_num = int(match.group(2))
        tot_ks_num = int(match.group(3))
        return
    match = pattern13.search(line)
    if match:
        time = float(match.group(1)) * 1000
        server_id = int(match.group(2))
        queue_siz = int(match.group(3))
        que_avg = float(match.group(4))
        que_max = float(match.group(5))
        tqs_dynamic = float(match.group(6))
        tqs = int(match.group(7))
        util_avg_long = float(match.group(8))
        util_avg_short = float(match.group(9))
        if server_id not in all_server_queue_size.keys():
            all_server_queue_size[server_id] = {}
        all_server_queue_size[server_id][time] = queue_siz
        queue_avg_values.append(que_avg)
        tqs_dynamic_values.append(tqs_dynamic)
        queue_max_values.append(int(que_max))  # 转换为整数存储
        util_avg_long_values.append(util_avg_long)
        util_avg_short_values.append(util_avg_short)
        return
    match = pattern14.search(line)
    if match:
        time = float(match.group(1)) * 1000
        server_id = int(match.group(2))
        queue_siz = int(match.group(3))
        que_avg = float(match.group(4))
        tqs = int(match.group(5))
        if server_id not in all_server_queue_size.keys():
            all_server_queue_size[server_id] = {}
        all_server_queue_size[server_id][time] = queue_siz
        queue_avg_values.append(que_avg)
        return

# =========================
# 主解析入口
# =========================
def parse(file_path: str):
    # 检查文件是否存在
    if not os.path.exists(file_path):
        print("File not exists!")
        return
    # 逐行读取文件并解析
    with open(file_path, 'r') as file:
        for line in file:
            parse_line(line)
    # 检查数据完整性，若有丢包则报错
    if (client_send_cnt != tot_ks_num or client_recv_cnt != tot_ks_num or 
        server_send_cnt != tot_ks_num or server_recv_cnt != tot_ks_num or
        len(req_send_time) != tot_request_num or len(req_recv_time) != tot_request_num):
        print("Error! There is packet loss")
        print(client_send_cnt, client_recv_cnt, server_send_cnt, server_recv_cnt, tot_ks_num)
        print(len(req_send_time), len(req_recv_time), tot_request_num)
        # return
    # 统计分析并输出结果
    analyze()
    print_result()

# 程序入口，传入日志文件路径
if __name__ == "__main__":
    parse(sys.argv[1])
