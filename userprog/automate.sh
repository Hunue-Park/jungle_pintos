#! /bin/bash
# 주의사항: threads 폴더 바로 하위에 본 파일을 저장하세요.
# 돌리고 싶은 테스트의 경우 주석 처리를 해제하세요.

declare -a tests=(
    "args-single" 
    # "alarm-multiple"
    # "alarm-simultaneous"
    # "alarm-zero"
    # "alarm-negative"
    # "alarm-priority"

    # "priority-fifo"
    # "priority-preempt"
    # "priority-change"
    # "priority-donate-one"
    # "priority-donate-multiple"
    # "priority-donate-multiple2"
    # "priority-donate-nest"
    # "priority-donate-sema"
    # "priority-donate-lower"
    # "priority-donate-chain"
    # "priority-sema"
    # "priority-condvar"
    
    # "mlfqs-load-1"
    # "mlfqs-load-60"
    # "mlfqs-load-avg"
    # "mlfqs-recent-1"
    # "mlfqs-fair-2"
    # "mlfqs-fair-20"
    # "mlfqs-nice-2"
    # "mlfqs-nice-10"
    # "mlfqs-block"
)

make clean
source activate
cd userprog
make
cd build

directory="tests/userprog/"
suffix=".result"
VB=0 # 이 값을 1로 하면 테스트 결과와 더불어 테스트 중간에 찍히는 출력값도 볼 수 있습니다.

for test in "${tests[@]}"; do
    echo "----------------------------------------------------------"
    echo "$test 시작합니다."
    if [ $VB -eq 0 ];then
        make "${directory}$test${suffix}"
    else
        make "${directory}$test${suffix}" VERBOSE=1
    fi
done