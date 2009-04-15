<<<<<<< HEAD:boot/sign.pl
#!/usr/bin/perl
=======
#!/bin/perl
>>>>>>> master:boot/sign.pl

<<<<<<< HEAD:boot/sign.pl
open(BB, $ARGV[0]) || die "open $ARGV[0]: $!";
=======
open(SIG, $ARGV[0]) || die "open $ARGV[0]: $!";
>>>>>>> master:boot/sign.pl

<<<<<<< HEAD:boot/sign.pl
binmode BB;
my $buf;
read(BB, $buf, 1000);
$n = length($buf);
=======
$n = sysread(SIG, $buf, 1000);
>>>>>>> master:boot/sign.pl

if($n > 510){
	print STDERR "boot block too large: $n bytes (max 510)\n";
	exit 1;
}

print STDERR "boot block is $n bytes (max 510)\n";

$buf .= "\0" x (510-$n);
$buf .= "\x55\xAA";

<<<<<<< HEAD:boot/sign.pl
open(BB, ">$ARGV[0]") || die "open >$ARGV[0]: $!";
binmode BB;
print BB $buf;
close BB;
=======
open(SIG, ">$ARGV[0]") || die "open >$ARGV[0]: $!";
print SIG $buf;
close SIG;
>>>>>>> master:boot/sign.pl
