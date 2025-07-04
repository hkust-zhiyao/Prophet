import os
import datetime
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.pyplot import MultipleLocator

sum_threshold = 0.05
AllProb_threshold = 0.99
accuracy_threshold = 0.15

def key_gem5_IPC(line):
    p1 = line.split()
    return p1[1]

def extract(file, key):
    with open(file, "r") as f:
        ss = f.readlines() #all lines
        for s in ss:
            index = s.find(key)
            if index != -1:  #if hit
                return key_gem5_IPC(s)
        return -1


def read_file(filename):
    allMiss = ''
    pc_list = []
    solve_list = []
    issue_list = []
    miss_list = []
    first = True

    with open(filename, 'r') as file:
        for line in file:
            line = line.strip()
            if first:
                allMiss = line
                first = False
            else:
                pc, solve, issue, miss = line.split(',')
                pc_list.append(pc)
                solve_list.append(solve)
                issue_list.append(issue)
                miss_list.append(miss)
                

    return allMiss, pc_list, solve_list, issue_list, miss_list



if __name__ == "__main__":
    import sys
    enable_merge_counter = True

    print(f"enable_merge_counter: {enable_merge_counter}")
    print(f"sum_threshold is: {sum_threshold}")
    print(f"AllProb_threshold is: {AllProb_threshold}")
    print(f"Accuracy_threshold is: {accuracy_threshold}")

    trace_file = sys.argv[1]
    stats_path = sys.argv[2]
    output_path = sys.argv[3]
    merge_file = sys.argv[4]

    output_file = f"{output_path}/insertion.txt"
    partition_file = f"{output_path}/partition.txt"
    miss_file = f"{output_path}/miss.txt"

    profile_re = []
    llc_re = []
    miss_re = []
    global_cov_PF = 0.0
    absolute_PF = 0
    if enable_merge_counter and os.path.exists(merge_file):
        print("Using merge counter")
        allMiss, pc_list, solve_list, issue_list, miss_list = read_file(merge_file)
    else:    
        allMiss, pc_list, solve_list, issue_list, miss_list = read_file(trace_file)
    allMiss = int(allMiss)

    for pc, solve_str, issue_str, miss_str in zip(pc_list, solve_list, issue_list, miss_list):
        solve = int(solve_str)
        issue = int(issue_str)
        miss = int(miss_str)

        occu = miss/allMiss
        if occu > 0.05:
            miss_re.append(pc)

        accuracy = 0.0 if issue == 0 else solve/issue
        local_solved_rate = 0.0 if miss == 0 else solve/miss
        global_solved_rate = solve/allMiss * 10
        absolute_PF += solve
        sum = local_solved_rate + global_solved_rate

        global_cov_PF += solve/allMiss
        
        
        if accuracy > accuracy_threshold:
            print("**********", end="")
            if accuracy < 0.25:
                profile_re.append([pc, 1])
            elif accuracy < 0.5:
                profile_re.append([pc, 2])
            elif accuracy < 0.75:
                profile_re.append([pc, 3])
            else:
                profile_re.append([pc, 4])
        # if accuracy > accuracy_threshold:
        #     print("**********", end="")
        #     if accuracy < 0.125:
        #         profile_re.append([pc, 1])
        #     elif accuracy < 0.25:
        #         profile_re.append([pc, 2])
        #     elif accuracy < 0.375:
        #         profile_re.append([pc, 3])
        #     elif accuracy < 0.5:
        #         profile_re.append([pc, 4])
        #     elif accuracy < 0.625:
        #         profile_re.append([pc, 5])
        #     elif accuracy < 0.75:
        #         profile_re.append([pc, 6])
        #     elif accuracy < 0.875:
        #         profile_re.append([pc, 7])
        #     else:
        #         profile_re.append([pc, 8])
        print(f"PC: {pc}, accuracy: {accuracy}")
        # print(f"local_solved_rate: {local_solved_rate}, global_solved_rate: {global_solved_rate}, sum: {sum}")
    
    # x > 0.75 ----> 1MB
    # 0.5 < x < 0.75 ----> 0.75MB
    # 0.25 < x < 0.5 ----> 0.5MB
    # 0MB

    global_cov_CACHE = 1.0 - float(extract(stats_path, 'l3.overallMissRate::cpu.data'))
    absolute_CACHE = float(extract(stats_path, 'l3.demandHits::cpu.data'))

    meta_table_used = int(extract(stats_path, 'metaTableUsedEntries'))
    meta_table_used = meta_table_used * 4 / 3

    print(f"global coverage from PF is: {global_cov_PF}, absolute is: {absolute_PF}")
    print(f"global coverage from CACHE is: {global_cov_CACHE}, absolute is: {absolute_CACHE}")
    print(f"absolute ratio is: {absolute_PF/absolute_CACHE}")

    with open(partition_file, 'w') as f:
        if meta_table_used > 0 and meta_table_used < 32768:
            f.write("32768")
        elif meta_table_used > 32768 and meta_table_used < 65536:
            f.write("65536")
        elif meta_table_used > 65536 and meta_table_used < 131072:
            f.write("131072")
        else:
            f.write("262144")

    with open(output_file, 'w') as f:
        for re in profile_re:
            f.write(f"{re[0]},{re[1]}\n")
            print(f"{re[0]},{re[1]}")
    
    with open(miss_file, 'w') as f:
        for m in miss_re:
            f.write(f"{m}\n")


    
    


                            



