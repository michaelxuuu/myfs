RED='\033[1;31m'
GREEN='\033[1;32m'
RESET='\033[0m'

pass() {
    echo "${GREEN}passed: $1${RESET}"
}

fail() {
    echo "${RED}failed: $1${RESET}"
}

cleanup() {
    rm *.tmp
}

doecho() {
    for cmd in "$@"; do
        echo "$cmd"
    done
}

matchline() {
    line=$(sed -n "${1}p" $3)
    if [ $line = $2 ]; then
        echo "1"
    fi
}

migrate_test() {
    touch input.tmp
    commands=(
        "migrate /fs.c fs.c"
        "retrieve fs.c.tmp /fs.c"
        "quit"
    )
    doecho "${commands[@]}" > input.tmp
    ./main vhd < input.tmp
    if [ -n $(diff fs.c.tmp fs.c) ]; then
        pass "two files match"
    else
        fail "two files differ"
    fi
}

NDIRECT=10
NINDIRECT=2
NDINDIRECT=1
BSZ=512
sparse_test() {
    touch input.tmp > out.tmp
    commands=(
        "touch /sparse"
        "write /sparse 0 1 a"
        "write /sparse $((NDIRECT*BSZ)) 1 a"
        "write /sparse $((NDIRECT*BSZ+NINDIRECT*BSZ*BSZ)) 1 a"
        "read /sparse $((BSZ)) 1"
        "read /sparse $((NDIRECT*BSZ+BSZ)) 1"
        "read /sparse $((NDIRECT*BSZ+NINDIRECT*BSZ*BSZ)) 1"
        "stat /sparse"
        "quit"
    )
    doecho "${commands[@]}" > input.tmp
    ./main vhd < input.tmp > out.tmp
    size=$(awk -F: '$1 == "size" { print $2 }' out.tmp)
    # Check if the size is 100
    if [ "$size" -eq $((NDIRECT*BSZ+NINDIRECT*BSZ*BSZ+1)) ]; then
        pass "sparse: size is $((NDIRECT*BSZ+NINDIRECT*BSZ*BSZ+1))"
    else
        fail "sparse: size is not $((NDIRECT*BSZ+NINDIRECT*BSZ*BSZ+1))"
    fi

    # Check if the lines match the expected content
    if [ "$(matchline 1 "\0" out.tmp)" ] && \
        [ "$(matchline 2 "\0" out.tmp)" ] && \
        [ "$(matchline 3 "a" out.tmp)" ]; then
        pass "sparse: read result matches the expected outcome"
    else
        fail "sparse: read result differs from the expected outcome"
    fi
}

migrate_test
cleanup
sparse_test
cleanup
