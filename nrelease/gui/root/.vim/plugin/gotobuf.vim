function! GotoBuf(n)
    let n = a:n
    let i = 1
    let c = 1
    while i <= bufnr('$')
        if bufexists(i) && buflisted(i)
            if c == n
                execute "buffer! ".i
                return
            endif
            let c = c + 1
        endif
        let i = i + 1
    endwhile
endfunction

