;;; This function switches C-mode so that it indents stuff according to
;;; our style(9) which is equivalent to FreeBSD's. Tested with emacs-22.3.
;;;
;;; Use "M-x bsd" in a C mode buffer to activate it.
;;;
;;; To make this the default, use a line like this, but you can't easily
;;; switch back to default GNU style, since the old state isn't saved.
;;;
;;; (add-hook 'c-mode-common-hook 'bsd)
;;;
;;; As long as you don't have this in the c-mode hook you can edit GNU
;;; and BSD style C sources within one emacs session with no problem.
;;;
;;; Posted to FreeBSD's cvs-all by DES (<867ifoaulz.fsf@ds4.des.no>).

(defun bsd ()
  (interactive)
  (c-set-style "bsd")

  ;; Basic indent is 8 spaces
  (setq c-basic-offset 8)
  (setq tab-width 8)

  ;; Continuation lines are indented 4 spaces
  (c-set-offset 'arglist-cont 4)
  (c-set-offset 'arglist-cont-nonempty 4)
  (c-set-offset 'statement-cont 4)
  (c-set-offset 'cpp-macro-cont 8)

  ;; Labels are flush to the left
  (c-set-offset 'label [0])

  ;; Fill column
  (setq fill-column 74))
