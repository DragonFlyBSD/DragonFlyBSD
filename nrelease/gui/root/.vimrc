set statusline=%<%1*(%M%R)%f(%F)%=\ [%n]%1*%-19(%2*\ %03lx%02c(%p%%)\ %1*%)%O'%3*%02b%1*'
set errorformat=%A%f:%l:\ %m,%-Z%p^,%-C%.%#
set nocompatible
set nocp
set shiftwidth=4
set tabstop=4
set hlsearch
set showmatch
set incsearch
set laststatus=2
set bs=2

syntax on
filetype plugin on
filetype indent on

au BufNewFile,BufRead *.s set ft=asmMIPS syntax=asm
au BufNewFile,BufRead *.c,*.C,*.h,*.H set expandtab cin

map <F1> :call GotoBuf(1)<CR>
map <F2> :call GotoBuf(2)<CR>
map <F3> :call GotoBuf(3)<CR>
map <F4> :call GotoBuf(4)<CR>
map <F5> :call GotoBuf(5)<CR>
map <F6> :call GotoBuf(6)<CR>
map <F7> :call GotoBuf(7)<CR>
map <F8> :call GotoBuf(8)<CR>
map <F9> :call GotoBuf(9)<CR>
map <F10> :call GotoBuf(10)<CR>
map <F11> :call GotoBuf(11)<CR>
map <F12> :call GotoBuf(12)<CR>

hi User1 term=inverse,bold ctermbg=darkblue ctermfg=cyan guibg=#18163e guifg=grey
hi User2 term=inverse,bold ctermbg=darkblue ctermfg=cyan guibg=#0d0c22 guifg=grey
hi User3 term=inverse,bold ctermbg=darkblue ctermfg=lightred guibg=#18163e guifg=#ff5e6e
