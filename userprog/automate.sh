#! /bin/bash
# 주의사항: threads 폴더 바로 하위에 본 파일을 저장하세요.
# 돌리고 싶은 테스트의 경우 주석 처리를 해제하세요.

declare -a tests=(
# "tests/threads/priority-donate-chain"

# -----------------------------------------

# "args-none"
# "args-single"
# "args-multiple"
# "args-many"
# "args-dbl-space"
"halt"
# "exit"
# "create-normal"
# "create-empty"
# "create-null"
# "create-bad-ptr"
# "create-long"
# "create-exists"
# "create-bound"
# "open-normal"
# "open-missing"
# "open-boundary"
# "open-empty"
# "open-null"
# "open-bad-ptr"
# "open-twice"
# "close-normal"
# "close-twice"
# "close-bad-fd"
# "read-normal"
# "read-bad-ptr"
# "read-boundary"
# "read-zero"
# "read-stdout"
# "read-bad-fd"
# "write-normal"
# "write-bad-ptr"
# "write-boundary"
# "write-zero"
# "write-stdin"
# "write-bad-fd"
# "fork-once"
# "fork-multiple"
# "fork-recursive"
# "fork-read"
# "fork-close"
# "fork-boundary"
# "exec-once"
# "exec-arg"
# "exec-boundary"
# "exec-missing"
# "exec-bad-ptr"
# "exec-read"
# "wait-simple"
# "wait-twice"
# "wait-killed"
# "wait-bad-pid"
# "multi-recurse"
# "multi-child-fd"
# "rox-simple"
# "rox-child"
# "rox-multichild"
# "bad-read"
# "bad-write"
# "bad-read2"
# "bad-write2"
# "bad-jump"
# "bad-jump2"

# --------------------------------------

# "tests/filesys/base/lg-create"
# "tests/filesys/base/lg-full"
# "tests/filesys/base/lg-random"
# "tests/filesys/base/lg-seq-block"
# "tests/filesys/base/lg-seq-random"
# "tests/filesys/base/sm-create"
# "tests/filesys/base/sm-full"
# "tests/filesys/base/sm-random"
# "tests/filesys/base/sm-seq-block"
# "tests/filesys/base/sm-seq-random"
# "tests/filesys/base/syn-read"
# "tests/filesys/base/syn-remove"
# "tests/filesys/base/syn-write"
# "tests/userprog/no-vm/multi-oom"
# "tests/threads/alarm-single"
# "tests/threads/alarm-multiple"
# "tests/threads/alarm-simultaneous"
# "tests/threads/alarm-priority"
# "tests/threads/alarm-zero"
# "tests/threads/alarm-negative"
# "tests/threads/priority-change"
# "tests/threads/priority-donate-one"
# "tests/threads/priority-donate-multiple"
# "tests/threads/priority-donate-multiple2"
# "tests/threads/priority-donate-nest"
# "tests/threads/priority-donate-sema"
# "tests/threads/priority-donate-lower"
# "tests/threads/priority-fifo"
# "tests/threads/priority-preempt"
# "tests/threads/priority-sema"
# "tests/threads/priority-condvar"
# "tests/threads/priority-donate-chain"
)

make clean
source activate
cd userprog
make
cd build

directory="tests/userprog/"
suffix=".result"
VB=1 # 이 값을 1로 하면 테스트 결과와 더불어 테스트 중간에 찍히는 출력값도 볼 수 있습니다.

for test in "${tests[@]}"; do
    echo "----------------------------------------------------------"
    echo "$test 시작합니다."
    if [ $VB -eq 0 ];then
        make "${directory}$test${suffix}"
    else
        make "${directory}$test${suffix}" VERBOSE=1
    fi
done