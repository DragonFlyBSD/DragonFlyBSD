if &cp | set nocp | endif
map  ye:i/\<pa\>
map  :!exctags -R:!cscope -kcbqR
let s:cpo_save=&cpo
set cpo&vim
vmap gx <Plug>NetrwBrowseXVis
nmap gx <Plug>NetrwBrowseX
vnoremap <silent> <Plug>NetrwBrowseXVis :call netrw#BrowseXVis()
nnoremap <silent> <Plug>NetrwBrowseX :call netrw#BrowseX(expand((exists("g:netrw_gx")? g:netrw_gx : '<cfile>')),netrw#CheckIfRemote())
let &cpo=s:cpo_save
unlet s:cpo_save
set autoindent
set backspace=indent,eol,start
set nomodeline
set path=/usr/src/sys,/usr/obj/usr/src/sys/ORB
set ruler
set noshowmode
set nowritebackup
" vim: set ft=vim :
