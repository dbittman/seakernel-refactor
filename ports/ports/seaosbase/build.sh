NAME='seaosbase'
VERSION='0.4'
DESC='Base configuration and directory layout for SeaOS'
REQUIRES=()

SOURCES=()
PATCHED=()
ALLFILES=()

prepare() {
	return 0
}

build() {
	echo $INSTALLDIR
	mkdir -p $INSTALLDIR/{var/cache,tmp,etc,bin,users/root}
	ln -s bash $INSTALLDIR/bin/sh
	cat > $INSTALLDIR/etc/bashrc <<EOF
export CC=gcc
export CFLAGS="-O2 -g"

export SHELL=/bin/sh
export PAGER=less

export LS_COLORS='rs=0:di=01;34:ln=01;36:mh=00:pi=40;33:so=01;35:do=01;35:bd=40;33;01:cd=40;33;01:or=40;31;01:su=37;41:sg=30;43:ca=30;40:tw=

alias ls='ls --color=auto'
export EDITOR=vim

export PS1="\e[1;37m[\e[1;31m\u\e[1;37m:\e[1;33m\\w\e[1;37m]$\e[m "
alias lsmod='modprobe -l'
alias less='less -d'

export GROFF_NO_SGR=1
EOF

	cat > $INSTALLDIR/etc/passwd <<EOF
root:x:0:0:x:/users/root:/bin/sh
EOF

	cat > $INSTALLDIR/etc/profile <<EOF
export TERM=seaos
export BASH_ENV=/etc/bashrc
export TERM=seaos
export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/libexec:/usr/$TARGET/bin:.

export USER="\`id -un\`"
if [ -n "\${USER:+x}" ]; then
        export LOGNAME=\$USER
        export HOME="/users/\$LOGNAME"
fi

. /etc/bashrc
EOF

}

