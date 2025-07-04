#!/usr/bin/python3
import statistics
import os
import sys
import json
import datetime
from time import strftime

inp = ['gcc_166', 'gcc_expr', 'gcc_typeck', 'gcc_expr2']
inp = ['soplex_pds-50', 'soplex_ref']
inp = ['astar_biglakes', 'astar_rivers']
# inp = ['soplex_pds-50', 'soplex_ref']


def extract(weight_file):
    with open(weight_file, 'r') as f:
        r = []
        mp = {}
        total = 0
        loop = 0
        dict = json.load(f)
        for workload in inp:
            stats_file = f"./merge/{workload}/result.txt"
            alpha = 1.0 / (loop + 1) 

            with open(stats_file, 'r') as f:
                for line in f:
                    seg = line.strip().split(',')
                    if len(seg) != 4:
                        if total == 0:
                            total = int(seg[0])
                        else:
                            total = total + int((int(seg[0]) - total) * alpha)
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
                        mp[pc][0] = mp[pc][0] + int((solved - mp[pc][0]) * alpha)
                        mp[pc][1] = mp[pc][1] + int((issued - mp[pc][1]) * alpha)
                        mp[pc][2] = mp[pc][2] + int((miss - mp[pc][2]) * alpha)
                        # mp[pc][3] += meta_used
                        # mp[pc][4] += meta_inserted
            loop += 1

        os.system('rm -r ./learn')
        for workload in dict.keys():
            result_file = f"./learn/{workload}/result.txt"
            os.system(f'mkdir -p ./learn/{workload}')
            with open(result_file, 'w') as rf:
                rf.write(f"{total}\n")
                for pc, counter in mp.items():
                    rf.write(f"{pc},{counter[0]},{counter[1]},{counter[2]}\n")
           


### main ###
if __name__ == "__main__":
    out_dir = os.path.abspath(sys.argv[1])
    # weight_file = os.path.abspath("spec06_rv64gcb_o2_20m.json.coverage0.01.json")
    weight_file = os.path.abspath("../script/soplex.json")
    # weight_file = os.path.abspath("../script/temporal2.json")
    extract(weight_file)
    os.system(f'python3 batch_process_new.py {out_dir} 0')
