; Pipeline-test map. One area with BITS (code + initialised data + reset),
; which is what gets written to the file and therefore what OMF carries;
; BSS lives separately and is created by RESSPC/the loader, not stored.
(define memories
  '((memory DirectPage (address (#x0000 . #x00ff)) (type ANY)
            (section registers ztiny tiny))
    (memory Bss (address (#x0800 . #x1fff)) (type ANY)
            (section stack data znear near heap zfar far huge))
    (memory Program (address (#x2000 . #xfff3)) (type ANY)
            (section code compactcode cdata cnear switch
                     itiny idata inear data_init_table
                     farcode cfar chuge ifar ihuge reset))
    ))
