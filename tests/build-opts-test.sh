#!/bin/sh
export QEMU_FLAGS+=-no-reboot
INIT_QF=$QEMU_FLAGS
change_option() {
	sed -i -e "s/$1=$2/$1=$3/g" config.cfg
}

compile() {
	echo ' * Compiling...'
	if ! make clean all -j6 &> test.log; then
		echo COMPILE FAILED
		exit 1
	fi
}

test() {
	echo ' *' "Testing ($INIT_QF $@)..."
	QEMU_FLAGS="$INIT_QF $@" make test </tmp/test_fifo &>> test.log &
	echo "test!" > /tmp/test_fifo
	sleep 0.1
	echo '````````````````' >> /tmp/test_fifo
	echo '````````````````' >> /tmp/test_fifo
	wait
	sleep 0.1
	if ! (tail -n4 test.log | grep '==TESTS PASSED=='); then
		echo TEST FAILED
		exit 1
	fi
}

run_tests() {
	rm /tmp/test_fifo
	mkfifo /tmp/test_fifo
	compile
	for var in "$@"; do
		test $var
	done
}

function finish {
	echo Cleaning up...
	cp config.cfg.old config.cfg
}
trap finish EXIT

cp config.cfg config.cfg.old
make clean
main_tests() {
	cp config.cfg config.cfg.old
	echo Resetting tests...
	change_option CONFIG_RUN_TESTS n y
	change_option CONFIG_UBSAN n y
	change_option CONFIG_BUILD_LTO y n
	change_option CONFIG_BUILD_WERROR n y
	change_option CONFIG_DEBUG n y

	echo 'Clang, -O3'
	change_option CONFIG_BUILD_CLANG n y
	change_option CONFIG_BUILD_OPTIMIZATION 0 3
	change_option CONFIG_BUILD_OPTIMIZATION 1 3
	change_option CONFIG_BUILD_OPTIMIZATION 2 3
	change_option CONFIG_BUILD_OPTIMIZATION s 3
	change_option CONFIG_BUILD_OPTIMIZATION g 3
	change_option CONFIG_BUILD_OPTIMIZATION fast 3
	run_tests "$@"

	echo 'Clang, -Os'
	change_option CONFIG_BUILD_OPTIMIZATION 3 s
	run_tests "$@"

	echo 'Clang, -O0'
	change_option CONFIG_BUILD_OPTIMIZATION s 0
	run_tests "$@"

	echo 'GCC, -O0'
	change_option CONFIG_BUILD_CLANG y n
	run_tests "$@"

	echo 'GCC, -Os'
	change_option CONFIG_BUILD_OPTIMIZATION 0 s
	run_tests "$@"

	echo 'GCC, -O3'
	change_option CONFIG_BUILD_OPTIMIZATION s 3
	run_tests "$@"

	#change_option CONFIG_UBSAN y n
	#echo 'GCC, -O3, LTO'
	#change_option CONFIG_BUILD_LTO n y
	#run_tests "$@"

	#echo 'GCC, -Os, LTO'
	#change_option CONFIG_BUILD_OPTIMIZATION 3 s
	#run_tests "$@"

	#echo 'GCC, -O0, LTO'
	#change_option CONFIG_BUILD_OPTIMIZATION s 0
	#run_tests "$@"
}

if [ "$#" -eq 0 ]; then
	main_tests " " "-smp 2" "-enable-kvm" "-enable-kvm -smp 4"
else
	main_tests "$@"
fi

