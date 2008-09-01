nmap <C-L> :!ld\ -o\ %<\ -L\ /usr/lib/gcc/mips-linux-gnu/4.1.2/crtbegin.o\ %.o\ -lc
set makeprg=as\ -march=r5000\ %\ -o\ %<.o
