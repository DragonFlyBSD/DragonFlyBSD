" Accents in SGML
" $DragonFly: doc/share/examples/vim/accents-sgml.vim,v 1.1.1.1 2004/04/02 09:36:25 hmp Exp $

" don't do it twice
if !exists("autocommands_accents_sgml")
  let autocommands_accents_sgml = 1

  " on reads, have a native (iso-8859-1) representation
  autocmd BufReadPost,FileReadPost *.sgml,*.ent,*.html '[,']!$HOME/.vim/sgml2accents.pl

  " to correct some accents
  autocmd BufEnter * map! ^a â|map! ^e ê|map! ^i î|map! ^o ô|map! ^u û

  " on writes,  have the SGML syntax
  autocmd BufWritePre,FileWritePre *.sgml,*.ent,*.html '[,']!$HOME/.vim/accents2sgml.pl
  " then go back with native accents
  autocmd BufWritePost,FileWritePost *.sgml,*.ent,*.html '[,']!$HOME/.vim/sgml2accents.pl

endif
