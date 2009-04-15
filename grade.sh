#!/bin/sh

verbose=false

if [ "x$1" = "x-v" ]
then
	verbose=true
	out=/dev/stdout
	err=/dev/stderr
else
	out=/dev/null
	err=/dev/null
fi

<<<<<<< HEAD:grade.sh
pts=2
=======
pts=5
>>>>>>> master:grade.sh
timeout=30
preservefs=n

echo_n () {
	# suns can't echo -n, and Mac OS X can't echo "x\c"
	# assume argument has no doublequotes
	awk 'BEGIN { printf("'"$*"'"); }' </dev/null
}

runbochs () {
	# Find the address of the kernel readline function,
	# which the kernel monitor uses to read commands interactively.
	brkaddr=`grep 'readline$' obj/kern/kernel.sym | sed -e's/ .*$//g'`
	#echo "brkaddr $brkaddr"

	# Run Bochs, setting a breakpoint at readline(),
	# and feeding in appropriate commands to run, then quit.
	t0=`date +%s.%N 2>/dev/null`
	(
		# The sleeps are necessary in some Bochs to 
		# make it parse each line separately.  Sleeping 
		# here sure beats waiting for the timeout.
		echo vbreak 0x8:0x$brkaddr
		sleep .5
		echo c
		# EOF will do just fine to quit.
	) | (
		ulimit -t $timeout
		# date
		bochs -q 'display_library: nogui' \
			'parport1: enabled=1, file="bochs.out"' 
		# date
	) >$out 2>$err
	t1=`date +%s.%N 2>/dev/null`
	time=`echo "scale=1; ($t1-$t0)/1" | sed 's/.N/.0/g' | bc 2>/dev/null`
	time="(${time}s)"
}


# Usage: runtest <tagname> <defs> <strings...>
runtest () {
	perl -e "print '$1: '"
	rm -f obj/kern/init.o obj/kern/kernel obj/kern/bochs.img 
	[ "$preservefs" = y ] || rm -f obj/fs/fs.img
	if $verbose
	then
		echo "gmake $2... "
	fi
	gmake $2 >$out
	if [ $? -ne 0 ]
	then
		echo gmake $2 failed 
		exit 1
	fi
	runbochs
	if [ ! -s bochs.out ]
	then
		echo 'no bochs.out'
	else
		shift
		shift
		continuetest "$@"
	fi
}

quicktest () {
	perl -e "print '$1: '"
	shift
	continuetest "$@"
}

stubtest () {
    perl -e "print qq|$1: OK $2\n|";
    shift
    score=`expr $pts + $score`
}

continuetest () {
	okay=yes

	not=false
	for i
	do
		if [ "x$i" = "x!" ]
		then
			not=true
		elif $not
		then
			if egrep "^$i\$" bochs.out >/dev/null
			then
				echo "got unexpected line '$i'"
				if $verbose
				then
					exit 1
				fi
				okay=no
			fi
			not=false
		else
			egrep "^$i\$" bochs.out >/dev/null
			if [ $? -ne 0 ]
			then
				echo "missing '$i'"
				if $verbose
				then
					exit 1
				fi
				okay=no
			fi
			not=false
		fi
	done
	if [ "$okay" = "yes" ]
	then
		score=`expr $pts + $score`
		echo OK $time
	else
		echo WRONG $time
	fi
}

# Usage: runtest1 [-tag <tagname>] <progname> [-Ddef...] STRINGS...
runtest1 () {
	if [ $1 = -tag ]
	then
		shift
		tag=$1
		prog=$2
		shift
		shift
	else
		tag=$1
		prog=$1
		shift
	fi
	runtest1_defs=
	while expr "x$1" : 'x-D.*' >/dev/null; do
		runtest1_defs="DEFS+='$1' $runtest1_defs"
		shift
	done
	runtest "$tag" "DEFS='-DTEST=_binary_obj_user_${prog}_start' DEFS+='-DTESTSIZE=_binary_obj_user_${prog}_size' $runtest1_defs" "$@"
}

<<<<<<< HEAD:grade.sh
score=0

# Reset the file system to its original, pristine state
resetfs() {
	rm -f obj/fs/fs.img
	gmake obj/fs/fs.img >$out
}


resetfs

runtest1 -tag 'fs i/o [testfsipc]' testfsipc \
	'FS can do I/O' \
	! 'idle loop can do I/O' \

quicktest 'read_block [testfsipc]' \
	'superblock is good' \

quicktest 'write_block [testfsipc]' \
	'write_block is good' \

quicktest 'read_bitmap [testfsipc]' \
	'read_bitmap is good' \

quicktest 'alloc_block [testfsipc]' \
	'alloc_block is good' \
=======
>>>>>>> master:grade.sh

<<<<<<< HEAD:grade.sh
quicktest 'file_open [testfsipc]' \
	'file_open is good' \
=======
>>>>>>> master:grade.sh

<<<<<<< HEAD:grade.sh
quicktest 'file_get_block [testfsipc]' \
	'file_get_block is good' \

quicktest 'file_truncate [testfsipc]' \
	'file_truncate is good' \

quicktest 'file_flush [testfsipc]' \
	'file_flush is good' \

quicktest 'file rewrite [testfsipc]' \
	'file rewrite is good' \

quicktest 'serv_* [testfsipc]' \
	'serve_open is good' \
	'serve_map is good' \
	'serve_close is good' \
	'stale fileid is good' \

pts=5
runtest1 -tag 'motd display [writemotd]' writemotd \
	'OLD MOTD' \
	'This is /motd, the message of the day.' \
	'NEW MOTD' \
	'This is the NEW message of the day!' \

preservefs=y
runtest1 -tag 'motd change [writemotd]' writemotd \
	'OLD MOTD' \
	'This is the NEW message of the day!' \
	'NEW MOTD' \
	! 'This is /motd, the message of the day.' \

pts=8
preservefs=n
runtest1 -tag 'spawn via icode [icode]' icode \
	'icode: read /motd' \
	'This is /motd, the message of the day.' \
	'icode: spawn /init' \
	'init: running' \
	'init: data seems okay' \
	'icode: exiting' \
	'init: bss seems okay' \
	"init: args: 'init' 'initarg1' 'initarg2'" \
	'init: exiting' \

echo LAB 5 SCORE: $score/40

if [ $score -lt 40 ]; then
=======
score=0
timeout=10

runtest1 dumbfork \
	'.00000000. new env 00001000' \
	'.00000000. new env 00001001' \
	'0: I am the parent!' \
	'9: I am the parent!' \
	'0: I am the child!' \
	'9: I am the child!' \
	'19: I am the child!' \
	'.00001001. exiting gracefully' \
	'.00001001. free env 00001001' \
	'.00001002. exiting gracefully' \
	'.00001002. free env 00001002'

echo PART A SCORE: $score/5

runtest1 faultread \
	! 'I read ........ from location 0!' \
	'.00001001. user fault va 00000000 ip 008.....' \
        'TRAP frame at 0xf.......' \
	'  trap 0x0000000e Page Fault' \
	'  err  0x00000004' \
	'.00001001. free env 00001001'

runtest1 faultwrite \
	'.00001001. user fault va 00000000 ip 008.....' \
        'TRAP frame at 0xf.......' \
	'  trap 0x0000000e Page Fault' \
	'  err  0x00000006' \
	'.00001001. free env 00001001'

runtest1 faultdie \
	'i faulted at va deadbeef, err 6' \
	'.00001001. exiting gracefully' \
	'.00001001. free env 00001001' 

runtest1 faultalloc \
	'fault deadbeef' \
	'this string was faulted in at deadbeef' \
	'fault cafebffe' \
	'fault cafec000' \
	'this string was faulted in at cafebffe' \
	'.00001001. exiting gracefully' \
	'.00001001. free env 00001001'

runtest1 faultallocbad \
	'.00001001. user_mem_check assertion failure for va deadbeef' \
	'.00001001. free env 00001001' 

runtest1 faultnostack \
	'.00001001. user_mem_check assertion failure for va eebfff..' \
	'.00001001. free env 00001001'

runtest1 faultbadhandler \
	'.00001001. user_mem_check assertion failure for va (deadb|eebfe)...' \
	'.00001001. free env 00001001'

runtest1 faultevilhandler \
	'.00001001. user_mem_check assertion failure for va (f0100|eebfe)...' \
	'.00001001. free env 00001001'

runtest1 forktree \
	'....: I am .0.' \
	'....: I am .1.' \
	'....: I am .000.' \
	'....: I am .100.' \
	'....: I am .110.' \
	'....: I am .111.' \
	'....: I am .011.' \
	'....: I am .001.' \
	'.00001001. exiting gracefully' \
	'.00001002. exiting gracefully' \
	'.0000200.. exiting gracefully' \
	'.0000200.. free env 0000200.'

echo PART B SCORE: $score/50

runtest1 spin \
	'.00000000. new env 00001000' \
	'.00000000. new env 00001001' \
	'I am the parent.  Forking the child...' \
	'.00001001. new env 00001002' \
	'I am the parent.  Running the child...' \
	'I am the child.  Spinning...' \
	'I am the parent.  Killing the child...' \
	'.00001001. destroying 00001002' \
	'.00001001. free env 00001002' \
	'.00001001. exiting gracefully' \
	'.00001001. free env 00001001'

runtest1 pingpong \
	'.00000000. new env 00001000' \
	'.00000000. new env 00001001' \
	'.00001001. new env 00001002' \
	'send 0 from 1001 to 1002' \
	'1002 got 0 from 1001' \
	'1001 got 1 from 1002' \
	'1002 got 8 from 1001' \
	'1001 got 9 from 1002' \
	'1002 got 10 from 1001' \
	'.00001001. exiting gracefully' \
	'.00001001. free env 00001001' \
	'.00001002. exiting gracefully' \
	'.00001002. free env 00001002' \

runtest1 primes \
	'.00000000. new env 00001000' \
	'.00000000. new env 00001001' \
	'.00001001. new env 00001002' \
	'2 .00001002. new env 00001003' \
	'3 .00001003. new env 00001004' \
	'5 .00001004. new env 00001005' \
	'7 .00001005. new env 00001006' \
	'11 .00001006. new env 00001007' 

echo PART C SCORE: $score/65

if [ $score -lt 65 ]; then
>>>>>>> master:grade.sh
    exit 1
fi

<<<<<<< HEAD:grade.sh
=======


>>>>>>> master:grade.sh
