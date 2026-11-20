import re
import os
from pathlib import Path


RESULTS_DIR = Path(os.environ.get("SDN_KVS_RESULTS_DIR") or "/mnt/nasDisk_ds3617/wangkai/SDN-KVS")

def extract_metrics_from_file(content):
    # 定义算法顺序和对应的正则表达式模式
    algorithms = ['SDN-KVS', 'FIFO', 'SRPT', 'SBF', 'DAS']         #r'SDN-KVS.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms',
    pattern_map = { 
        'SDN-KVS': r'SDN-KVS.*?\*+\s+q=64\s+\*+.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms',
        'FIFO': r'FIFO.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms',
        'SRPT': r'SRPT.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms',
        'SBF': r'SBF.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms',
        'DAS': r'DAS.*?Request: avg = (\d+\.\d+)ms, p95 = (\d+\.\d+)ms, p99 = (\d+\.\d+)ms'
    }
    
    # 初始化结果字典
    results = {
        'mean': [],
        'p95': [],
        'p99': []
    }
    
    # 为每个算法提取指标
    for algo in algorithms:
        pattern = pattern_map[algo]
        match = re.search(pattern, content, re.DOTALL)
        if match:
            results['mean'].append(match.group(1))
            results['p95'].append(match.group(2))
            results['p99'].append(match.group(3))
        else:
            results['mean'].append('N/A')
            results['p95'].append('N/A')
            results['p99'].append('N/A')
    
    return results

def process_all_files(file_list, output_file):
    with open(output_file, 'w') as out_f:
        for file_path in file_list:
            try:
                with open(file_path, 'r') as in_f:
                    content = in_f.read()
                    results = extract_metrics_from_file(content)
                    
                    # 第一行：文件名
                    out_f.write(f"{file_path}\n")
                    
                    # 第二行：mean值（用制表符分隔）
                    out_f.write("\t".join(results['mean']) + "\n")
                    
                    # 第三行：p95值（用制表符分隔）
                    out_f.write("\t".join(results['p95']) + "\n")
                    
                    # 第四行：p99值（用制表符分隔）
                    out_f.write("\t".join(results['p99']) + "\n")
                    
                    # 第五行：空行
                    out_f.write("\n")
                    
            except FileNotFoundError:
                print(f"Warning: File not found - {file_path}")
                # 仍然写入5行保持格式一致
                out_f.write(f"{file_path}\n")
                out_f.write("N/A\tN/A\tN/A\tN/A\tN/A\n")
                out_f.write("N/A\tN/A\tN/A\tN/A\tN/A\n")
                out_f.write("N/A\tN/A\tN/A\tN/A\tN/A\n")
                out_f.write("\n")
                continue
            except Exception as e:
                print(f"Error processing {file_path}: {str(e)}")
                # 仍然写入5行保持格式一致
                out_f.write(f"{file_path}\n")
                out_f.write("N/A\tN/A\tN/A\tN/A\tN/A\n")
                out_f.write("N/A\tN/A\tN/A\tN/A\tN/A\n")
                out_f.write("N/A\tN/A\tN/A\tN/A\tN/A\n")
                out_f.write("\n")
                continue
    
    print(f"All results saved to {output_file}")

# 文件列表

file_list = [
    'result_tqs_scene[8]_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_h.log',
    'result_tqs_scene[9]_c32_s32_k150000_r1:1:0_x0_y0_z0_u0.7_h.log',
    'result_tqs_scene[10]_c32_s32_k150000_r5:2:1_x0_y0_z0_u0.7_h.log',
    'result_tqs_scene[11]_c32_s32_k150000_r1:0:0_x1_y1_z0_u0.7_h.log',
    'result_tqs_scene[12]_c32_s32_k150000_r1:1:0_x1_y1_z0_u0.7_h.log',
    'result_tqs_scene[13]_c32_s32_k150000_r5:2:1_x1_y1_z0_u0.7_h.log',
    'result_tqs_scene[14]_c128_s128_k150000_r1:1:0_x0_y0_z0_u0.7_h.log',
    'result_tqs_scene[15]_c128_s128_k150000_r1:1:0_x1_y1_z0_u0.7_h.log',

]

# 处理所有文件
if __name__ == "__main__":
    if not RESULTS_DIR.is_dir():
        raise SystemExit(f"结果目录不存在 {RESULTS_DIR}")
    process_all_files(
        [RESULTS_DIR / name for name in file_list],
        RESULTS_DIR / "results_tqs_origin.txt",
    )
