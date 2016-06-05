NAME='vim'
VERSION='7.4'
DESC='Vi clone, text editor'
REQUIRES=('ncurses')

SOURCES=('ftp://ftp.vim.org/pub/vim/unix/vim-7.4.tar.bz2')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
	if [ -e vim-7.4 ]; then
		rm -rf vim-7.4
	fi
	mv vim74 vim-7.4
}

build() {
	cp -a ../vim-7.4/* .
	if ! ./configure $STDCONF vim_cv_toupper_broken=no --with-tlib=ncursesw vim_cv_terminfo=yes vim_cv_tty_mode=0620 vim_cv_tty_group=world vim_cv_getcwd_broken=no vim_cv_stat_ignores_slash=yes vim_cv_memmove_handles_overlap=yes --disable-sysmouse --disable-gpm --disable-xsmp --disable-xim LIBS="-lm -lncursesw" X_PRE_LIBS= --disable-nls; then
		return 1
	fi
	sed -i "s/\-lICE//g" src/auto/config.mk
	sed -i "s/\-lSM//g" src/auto/config.mk
	if ! make $STDMAKE; then
		return 1
	fi
	if ! make $STDINSTALL; then
		return 1
	fi
}
