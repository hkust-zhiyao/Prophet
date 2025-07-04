#!/usr/bin/python3
import statistics
import os
import sys
import json
from time import strftime
from datetime import datetime
from multiprocessing import Pool
    
proc_num = 100 ## total proc numbers in Pool

def gem5_mission(cmd):
    os.system(cmd)

def extract(out_dir, weight_file, merge):
    pool = Pool(proc_num)
    with open(weight_file, 'r') as f:
        dict = json.load(f)
        os.system('mkdir -p ./profile_re')
        root_dir = os.path.abspath('./')
        for workload in dict.keys():
            for point in dict[workload].keys():
                ## generate path for each point
                weight = dict[workload][point]
                trace_path = f"{out_dir}/{workload}/{workload}_{point}_{weight}/train.txt"
                stats_path = f"{out_dir}/{workload}/{workload}_{point}_{weight}/stats.txt"
                merge_path = f"./{merge}/{workload}/result.txt"

                store_path_log = f"{root_dir}/profile_re/{workload}/{workload}_{point}_{weight}/log.txt"
                store_path_result = f"{root_dir}/profile_re/{workload}/{workload}_{point}_{weight}"
                os.system(f"mkdir -p {root_dir}/profile_re/{workload}/{workload}_{point}_{weight}")
                if(os.path.exists(trace_path) and os.path.exists(merge_path)):
                    cmd = f"python3 process_new.py {trace_path} {stats_path} {store_path_result} {merge_path} > {store_path_log}"
                    print(cmd)
                    pool.apply_async(gem5_mission, args=(cmd,))
        pool.close()
        pool.join()
        now = datetime.now()
        if os.path.exists("profile_re_" + now.strftime("%Y-%m-%d")):
            os.system("rm -r " + "profile_re_" + now.strftime("%Y-%m-%d"))
        os.system("cp -r profile_re profile_re_" + now.strftime("%Y-%m-%d"))



### main ###
if __name__ == "__main__":
    out_dir = os.path.abspath(sys.argv[1])
    merge = 'merge' if int(sys.argv[2]) == 1 else 'learn'
    # weight_file = os.path.abspath("spec06_rv64gcb_o2_20m.json.coverage0.01.json")
    weight_file = os.path.abspath("../script/temporal2.json")
    extract(out_dir, weight_file, merge)

