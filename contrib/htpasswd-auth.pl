#!/usr/bin/perl
#
# htpasswd-auth.pl: dreaderd authentication against an Apache htpasswd file.
#
# Use it in dreader.access (dreaderd must be built with PERL_ENABLED, which
# ./configure turns on where perl can be embedded):
#
#	authdef webusers
#	    perl /news/htpasswd-auth.pl -f /news/htpasswd
#	end
#	readerdef members
#	    read yes
#	    post yes
#	    auth webusers
#	end
#
# The htpasswd file is the one `htpasswd` (Apache), `openssl passwd` or any
# tool that writes "user:hash" lines makes.  The arguments after the script
# in the authdef are the arguments of the script:
#
#	-f FILE		the htpasswd file (required)
#	--plaintext	accept a plain text password in the file (off by default)
#
# Another authdef can use the same script with another file; a path with
# spaces goes in double quotes: perl /news/htpasswd-auth.pl -f "/news/my users".
#
# The file is cached per worker and reloaded only when it changes, so a new
# user, a changed password or a removed user still counts at once without
# restarting dreaderd: every login stat()s the file and rebuilds the cache
# when (mtime, size, inode) differ from the cached copy, or when the file was
# touched within the last couple of seconds (to catch a same-second,
# same-size edit).  The whole file is slurped and parsed from memory, so a
# login never sees a half-written file during an htpasswd rewrite.  Because
# the lookup is cached, an unauthenticated client looping AUTHINFO with an
# unknown user no longer forces a full-file scan on every attempt.
# (dreaderd runs the script in its DNS/auth worker processes, each its own
# single-threaded process with its own cache; the script itself is read once
# per worker, so a change of THIS file needs a restart of dreaderd.)
#
# Password hashes it checks:
#
#   $apr1$...      Apache MD5 (htpasswd -m, the default of older Apache)
#   {SHA}...       SHA-1 in base64 (htpasswd -s)
#   {SSHA}...      salted SHA-1 in base64 (LDAP style)
#   everything else is passed to the C library's crypt(3), so the schemes
#   of the platform work, as far as it has them:
#   $2y$ $2a$ $2b$ bcrypt (htpasswd -B), $6$ SHA-512, $5$ SHA-256, $1$ MD5,
#   $y$ yescrypt, and the traditional 13 character DES crypt (htpasswd -d).
#
# A hash that the platform cannot compute, such as bcrypt on a system whose
# crypt(3) lacks it, never matches: the login is refused.  `perl -e 'print
# crypt("pw", q($6$salt))'` shows what crypt(3) here can do.
#
# Self test, without dreaderd:   perl htpasswd-auth.pl -f FILE user password
# prints "100 Success" or "0".

use strict;
use warnings;
use Digest::MD5 qw(md5);
use Digest::SHA qw(sha1);
use MIME::Base64 qw(decode_base64 encode_base64);
use Getopt::Long qw(GetOptions);

our $HTPASSWD;			# -f FILE
our $ALLOW_PLAINTEXT = 0;	# --plaintext: a hash without a known format is a plain text password
our $SUCCESS = '100 Success';

# per-interpreter cache of the htpasswd file (each perl line gets its own
# interpreter, so these are naturally per -f file; workers are separate
# single-threaded processes, so no locking is needed)
our %USERS;			# user => stored hash, from the last load
our $LOADED = 0;		# %USERS reflects a real file
our ($MTIME, $SIZE, $INO) = (-1, -1, -1);	# stat of the cached copy

# the arguments of the script (dreaderd gives it those of the authdef)
GetOptions('f=s' => \$HTPASSWD, 'plaintext' => \$ALLOW_PLAINTEXT)
    or die "usage: htpasswd-auth.pl -f htpasswd-file [--plaintext]\n";
die "htpasswd-auth.pl: -f htpasswd-file is required\n" unless defined $HTPASSWD && length $HTPASSWD;

# --- the checks ----------------------------------------------------------

# Apache's variant of the MD5 crypt of FreeBSD ($apr1$salt$hash)
sub apr1_hash {
    my ($pw, $salt) = @_;
    my $magic = '$apr1$';
    $salt = substr($salt, 0, 8);
    my $final = md5($pw . $salt . $pw);
    my $ctx = $pw . $magic . $salt;
    for (my $n = length $pw; $n > 0; $n -= 16) {
	$ctx .= substr($final, 0, $n > 16 ? 16 : $n);
    }
    for (my $i = length $pw; $i; $i >>= 1) {
	$ctx .= ($i & 1) ? "\0" : substr($pw, 0, 1);
    }
    $final = md5($ctx);
    for my $i (0 .. 999) {
	my $c = ($i & 1) ? $pw : $final;
	$c .= $salt if $i % 3;
	$c .= $pw if $i % 7;
	$c .= ($i & 1) ? $final : $pw;
	$final = md5($c);
    }
    my @f = unpack('C*', $final);
    my $alphabet = './0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
    my $to64 = sub {
	my ($v, $n) = @_;
	my $s = '';
	while ($n-- > 0) {
	    $s .= substr($alphabet, $v & 0x3f, 1);
	    $v >>= 6;
	}
	return $s;
    };
    my $out = $to64->(($f[0] << 16) | ($f[6] << 8) | $f[12], 4)
	    . $to64->(($f[1] << 16) | ($f[7] << 8) | $f[13], 4)
	    . $to64->(($f[2] << 16) | ($f[8] << 8) | $f[14], 4)
	    . $to64->(($f[3] << 16) | ($f[9] << 8) | $f[15], 4)
	    . $to64->(($f[4] << 16) | ($f[10] << 8) | $f[5], 4)
	    . $to64->($f[11], 2);
    return $magic . $salt . '$' . $out;
}

# compare two strings in a time that does not depend on where they differ
sub same {
    my ($a, $b) = @_;
    return 0 unless defined $a && defined $b && length($a) == length($b);
    my $x = 0;
    $x |= ord(substr($a, $_, 1)) ^ ord(substr($b, $_, 1)) for 0 .. length($a) - 1;
    return $x == 0;
}

# does password $pw match the stored hash $hash?
sub password_ok {
    my ($pw, $hash) = @_;
    return 0 unless length $hash;

    if ($hash =~ /^\$apr1\$([^\$]{0,8})\$/) {
	return same(apr1_hash($pw, $1), $hash);
    }
    if ($hash =~ /^\{SHA\}(.*)$/) {
	return same(sha1($pw), decode_base64($1));
    }
    if ($hash =~ /^\{SSHA\}(.*)$/) {
	my $raw = decode_base64($1);
	return 0 if length($raw) < 20;
	my $salt = substr($raw, 20);
	return same(sha1($pw . $salt) . $salt, $raw);
    }
    # the platform's crypt(3): DES, $1$, $2y$, $5$, $6$, $y$ ...  A hash that crypt(3)
    # does not know comes back as a failure marker ("*0") or as a different string.
    if ($hash =~ /^\$[0-9a-z]+\$/ || $hash =~ m{^[./0-9A-Za-z]{13}$} || $hash =~ /^_/) {
	my $c = crypt($pw, $hash);
	return (defined $c && length $c > 1 && same($c, $hash)) ? 1 : 0;
    }
    return $ALLOW_PLAINTEXT ? same($pw, $hash) : 0;
}

# Load the htpasswd file into %USERS, but only when it has changed since the
# last load.  Returns 1 if %USERS is usable, 0 if the file is gone/unreadable
# (in which case the cache is dropped and every login is refused).
sub load_htpasswd {
    my @st = stat($HTPASSWD);
    unless (@st) {
	warn "htpasswd-auth: cannot stat $HTPASSWD: $!\n";
	%USERS = ();
	$LOADED = 0;
	($MTIME, $SIZE, $INO) = (-1, -1, -1);
	return 0;
    }
    my ($ino, $size, $mtime) = @st[1, 7, 9];
    my $now = time;
    # Trust the cache only when identity is unchanged AND the file has been
    # stable for a couple of seconds: within that window a same-second,
    # same-size edit could be invisible to (mtime,size,ino), so reload.
    return 1 if $LOADED
	     && $ino == $INO && $size == $SIZE && $mtime == $MTIME
	     && ($now - $mtime) >= 2;

    my $fh;
    unless (open($fh, '<', $HTPASSWD)) {
	warn "htpasswd-auth: cannot open $HTPASSWD: $!\n";
	%USERS = ();
	$LOADED = 0;
	($MTIME, $SIZE, $INO) = (-1, -1, -1);
	return 0;
    }
    # Slurp the whole file in one read and parse from memory, so a login never
    # parses a half-written file in the middle of an htpasswd rewrite.
    my $data = do { local $/; <$fh> };
    close($fh);

    my %u;
    for my $line (split(/\n/, defined $data ? $data : '')) {
	$line =~ s/\r\z//;
	next if $line eq '' || $line =~ /^\s*#/;
	my ($usr, $hash) = split(/:/, $line, 3);	# user:hash[:comment]
	next unless defined $hash;
	# the first line of a user decides, as in Apache
	$u{$usr} = $hash unless exists $u{$usr};
    }
    %USERS = %u;
    ($MTIME, $SIZE, $INO) = ($mtime, $size, $ino);
    $LOADED = 1;
    return 1;
}

# --- the hook ------------------------------------------------------------

# dreaderd calls this for every AUTHINFO PASS; the result "100 Success" logs the user in
sub checkuser {
    my ($user, $pass) = @_;
    return '0' unless defined $user && defined $pass && length $user && length $pass;
    return '0' if $user =~ /[:\r\n\0]/ || length($user) > 255 || length($pass) > 1024;

    return '0' unless load_htpasswd();	# file gone/unreadable: refuse, keep serving

    my $hash = $USERS{$user};
    return '0' unless defined $hash;		# unknown user: refuse
    return password_ok($pass, $hash) ? $SUCCESS : '0';
}

# --- self test: perl htpasswd-auth.pl user password ------------------------

if (@ARGV >= 2) {
    my ($u, $p) = @ARGV;
    print checkuser($u, $p), "\n";
}

1;
