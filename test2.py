import random

req_num = 500
dt = 0.00001
dt_base = 0
lst_time = 2.00100
id = 1
flag = True

req_siz_list = []
alpha = 2.0
x_min = 60 * (alpha - 1) / alpha
bound = 60
for i in range(1, req_num + 1):
    # req_siz_list.append(int(random.uniform(58, 62)))
    # req_siz_list.append(5)
    while True:
        x = random.paretovariate(2)
        r = x * x_min
        if r <= bound:
            req_siz_list.append(int(r))
            break

uti = 0.7
tot_key_num = sum(req_siz_list)
tot_ks_num = 2 * req_num

req_mean = 1.0 * tot_key_num / req_num
send_time = 2
server_rate = 1.0
t_mean = (req_mean / (server_rate * 1000.0)) / (uti * 2)

print("tot_key_num = {}, tot_request_num = {}, tot_ks_num = {}".format(tot_key_num, req_num, tot_ks_num))
for i in range(1, req_num + 1):
    x = req_siz_list[i - 1]
    small = int(max(x // 5, 1))
    big = x - small
    # flag = True if random.random() < 0.5000 else False
    print("[0] (id={}, st={:.5f}, src_client=0, dst_server={}, dst_ip=10.1.1.{}, req_id={}, ks_siz={}, bottleneck_ks_size={}, is_heavy=0)".format( \
        id, lst_time + dt * dt_base, "0" if flag else "1", "2" if flag else "3", i, small, max(small, big)))
    id += 1
    print("[0] (id={}, st={:.5f}, src_client=0, dst_server={}, dst_ip=10.1.1.{}, req_id={}, ks_siz={}, bottleneck_ks_size={}, is_heavy=1)".format( \
        id, lst_time + dt * dt_base, "1" if flag else "0", "3" if flag else "2", i, big, max(small, big)))
    id += 1
    dt_base += 1
    if i % 1 == 0:
        flag = not flag
    if i == 1:
        # delta_time = random.expovariate(1.0 / t_mean)
        # delta_time = random.uniform(0.005, 0.015)
        # dt_base = 0
        lst_time += 0.01