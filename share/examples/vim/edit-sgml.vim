" Formatting SGML documents
" $DragonFly: doc/share/examples/vim/edit-sgml.vim,v 1.1.1.1 2004/04/02 09:36:25 hmp Exp $

if !exists("format_fdp_sgml")
  let format_fdp_sgml = 1
  " correction for highlighting special characters
  autocmd BufNewFile,BufRead *.sgml,*.ent,*.html syn match sgmlSpecial "&[^;]*;"

  " formatting DragonFlyBSD SGML/Docbook
  autocmd BufNewFile,BufRead *.sgml,*.ent set autoindent formatoptions=tcq2l textwidth=70 shiftwidth=2 softtabstop=2 tabstop=8
endif
