RED='\033[1;32m'
GREEN='\033[1;32m'
RESET='\033[0m'

pass() {
    echo "${GREEN}passed: $1${RESET}"
}

fail() {
    echo "${GREEN}failed: $1${RESET}"
}

touch input.tmp
echo "migrate /fs.c fs.c" >> input.tmp
echo "retrieve fs.c.tmp /fs.c" >> input.tmp
echo "quit" >> input.tmp
./main vhd < input.tmp
if [ -n $(diff fs.c.tmp fs.c) ]; then
    pass "two files match"
else
    fail "two files differ"
fi
rm *.tmp
