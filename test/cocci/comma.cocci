//
//  convert comma to semicolon
//
// Target: Linux
// Copyright:  Copyright: 2014 - LIP6/INRIA
// License:  Licensed under GPLv2 or any later version.
// Author: Julia Lawall <Julia.Lawall@lip6.fr>
// URL: http://coccinelle.lip6.fr/ 
// URL: http://coccinellery.org/ 

@initialize:ocaml@
@@
let tbl = Hashtbl.create(100)

let add_if_not_present file =
try let _ = Hashtbl.find tbl file in ()
with Not_found ->
   Hashtbl.add tbl file ();
   let it = new iteration() in
   it#set_files [file];
   it#register()

@script:ocaml@
@@
Hashtbl.clear tbl

@r@
expression e1,e2,e;
position p1,p2;
type T;
identifier i;
@@

(
T i = ...;
|
 e;
)
 e1,@p1 e2@p2;

@script:ocaml@
p1 << r.p1;
p2 << r.p2;
@@

if (List.hd p1).line = (List.hd p2).line
then include_match false
else add_if_not_present ((List.hd p1).file)

@@
expression e1,e2;
position r.p1;
@@

 e1
- ,@p1
+ ;
  e2;
