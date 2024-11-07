cmake_minimum_required(VERSION 3.27)
project(os_assignment C)

set(CMAKE_C_STANDARD 11)

include_directories(include)

add_executable(os_assignment
        include/bitops.h
        include/common.h
        include/cpu-tlbcache.h
        include/cpu.h
        include/loader.h
        include/mem.h
        include/mm.h
        include/os-cfg.h
        include/os-mm.h
        include/queue.h
        include/sched.h
        include/timer.h
        src/cpu-tlb.c
        src/cpu-tlbcache.c
        src/cpu.c
        src/loader.c
        src/mem.c
        src/mm-memphy.c
        src/mm-vm.c
        src/mm.c
        src/os.c
        src/paging.c
        src/queue.c
        src/sched.c
        src/timer.c)
