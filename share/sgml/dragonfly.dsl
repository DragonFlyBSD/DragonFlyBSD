<!-- $DragonFly: doc/share/sgml/dragonfly.dsl,v 1.3 2004/06/26 03:15:46 justin Exp $ -->

<!DOCTYPE style-sheet PUBLIC "-//James Clark//DTD DSSSL Style Sheet//EN" [
<!ENTITY % output.html		"IGNORE">
<!ENTITY % output.html.images 	"IGNORE">
<!ENTITY % output.print 	"IGNORE">
<!ENTITY % output.print.pdf 	"IGNORE">
<!ENTITY % output.print.justify	"IGNORE">
<!ENTITY % output.print.twoside	"IGNORE">

<!ENTITY % dragonfly.l10n PUBLIC "-//DragonFlyBSD//ENTITIES DocBook Language Specific Entities//EN">
%dragonfly.l10n;
<!ENTITY % dragonfly.l10n-common PUBLIC "-//DragonFlyBSD//ENTITIES DocBook Language Neutral Entities//EN">
%dragonfly.l10n-common;

<![ %output.html; [
<!ENTITY docbook.dsl PUBLIC "-//Norman Walsh//DOCUMENT DocBook HTML Stylesheet//EN" CDATA DSSSL>
]]>
<![ %output.print; [
<!ENTITY docbook.dsl PUBLIC "-//Norman Walsh//DOCUMENT DocBook Print Stylesheet//EN" CDATA DSSSL>

]]>
]>

<style-sheet>
  <style-specification use="docbook">
    <style-specification-body>

      (declare-flow-object-class formatting-instruction
        "UNREGISTERED::James Clark//Flow Object Class::formatting-instruction")

      <!-- HTML only .................................................... -->
      
      <![ %output.html; [
        <!-- Configure the stylesheet using documented variables -->

        (define %hyphenation% #f)        <!-- Silence a warning -->

        (define %gentext-nav-use-tables%
          ;; Use tables to build the navigation headers and footers?
          #t)

        (define %html-ext%
          ;; Default extension for HTML output files
          ".html")

        (define %shade-verbatim%
          ;; Should verbatim environments be shaded?
          #f)

        (define %use-id-as-filename%
          ;; Use ID attributes as name for component HTML files?
          #t)
 
        (define %root-filename%
          ;; Name for the root HTML document
          "index")

        (define html-manifest
          ;; Write a manifest?
          #f)

        (define %generate-legalnotice-link%
          ;; Should legal notices be a link to a separate file?
          ;;
          ;; Naturally, this has no effect if you're building one big
          ;; HTML file.
          #f)

        (define %generate-docformat-navi-link%
          ;; Create docformat navi link for HTML output?
          #f)

        (define (book-titlepage-recto-elements)
          (list (normalize "title")
                (normalize "subtitle")
                (normalize "graphic")
                (normalize "mediaobject")
                (normalize "corpauthor")
                (normalize "authorgroup")
                (normalize "author")
                (normalize "editor")
                (normalize "copyright")
                (normalize "abstract")
                (normalize "legalnotice")
                (normalize "isbn")))

        ;; Create a simple navigation link
        ;; if %generate-docformat-navi-link% defined.
        (define (make-docformat-navi tlist)
          (let ((rootgi (gi (sgml-root-element))))
            (make element gi: "DIV"
                  attributes: '(("CLASS" "DOCFORAMTNAVI"))
                  (literal "[ ")
                  (make-docformat-navi-link rootgi tlist)
                  (literal " ]"))))

        (define (make-docformat-navi-link rootgi tlist)
          (make sequence
            (cond
             ((null? tlist)               (empty-sosofo))
             ((null? (car tlist))         (empty-sosofo))
             ((not (symbol? (car tlist))) (empty-sosofo))
             ((equal? (car tlist) 'html-split)
              (make sequence
                (create-link (list (list "href" "./index.html"))
                             (literal "&docnavi.split-html;"))
                (if (not (null? (cdr tlist)))
                    (make sequence
                      (literal " / ")
                      (make-docformat-navi-link rootgi (cdr tlist)))
                    (empty-sosofo))))
             ((equal? (car tlist) 'html-single)
              (make sequence
                (create-link (list (list "href"
                                         (string-append "./" (case-fold-down rootgi) ".html")))
                             (literal "&docnavi.single-html;"))
                (if (not (null? (cdr tlist)))
                    (make sequence
                      (literal " / ")
                      (make-docformat-navi-link rootgi (cdr tlist)))
                    (empty-sosofo))))
             (else (empty-sosofo)))))

        (define (article-titlepage-separator side)
          (make sequence
            (if %generate-docformat-navi-link%
                (make-docformat-navi '(html-split html-single))
                (empty-sosofo))
            (make empty-element gi: "HR")))

        (define (book-titlepage-separator side)
          (if (equal? side 'recto)
              (make sequence
                (if %generate-docformat-navi-link%
                    (make-docformat-navi '(html-split html-single))
                    (empty-sosofo)) 
                (make empty-element gi: "HR"))
              (empty-sosofo)))

        <!-- This is the text to display at the bottom of each page.
             Defaults to nothing.  The individual stylesheets should
             redefine this as necessary. -->
        (define ($email-footer$)
          (empty-sosofo))

	(define html-index-filename
	  (if nochunks
	    "html.index"
	    "html-split.index"))

	(define %stylesheet%
	  "docbook.css")

        <!-- This code handles displaying $email-footer$ at the bottom
             of each page.

             If "nochunks" is turned on then we make sure that an <hr>
             is shown first.

             Then create a centered paragraph ("<p>"), and reduce the font
             size ("<small>").  Then run $email-footer$, which should
             create the text and links as necessary. -->
	(define ($html-body-end$)
          (if (equal? $email-footer$ (normalize ""))
            (empty-sosofo)
            (make sequence
              (if nochunks
                  (make empty-element gi: "hr")
                  (empty-sosofo))
              ($email-footer$))))

        (define %refentry-xref-link%
          ;; REFENTRY refentry-xref-link
          ;; PURP Generate URL links when cross-referencing RefEntrys?
          ;; DESC
          ;; If true, a web link will be generated, presumably
          ;; to an online man->HTML gateway.  The text of the link is
          ;; generated by the $create-refentry-xref-link$ function.
          ;; /DESC
          ;; AUTHOR N/A
          ;; /REFENTRY
          #f)

        <!-- Specify how to generate the man page link HREF -->
        (define ($create-refentry-xref-link$ #!optional (n (current-node)))
          (let* ((r (select-elements (children n) (normalize "refentrytitle")))
                 (m (select-elements (children n) (normalize "manvolnum")))
                 (v (attribute-string (normalize "vendor") n))
                 (u (string-append "http://leaf.dragonflybsd.org/cgi/web-man?"
                         (data r) "+" (data m))))
            (case v
              (("current") (string-append u "&" "manpath=FreeBSD+5.2-current"))
              (("xfree86") (string-append u "&" "manpath=XFree86+4.3.0"))
              (("netbsd")  (string-append u "&" "manpath=NetBSD+1.6.1"))
              (("openbsd") (string-append u "&" "manpath=OpenBSD+3.3"))
              (("ports")   (string-append u "&" "manpath=FreeBSD+Ports"))
              (else u))))

        (element citerefentry
          (let ((href          ($create-refentry-xref-link$)))
            (if %refentry-xref-link%
              (create-link (list (list "HREF" href))
                (if %refentry-xref-italic%
                  ($italic-seq$)
                  ($charseq$)))
              (if %refentry-xref-italic%
                ($italic-seq$)
                ($charseq$)))))

	(element filename
	  (let*	((class		(attribute-string (normalize "role"))))
	    (cond
	     ((equal? class "package")
	      (let* ((urlurl	"http://www.FreeBSD.org/cgi/url.cgi")
		     (href	(string-append urlurl "?ports/"
					       (data (current-node))
					       "/pkg-descr")))
		(create-link (list (list "HREF" href)) ($mono-seq$))))
	     (else ($mono-seq$)))))

	;; Ensure that we start with no preferred mediaobject notations,
	;; so that in the text-only case we don't choose any of the
	;; possible images, and fallback to the most appropriate
	;; textobject
        (define preferred-mediaobject-notations
	  '())

	<!-- Convert " ... " to `` ... '' in the HTML output. -->
	(element quote
	  (make sequence
	    (literal "``")
	    (process-children)
	    (literal "''")))

	;; The special FreeBSD version of the trademark tag handling.
	;; This function was more or less taken from the DocBook DSSSL
	;; stylesheets by Norman Walsh.
	(element trademark
	  (if (show-tm-symbol? (current-node))
	      (make sequence
		($charseq$)	
		(cond
		 ((equal? (attribute-string "class") (normalize "copyright"))
		  (make entity-ref name: "copy"))
		 ((equal? (attribute-string "class") (normalize "registered"))
		  (make entity-ref name: "reg"))
		 ((equal? (attribute-string "class") (normalize "service"))
		  (make element gi: "SUP"
			(literal "SM")))
		 (else
		  (make entity-ref name: "#8482"))))
	      ($charseq$)))

      ]]>

      <!-- HTML with images  ............................................ -->

      <![ %output.html.images [

; The new Cascading Style Sheets for the HTML output are very confused
; by our images when used with div class="mediaobject".  We can
; clear up the confusion by ignoring the whole mess and just
; displaying the image.

        (element mediaobject
          (make element gi: "P"
            ($mediaobject$)))

        (define %graphic-default-extension%
          "png")

        (define %callout-graphics%
          ;; Use graphics in callouts?
          #t)

        (define %callout-graphics-ext%
          ;; The extension to use for callout images.  This is an extension
          ;; to the stylesheets, they do not support this functionality
          ;; natively.
          ".png")

        (define %callout-graphics-path%
          ;; Path to callout graphics
          "./imagelib/callouts/")

        ;; Redefine $callout-bug$ to support the %callout-graphic-ext%
        ;; variable.
        (define ($callout-bug$ conumber)
	  (let ((number (if conumber (format-number conumber "1") "0")))
	    (if conumber
		(if %callout-graphics%
	            (if (<= conumber %callout-graphics-number-limit%)
		        (make empty-element gi: "IMG"
			      attributes: (list (list "SRC"
				                      (root-rel-path
					               (string-append
						        %callout-graphics-path%
							number
	                                                %callout-graphics-ext%)))
		                                (list "HSPACE" "0")
			                        (list "VSPACE" "0")
				                (list "BORDER" "0")
					        (list "ALT"
						      (string-append
	                                               "(" number ")"))))
		        (make element gi: "B"
			      (literal "(" (format-number conumber "1") ")")))
	            (make element gi: "B"
		          (literal "(" (format-number conumber "1") ")")))
	        (make element gi: "B"
	       (literal "(??)")))))
      ]]>

      <!-- Two-sided Print output ....................................... --> 
      <![ %output.print.twoside; [

      ;; From an email by Ian Castle to the DocBook-apps list

      (define ($component$)
        (make simple-page-sequence
          page-n-columns: %page-n-columns%
          page-number-restart?: (or %page-number-restart% 
;			      (book-start?) 
				    (first-chapter?))
          page-number-format: ($page-number-format$)
          use: default-text-style
          left-header:   ($left-header$)
          center-header: ($center-header$)
          right-header:  ($right-header$)
          left-footer:   ($left-footer$)
          center-footer: ($center-footer$)
          right-footer:  ($right-footer$)
          start-indent: %body-start-indent%
          input-whitespace-treatment: 'collapse
          quadding: %default-quadding%
          (make sequence
	    ($component-title$)
	    (process-children))
          (make-endnotes)))

      ;; From an email by Ian Castle to the DocBook-apps list

      (define (first-part?)
        (let* ((book (ancestor (normalize "book")))
	       (nd   (ancestor-member (current-node)
				      (append
				       (component-element-list)
				       (division-element-list))))
	       (bookch (children book)))
        (let loop ((nl bookch))
	  (if (node-list-empty? nl)
	      #f
	      (if (equal? (gi (node-list-first nl)) (normalize "part"))
		  (if (node-list=? (node-list-first nl) nd)
		      #t
		      #f)
		  (loop (node-list-rest nl)))))))


      ;; From an email by Ian Castle to the DocBook-apps list

      (define (first-chapter?)
      ;; Returns #t if the current-node is in the first chapter of a book
        (if (has-ancestor-member? (current-node) (division-element-list))
          #f
         (let* ((book (ancestor (normalize "book")))
                (nd   (ancestor-member (current-node)
				       (append (component-element-list)
					       (division-element-list))))
		(bookch (children book))
		(bookcomp (expand-children bookch (list (normalize "part")))))
	   (let loop ((nl bookcomp))
	     (if (node-list-empty? nl)
		 #f
		 (if (equal? (gi (node-list-first nl)) (normalize "chapter"))
		     (if (node-list=? (node-list-first nl) nd)
			 #t
			 #f)
		     (loop (node-list-rest nl))))))))


      ; By default, the Part I title page will be given a roman numeral,
      ; which is wrong so we have to fix it

      (define (part-titlepage elements #!optional (side 'recto))
        (let ((nodelist (titlepage-nodelist 
			 (if (equal? side 'recto)
			     (part-titlepage-recto-elements)
			     (part-titlepage-verso-elements))
			 elements))
	      ;; partintro is a special case...
	      (partintro (node-list-first
			  (node-list-filter-by-gi elements (list (normalize "partintro"))))))
          (if (part-titlepage-content? elements side)
	      (make simple-page-sequence
		page-n-columns: %titlepage-n-columns%
		;; Make sure that page number format is correct.
		;; page-number-format: ($page-number-format$)
		;; Make sure that the page number is set to 1 if this is the first part
		;; in the book
		;; page-number-restart?: (first-part?)
		input-whitespace-treatment: 'collapse
		;; use: default-text-style
	  
		;; This hack is required for the RTF backend. If an
		;; external-graphic is the first thing on the page,
		;; RTF doesn't seem to do the right thing (the graphic
		;; winds up on the baseline of the first line of the
		;; page, left justified).  This "one point rule" fixes
		;; that problem.

		(make paragraph
		  line-spacing: 1pt
		  (literal ""))

		(let loop ((nl nodelist) (lastnode (empty-node-list)))
		  (if (node-list-empty? nl)
		      (empty-sosofo)
		      (make sequence
			(if (or (node-list-empty? lastnode)
				(not (equal? (gi (node-list-first nl))
					     (gi lastnode))))
			    (part-titlepage-before (node-list-first nl) side)
			    (empty-sosofo))
			(cond
			 ((equal? (gi (node-list-first nl)) (normalize "subtitle"))
			  (part-titlepage-subtitle (node-list-first nl) side))
			 ((equal? (gi (node-list-first nl)) (normalize "title"))
			  (part-titlepage-title (node-list-first nl) side))
			 (else
			  (part-titlepage-default (node-list-first nl) side)))
			(loop (node-list-rest nl) (node-list-first nl)))))
		(if (and %generate-part-toc%
			 %generate-part-toc-on-titlepage%
			 (equal? side 'recto))
		    (make display-group
		      (build-toc (current-node)
				 (toc-depth (current-node))))
		    (empty-sosofo))

		;; PartIntro is a special case
		(if (and (equal? side 'recto)
			 (not (node-list-empty? partintro))
			 %generate-partintro-on-titlepage%)
		    ($process-partintro$ partintro #f)
		    (empty-sosofo)))
	      (empty-sosofo))))

      ]]>

      <!-- Print with justification ..................................... --> 
      <![ %output.print.justify; [

        (define %default-quadding%
          'justify)

        (define %hyphenation%
          #t)


        ;; The url.sty package is making all of the links purple/pink.
        ;; Someone please fix this!

        (define (urlwrap)
          (let ((%factor% (if %verbatim-size-factor% 
			      %verbatim-size-factor% 
			      1.0)))
          (make sequence
	    font-family-name: %mono-font-family%
	    font-size: (* (inherited-font-size) %factor%)
	    (make formatting-instruction data:
		  (string-append
		   "\\url|"
		   (data (current-node))
		   "|")))))

        (define (pathwrap)
          (let ((%factor% (if %verbatim-size-factor% 
			      %verbatim-size-factor% 
			      1.0)))
          (make sequence
	    font-family-name: %mono-font-family%
	    font-size: (* (inherited-font-size) %factor%)
	    (make formatting-instruction data:
		  (string-append
		   "\\path|"
		   (data (current-node))
		   "|")))))

        ;; Some others may check the value of %hyphenation% and be
        ;; specified below

;        (element email
;          (make sequence
;            (literal "<")
;            (urlwrap)
;            (literal ">")))

        (element filename
	    (pathwrap))

        (element varname
	    (pathwrap))

      ]]>

      <!-- Print only ................................................... --> 
      <![ %output.print; [
        (define withpgpkeys
          #f)

        ;; If a link is entered as "file://localhost/usr/ports" in the docs
        ;; then we only want to display "/usr/ports" in printed form.

        (define (fix-url url)
          (if (and (> (string-length url) 15)
		   (string=? (substring url 0 16) "file://localhost"))
              (substring url 16 (string-length url))
              url))


        (element (primaryie ulink)
          (indexentry-link (current-node)))
        (element (secondaryie ulink)
          (indexentry-link (current-node)))
        (element (tertiaryie ulink)
          (indexentry-link (current-node)))

	;; Override the count-footnote? definition from dbblock.dsl
	;; to fix a bug.  Basically, the original procedure would count
	;; all ulink elements when doing %footnote-ulinks%.  It's
	;; actually harder than that, because ulink elements with no
	;; content shouldn't generate footnotes (the ulink element
	;; definition just inserts the url attribute in-line, thus there
	;; is no need for a footnote with the url).  So, when we figure
	;; out which footnotes to count for the purpose of determining
	;; footnote numbers, we only count the ulink elements containing
	;; content.
	(define (count-footnote? footnote)
	  ;; don't count footnotes in comments (unless you're showing comments)
	  ;; or footnotes in tables which are handled locally in the table
	  (if (or (and (has-ancestor-member? footnote (list (normalize "comment")))
		       (not %show-comments%))
		  (has-ancestor-member? footnote (list (normalize "tgroup")))
		  (and (has-ancestor-member? footnote (list (normalize "ulink")))
		       (node-list-empty? (children footnote))))
	      #f
	      #t))

        (element ulink 
          (make sequence
            (if (node-list-empty? (children (current-node)))
   	      (literal (fix-url (attribute-string (normalize "url"))))
  	      (make sequence
	        ($charseq$)
	        (if %footnote-ulinks%
		    (if (and (equal? (print-backend) 'tex) bop-footnotes)
		      (make sequence
			    ($ss-seq$ + (literal (footnote-number (current-node))))
			    (make page-footnote
			          (make paragraph
			font-size: (* %footnote-size-factor% %bf-size%)
			font-posture: 'upright
			quadding: %default-quadding%
			line-spacing: (* (* %footnote-size-factor% %bf-size%)
					 %line-spacing-factor%)
			space-before: %para-sep%
			space-after: %para-sep%
			start-indent: %footnote-field-width%
			first-line-start-indent: (- %footnote-field-width%)
			(make line-field
			  field-width: %footnote-field-width%
			  (literal (footnote-number (current-node))
				   (gentext-label-title-sep (normalize "footnote"))))
			(literal (fix-url (attribute-string (normalize "url")))))))
		      ($ss-seq$ + (literal (footnote-number (current-node)))))
	            (if (and %show-ulinks% 
		             (not (equal? (fix-url (attribute-string (normalize "url")))
				          (data-of (current-node)))))
  	   	        (make sequence
		          (literal " (")
			  (if %hyphenation%
			      (make formatting-instruction data:
				    (string-append "\\url{"
						   (fix-url (attribute-string
							     (normalize "url")))
						   "}"))
			      (literal (fix-url (attribute-string (normalize "url")))))
		          (literal ")"))
		        (empty-sosofo)))))))


        (define (toc-depth nd)
          (if (string=? (gi nd) (normalize "book"))
              3
              1))

        (element programlisting
          (if (and (equal? (attribute-string (normalize "role")) "pgpkey")
		   (not withpgpkeys))
              (empty-sosofo)
              (next-match)))

        (element legalnotice
          (if (equal? (attribute-string (normalize "role")) "trademarks")
	      (make sequence
	          (process-children))
              (next-match)))

        (define %body-start-indent% 
          0pi)

        (define (book-titlepage-verso-elements)
          (list (normalize "title")
                (normalize "subtitle")
                (normalize "corpauthor")
                (normalize "authorgroup")
                (normalize "author")
                (normalize "editor")
                (normalize "edition")
                (normalize "pubdate")
                (normalize "copyright")
                (normalize "abstract")
                (normalize "legalnotice")
                (normalize "revhistory")
                (normalize "isbn")))

        ;; Norm's stylesheets are smart about working out what sort of
        ;; object to display.  But this bites us.  Since we know that the
        ;; first item is going to be displayable, always use that.
        (define (find-displayable-object objlist notlist extlist)
          (let loop ((nl objlist))
            (if (node-list-empty? nl)
              (empty-node-list)
                (let* ((objdata  (node-list-filter-by-gi
                                  (children (node-list-first nl))
                                  (list (normalize "videodata")
                                        (normalize "audiodata")
                                        (normalize "imagedata"))))
                       (filename (data-filename objdata))
                       (extension (file-extension filename))
                       (notation (attribute-string (normalize "format") objdata)))
                  (node-list-first nl)))))

        ;; When selecting a filename to use, don't append the default
        ;; extension, instead, just use the bare filename, and let TeX
        ;; work it out.  jadetex will use the .eps file, while pdfjadetex
        ;; will use the .png file automatically.
        (define (graphic-file filename)
          (let ((ext (file-extension filename)))
            (if (or tex-backend   ;; TeX can work this out itself
                    (not filename)
                    (not %graphic-default-extension%)
                    (member ext %graphic-extensions%))
                 filename
                 (string-append filename "." %graphic-default-extension%))))

        ;; Including bitmaps in the PS and PDF output tends to scale them
        ;; horribly.  The solution is to scale them down by 50%.
        ;;
        ;; You could do this with 'imagedata scale="50"'  in the source,
        ;; but that will affect all the output formats that we use (because
        ;; there is only one 'imagedata' per image).
        ;;
        ;; Solution is to have the authors include the "FORMAT" attribute,
        ;; set to PNG or EPS as appropriate, but to omit the extension.
	;; If we're using the tex-backend, and the FORMAT is PNG, and the
        ;; author hasn't already set a scale, then set scale to 0.5.
        ;; Otherwise, use the supplied scale, or 1, as appropriate.
        (define ($graphic$ fileref
                           #!optional (display #f) (format #f)
                                      (scale #f)   (align #f))
          (let* ((graphic-format (if format format ""))
                 (graphic-scale  (if scale
                                     (/  (string->number scale) 100)
                                     (if (and tex-backend
                                              (equal? graphic-format "PNG"))
                                          0.5 1)))
                 (graphic-align  (cond ((equal? align (normalize "center"))
                                        'center)
                                       ((equal? align (normalize "right"))
                                        'end)
                                       (else
                                        'start))))
           (make external-graphic
              entity-system-id: (graphic-file fileref)
              notation-system-id: graphic-format
              scale: graphic-scale
              display?: display
              display-alignment: graphic-align)))

	;; The special FreeBSD version of the trademark tag handling.
	;; This function was more or less taken from the DocBook DSSSL
	;; stylesheets by Norman Walsh.
	(element trademark 
	  (if (show-tm-symbol? (current-node))
	      (make sequence
		($charseq$)
		(cond
		 ((equal? (attribute-string "class") (normalize "copyright"))
		  (literal "\copyright-sign;"))
		 ((equal? (attribute-string "class") (normalize "registered"))
		  (literal "\registered-sign;"))
		 ((equal? (attribute-string "class") (normalize "service"))
		  ($ss-seq$ + (literal "SM")))
		 (else
		  (literal "\trade-mark-sign;"))))
	      ($charseq$)))

	;; Make the trademark functions think print output has chunks.
	(define (chunk-parent nd)
	  (sgml-root-element nd))

      ]]>

      <![ %output.print.pdf; [

      ]]>

      <!-- Both sets of stylesheets ..................................... -->

      (define %section-autolabel%
        #t)

      (define %label-preface-sections%
        #f)

      (define %may-format-variablelist-as-table%
        #f)
      
      (define %indent-programlisting-lines%
        #f)
 
      (define %indent-screen-lines%
        #f)

      (define (article-titlepage-recto-elements)
        (list (normalize "title")
              (normalize "subtitle")
              (normalize "corpauthor")
              (normalize "authorgroup")
              (normalize "author")
              (normalize "releaseinfo")
              (normalize "copyright")
              (normalize "pubdate")
              (normalize "revhistory")
              (normalize "legalnotice")
              (normalize "abstract")))

      (define %admon-graphics%
        ;; Use graphics in admonitions?
        #f)

      (define %admon-graphics-path%
        ;; Path to admonition images
        "./imagelib/admon/")

      (define ($admon-graphic$ #!optional (nd (current-node)))
        ;; Admonition graphic file
        (string-append %admon-graphics-path% (case-fold-down (gi nd)) ".png"))

      (define %show-all-trademark-symbols%
        ;; Show all the trademark symbols, not just the required
        ;; symbols.
        #f)

      <!-- Slightly deeper customisations -->

      <!-- We would like the author attributions to show up in line
           with the section they refer to.  Authors who made the same
           contribution should be listed in a single <authorgroup> and 
           only one of the <author> elements should contain a <contrib>
           element that describes what the whole authorgroup was
           responsible for.  For example:

           <chapterinfo>
             <authorgroup>
               <author>
                 <firstname>Bob</firstname>
                 <surname>Jones</surname>
                 <contrib>Contributed by </contrib>
               </author>
               <author>
                 <firstname>Sarah</firstname>
                 <surname>Lee</surname>
               </author>
             </authorgroup>
           </chapterinfo>

           Would show up as "Contributed by Bob Jones and Sarah Lee".  Each
           authorgroup shows up as a separate sentence. -->
  

      (element chapterinfo 
        (process-children))
      (element sect1info 
        (process-children))
      (element sect2info 
        (process-children))
      (element sect3info 
        (process-children))
      (element sect4info 
        (process-children))
      (element sect5info 
        (process-children))
      (element (chapterinfo authorgroup author)
        (literal (author-list-string)))
      (element (sect1info authorgroup author)
        (literal (author-list-string)))
      (element (sect2info authorgroup author)
        (literal (author-list-string)))
      (element (sect3info authorgroup author)
        (literal (author-list-string)))
      (element (sect4info authorgroup author)
        (literal (author-list-string)))
      (element (sect5info authorgroup author)
        (literal (author-list-string)))

      (define (custom-authorgroup)
        ($italic-seq$
          (make sequence
            (process-node-list (select-elements (descendants (current-node))
                                  (normalize "contrib")))
            (process-children)
            (literal ".  "))))

      (element (chapterinfo authorgroup)
        (custom-authorgroup))
      (element (sect1info authorgroup)
        (custom-authorgroup))
      (element (sect2info authorgroup)
        (custom-authorgroup))
      (element (sect3info authorgroup)
        (custom-authorgroup))
      (element (sect4info authorgroup)
        (custom-authorgroup))
      (element (sect5info authorgroup)
        (custom-authorgroup))

      <!-- I want things marked up with 'sgmltag' eg., 

              <para>You can use <sgmltag>para</sgmltag> to indicate
                paragraphs.</para>

           to automatically have the opening and closing braces inserted,
           and it should be in a mono-spaced font. -->

      (element sgmltag ($mono-seq$
          (make sequence
            (literal "<")
            (process-children)
            (literal ">"))))

      <!-- Add double quotes around <errorname> text. -->

      (element errorname
        (make sequence
          <![ %output.html;  [ (literal "``") ]]>
          ($mono-seq$ (process-children))
          <![ %output.html;  [ (literal "''") ]]>
          ))

      <!-- John Fieber's 'instant' translation specification had 
           '<command>' rendered in a mono-space font, and '<application>'
           rendered in bold. 

           Norm's stylesheet doesn't do this (although '<command>' is 
           rendered in bold).

           Configure the stylesheet to behave more like John's. -->

      (element command ($mono-seq$))
      (element envar ($mono-seq$))

      (element application ($bold-seq$))

      <!-- Warnings and cautions are put in boxed tables to make them stand
           out. The same effect can be better achieved using CSS or similar,
           so have them treated the same as <important>, <note>, and <tip>
      -->
      (element warning ($admonition$))
      (element (warning title) (empty-sosofo))
      (element (warning para) ($admonpara$))
      (element (warning simpara) ($admonpara$))
      (element caution ($admonition$))
      (element (caution title) (empty-sosofo))
      (element (caution para) ($admonpara$))
      (element (caution simpara) ($admonpara$))

      <!-- Tell the stylesheet about our local customisations -->

      (element hostid 
        (if %hyphenation%
          (urlwrap)
          ($mono-seq$)))
      (element username ($mono-seq$))
      (element groupname ($mono-seq$))
      (element devicename ($mono-seq$))
      (element maketarget ($mono-seq$))
      (element makevar ($mono-seq$))

      <!-- Override generate-anchor.  This is used to generate a unique ID for
           each element that can be linked to.  The element-id function calls
           this one if there's no ID attribute that it can use.  Normally, we
           would just use the current element number.  However, if it's a
           a question then use the question's number, as determined by the
           question-answer-label function.

           This generates anchors of the form "Qx.y.", where x.y is the
           question label.  This will probably break if question-answer-label
           is changed to generate something that might be the same for two
           different questions (for example, if question numbering restarts
           for each qandaset. -->
      (define (generate-anchor #!optional (nd (current-node)))
        (cond
          ((equal? (gi nd) (normalize "question"))
            (string-append "Q" (question-answer-label)))
          (else
            (string-append "AEN" (number->string (all-element-number nd))))))
      
      (define (xref-biblioentry target)
        (let* ((abbrev (node-list-first
                        (node-list-filter-out-pis (children target))))
               (label  (attribute-string (normalize "xreflabel") target)))
                    
          (if biblio-xref-title
              (let* ((citetitles (select-elements (descendants target)
                                                  (normalize "citetitle")))
                     (titles     (select-elements (descendants target)
                                                  (normalize "title")))
                     (isbn       (select-elements (descendants target)
                                                  (normalize "isbn")))
                     (publisher  (select-elements (descendants target)
                                                  (normalize "publishername")))
                     (title      (if (node-list-empty? citetitles)
                                     (node-list-first titles)
                                     (node-list-first citetitles))))
                (with-mode xref-title-mode
                  (make sequence
                    (process-node-list title))))
              (if biblio-number
                  (make sequence
                    (literal "[" (number->string (bibentry-number target)) "]"))
                  (if label
                      (make sequence
                        (literal "[" label "]"))
                      (if (equal? (gi abbrev) (normalize "abbrev"))
                          (make sequence
                            (process-node-list abbrev))
                          (make sequence
                            (literal "[" (id target) "]"))))))))
 
       <!-- The (create-link) procedure should be used by all FreeBSD
 	   stylesheets to create links.  It calls (can-link-here) to
 	   determine whether it's okay to make a link in the current
 	   position.
 
 	   This check is necessary because links aren't allowed in,
 	   for example, <question> tags since the latter cause links
 	   to be created by themselves.  Obviously, nested links lead
 	   to all kinds of evil.  This normally wouldn't be a problem
 	   since no one in their right mind will put a <ulink> or
 	   <link> in a <question>, but it comes up when someone uses,
 	   say, a man page entity (e.g., &man.ls.1;); the latter may
 	   cause a link to be created, but its use inside a <question>
 	   is perfectly legal.
 
 	   The (can-link-here) routine isn't perfect; in fact, it's a
 	   hack and an ugly one at that.  Ideally, it would detect if
 	   the currect output would wind up in an <a> tag and return
 	   #f if that's the case.  Slightly less ideally it would
 	   check the current mode and return #f if, say, we're
 	   currently in TOC mode.  Right now, it makes a best guess
 	   attempt at guessing which tags might cause links to be
 	   generated.  -->
      (define (can-link-here)
 	(cond ((has-ancestor-member? (current-node)
				     '("TITLE" "QUESTION")) #f)
 	      (#t #t)))
 
      (define (create-link attrlist target)
 	(if (can-link-here)
 	    (make element gi: "A"
 		  attributes: attrlist
 		  target)
	    target))

      ;; Standard boolean XNOR (NOT Exclusive OR).
      (define (xnor x y)
	(or (and x y)
	    (and (not x) (not y))))

      ;; Standard boolean XOR (Exclusive OR).
      (define (xor x y)
	(not (xnor x y)))

      ;; Determine if a given node is in a title.
      (define (is-in-title? node)
	(has-ancestor-member? node (list (normalize "title"))))

      ;; Number of references to a trademark before the current
      ;; reference in each chunk.  Trademarks in title tags, and
      ;; trademarks in normal text (actually just text that is not in
      ;; title tags) are counted separately.
      (define ($chunk-trademark-number$ trademark)
	(let* ((trademarks (select-elements
			    (descendants (chunk-parent trademark))
			    (normalize "trademark"))))
	  (let loop ((nl trademarks) (num 1))
	    (if (node-list-empty? nl)
		num
		(if (node-list=? (node-list-first nl) trademark)
		    num
		    (if (and (string=? (data trademark)
				       (data (node-list-first nl)))
			     (xnor (is-in-title? trademark)
				   (is-in-title? (node-list-first nl))))
			(loop (node-list-rest nl) (+ num 1))
			(loop (node-list-rest nl) num)))))))

      ;; Determine if we should show a trademark symbol.  Either in
      ;; first occurrence in the proper context, if the role
      ;; attribute is set to force, or if %show-all-trademark-symbols%
      ;; is set to true.
      (define (show-tm-symbol? trademark)
	(or %show-all-trademark-symbols%
	    (= ($chunk-trademark-number$ trademark) 1)
	    (equal? (attribute-string (normalize "role") trademark) "force")))

    </style-specification-body>
  </style-specification>
      
  <external-specification id="docbook" document="docbook.dsl">
</style-sheet>
