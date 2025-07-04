#!/usr/bin/python3
import statistics
import os
import sys
import json
import datetime
from time import strftime
   

def extract(out_dir, weight_file):
    with open(weight_file, 'r') as f:
        dict = json.load(f)
        r = []
        for workload in dict.keys():
            mp = {}
            total = 0
            result_file = f"./merge/{workload}/result.txt"
            os.system(f'mkdir -p ./merge/{workload}')
            for point in dict[workload].keys():
                ## generate path for each point
                weight = dict[workload][point]
                train_path = f"{out_dir}/{workload}/{workload}_{point}_{weight}/train.txt"
                if(os.path.exists(train_path)):
                    print(train_path)
                    with open(train_path, 'r') as f:
                        for line in f:
                            seg = line.strip().split(',')
                            if len(seg) != 4:
                                total += int(seg[0])
                                continue
                            pc = seg[0]
                            solved = int(seg[1])
                            issued = int(seg[2])
                            miss = int(seg[3])
                            # meta_used = int(seg[4])
                            # meta_inserted = int(seg[5])
                            if pc not in mp:
                                mp[pc] = [solved, issued, miss]
                            else:
                                mp[pc][0] += solved
                                mp[pc][1] += issued
                                mp[pc][2] += miss
                                # mp[pc][3] += meta_used
                                # mp[pc][4] += meta_inserted

            with open(result_file, 'w') as rf:
                rf.write(f"{total}\n")
                for pc, counter in mp.items():
                    rf.write(f"{pc},{counter[0]},{counter[1]},{counter[2]}\n")
           


### main ###
if __name__ == "__main__":
    out_dir = os.path.abspath(sys.argv[1])
    # weight_file = os.path.abspath("spec06_rv64gcb_o2_20m.json.coverage0.01.json")
    # weight_file = os.path.abspath("../script/gcc.json")
    weight_file = os.path.abspath("../script/temporal2.json")
    print(out_dir)
    extract(out_dir, weight_file)
