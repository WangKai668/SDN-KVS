import re
import os

def extract_metrics(content):
    algorithms = ['FIFO', 'SRPT', 'SBF', 'DAS', 'SDN-KVS']
    pattern_map = {
        'FIFO': r'FIFO.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms',
        'SRPT': r'SRPT.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms',
        'SBF': r'SBF.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms',
        'DAS': r'DAS.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms',
        'SDN-KVS': r'SDN-KVS.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms'
    }
    
    results = {}
    for algo in algorithms:
        match = re.search(pattern_map[algo], content, re.DOTALL)
        if match:
            results[algo] = {
                'mean': match.group(1),
                'p95': match.group(2),
                'p99': match.group(3)
            }
        else:
            results[algo] = {
                'mean': 'N/A',
                'p95': 'N/A',
                'p99': 'N/A'
            }
    return results

def process_util_files(file_list, output_file):
    # 按场景分类文件
    scenes = {}
    for file_path in file_list:
        match = re.search(r'scene\[(\d+)\].*_u([\d.]+)_', file_path)
        if match:
            scene = int(match.group(1))
            util = float(match.group(2))
            if scene not in scenes:
                scenes[scene] = {}
            scenes[scene][util] = file_path
    
    with open(output_file, 'w') as out_f:
        # 写入文件头
        out_f.write("Scene\tMetric\tUtilization\tFIFO\tSRPT\tSBF\tDAS\tSDN-KVS\n")
        
        # 为每个场景处理数据
        for scene in sorted(scenes.keys()):
            util_files = scenes[scene]
            util_values = sorted(util_files.keys())
            
            # 收集数据
            metrics_data = {
                'mean': {},
                'p95': {},
                'p99': {}
            }
            
            # 初始化数据结构
            for algo in ['FIFO', 'SRPT', 'SBF', 'DAS', 'SDN-KVS']:
                for metric in metrics_data:
                    metrics_data[metric][algo] = []
            
            for util in util_values:
                file_path = util_files[util]
                try:
                    with open(file_path, 'r') as f:
                        content = f.read()
                        results = extract_metrics(content)
                        
                        for algo in results:
                            for metric in metrics_data:
                                metrics_data[metric][algo].append(results[algo][metric])
                except Exception as e:
                    print(f"Error processing {file_path}: {str(e)}")
                    for algo in ['FIFO', 'SRPT', 'SBF', 'DAS', 'SDN-KVS']:
                        for metric in metrics_data:
                            metrics_data[metric][algo].append('N/A')
            
            # 写入数据
            for metric in metrics_data:
                for i, util in enumerate(util_values):
                    row = [
                        str(scene),
                        metric,
                        f"{util:.2f}",
                        metrics_data[metric]['FIFO'][i],
                        metrics_data[metric]['SRPT'][i],
                        metrics_data[metric]['SBF'][i],
                        metrics_data[metric]['DAS'][i],
                        metrics_data[metric]['SDN-KVS'][i]
                    ]
                    out_f.write('\t'.join(row) + '\n')
    
    print(f"All results saved to {output_file}")


def process_latency_files(file_list, output_file):
    # 按场景分类文件
    scenes = {}
    for file_path in file_list:
        # 匹配scene和latency
        match = re.search(r'scene\[(\d+)\].*_l(\d+)_', file_path)
        if match:
            scene = int(match.group(1))
            latency = int(match.group(2))
            if scene not in scenes:
                scenes[scene] = {}
            scenes[scene][latency] = file_path

    with open(output_file, 'w') as out_f:
        # 写入文件头
        out_f.write("Scene\tMetric\tLatency\tFIFO\tSRPT\tSBF\tDAS\tSDN-KVS\n")

        # 为每个场景处理数据
        for scene in sorted(scenes.keys()):
            latency_files = scenes[scene]
            latency_values = sorted(latency_files.keys())

            # 收集数据
            metrics_data = {
                'mean': {},
                'p95': {},
                'p99': {}
            }

            # 初始化数据结构
            for algo in ['FIFO', 'SRPT', 'SBF', 'DAS', 'SDN-KVS']:
                for metric in metrics_data:
                    metrics_data[metric][algo] = []

            for latency in latency_values:
                file_path = latency_files[latency]
                try:
                    with open(file_path, 'r') as f:
                        content = f.read()
                        results = extract_metrics(content)

                        for algo in results:
                            for metric in metrics_data:
                                metrics_data[metric][algo].append(results[algo][metric])
                except Exception as e:
                    print(f"Error processing {file_path}: {str(e)}")
                    for algo in ['FIFO', 'SRPT', 'SBF', 'DAS', 'SDN-KVS']:
                        for metric in metrics_data:
                            metrics_data[metric][algo].append('N/A')

            # 写入数据
            for metric in metrics_data:
                for i, latency in enumerate(latency_values):
                    row = [
                        str(scene),
                        metric,
                        str(latency),
                        metrics_data[metric]['FIFO'][i],
                        metrics_data[metric]['SRPT'][i],
                        metrics_data[metric]['SBF'][i],
                        metrics_data[metric]['DAS'][i],
                        metrics_data[metric]['SDN-KVS'][i]
                    ]
                    out_f.write('\t'.join(row) + '\n')

    print(f"All results saved to {output_file}")

# # # 需要特殊处理的latency文件列表
# latency_files = [
#     'result_util_scene[1]_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_q64_l600_h.log',
#     'result_util_scene[1]_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_q64_l1000_h.log',
#     'result_util_scene[1]_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_q64_l2500_h.log',
#     'result_util_scene[1]_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_q64_l5000_h.log',
#     'result_util_scene[1]_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_q64_l10000_h.log',
#     'result_util_scene[1]_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_q64_l50000_h.log',
#     'result_util_scene[1]_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_q64_l100000_h.log',
# ]

# # 处理latency文件并输出到单个文件
# output_file = 'results_latency_origin.tsv'
# process_latency_files(latency_files, output_file)

# # 需要特殊处理的util文件列表
util_files = [
    'result_util_scene[2]_c32_s32_k150000_r1:1:0_x0_y0_z0_u0.4_q64_h.log',
    'result_util_scene[2]_c32_s32_k150000_r1:1:0_x0_y0_z0_u0.5_q64_h.log',
    'result_util_scene[2]_c32_s32_k150000_r1:1:0_x0_y0_z0_u0.6_q64_h.log',
    'result_util_scene[2]_c32_s32_k150000_r1:1:0_x0_y0_z0_u0.71_q64_h.log',
    'result_util_scene[2]_c32_s32_k150000_r1:1:0_x0_y0_z0_u0.8_q64_h.log',
    'result_util_scene[2]_c32_s32_k150000_r1:1:0_x0_y0_z0_u0.9_q64_h.log',
    'result_util_scene[2]_c32_s32_k150000_r1:1:0_x0_y0_z0_u1_q64_h.log',
]

# 处理util文件并输出到单个文件
output_file = 'results_util_origin.tsv'
process_util_files(util_files, output_file)